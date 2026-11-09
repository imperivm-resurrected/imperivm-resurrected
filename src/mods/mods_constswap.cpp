// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>

#include "MinHook.h"
#include "core/log.h"
#include "hooks/media.h"
#include "mods/mods_internal.h"
#include "mods/vfs.h"

using namespace mods_impl;

namespace {
// The ruleset whose data/const.ini the game's own table holds. The game reads that file once per process, so it is the ruleset that was
// mounted when it started; it only changes when game/constreload.cpp loads another one. Not known until the first swap.
ModRuleset g_engineRules;
bool g_engineKnown = false;

bool SameFolder(const std::string &a, const std::string &b)
{
	return _stricmp(a.c_str(), b.c_str()) == 0;
}

// The folder a profile mounts for a ruleset (the same string MountProfileLayers puts in g_mount.dirs); empty if it mounts none.
std::string LayerDir(const ModRuleset &r)
{
	ModInfo m;
	if (r.Empty() || !ModsFind(r.pack, m)) return std::string();
	const ModLayer *layer = ModsFindLayer(m, r.id);
	if (!layer) return std::string();
	std::string dir = ModsDir() + "\\" + m.folder + "\\" + layer->path;
	std::replace(dir.begin(), dir.end(), '/', '\\');
	return dir;
}
}

bool ModsSwapMountedRuleset(const ModRuleset &wanted)
{
	ModProfile p;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.known) return false;
		p = g_mount.profile;
	}
	const ModRuleset before = p.ruleset;
	p.ruleset = wanted;
	const ModRuleset next = ModsEffectiveRuleset(p);
	const std::string oldDir = LayerDir(before), newDir = LayerDir(next);
	std::vector<std::string> dirs;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_engineKnown) {
			g_engineRules = before;
			g_engineKnown = true;
		}
		std::vector<std::string> &mounted = g_mount.dirs;
		if (!oldDir.empty()) {
			mounted.erase(std::remove_if(mounted.begin(), mounted.end(), [&](const std::string &d) { return SameFolder(d, oldDir); }), mounted.end());
		}
		if (!newDir.empty()) {
			// A ruleset goes right above its package, as MountProfileLayers mounts it.
			const std::string root = ModsDir() + "\\" + next.pack;
			const auto at = std::find_if(mounted.begin(), mounted.end(), [&](const std::string &d) { return SameFolder(d, root); });
			mounted.insert(at, newDir);
		}
		g_mount.profile.ruleset = next;
		dirs = mounted;
	}
	VfsResetPatched("data/const.ini");
	MediaSetModDirs(dirs);
	LogI("content", "ruleset changed without a restart: %s%s%s -> %s%s%s", before.pack.c_str(), before.id.empty() ? "" : ":", before.id.c_str(),
		next.pack.c_str(), next.id.empty() ? "" : ":", next.id.c_str());
	return true;
}

bool ModsConstReloadPending()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_engineKnown && g_mount.known && !ModsSameRuleset(g_engineRules, g_mount.profile.ruleset);
}

void ModsConstReloadDone()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_engineRules = g_mount.profile.ruleset;
}

void ModsConstReloadFailed()
{
	ModRuleset back;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		back = g_engineRules;
	}
	ModsSwapMountedRuleset(back);
	std::lock_guard<std::mutex> lock(g_lock);
	g_mount.choice = back;
}
