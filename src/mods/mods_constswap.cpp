// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>

#include "MinHook.h"
#include "core/log.h"
#include "addr/state.h"
#include "hooks/media.h"
#include "mods/constswap.h"
#include "mods/mods_internal.h"
#include "mods/vfs.h"

using namespace mods_impl;

namespace {
// The ruleset whose data/const.ini the game's own table holds. The game reads that file once per process, so it is the ruleset that was
// mounted when it started; it only changes when game/constreload.cpp loads another one. Not known until the first swap.
ModRuleset g_engineRules;
bool g_engineKnown = false;
std::atomic<bool> g_ready{false};

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

// The files of the folder a ruleset mounts; false if the ruleset is not there. Nothing for the game's own rules.
bool LayerFiles(const ModRuleset &r, std::vector<std::string> &out)
{
	out.clear();
	if (r.Empty()) return true;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if (!SameFolder(m.folder, r.pack)) continue;
		const ModLayer *layer = ModsFindLayer(m, r.id);
		if (!layer) return false;
		out = layer->files;
		return true;
	}
	return false;
}
}

void ModsConstReloadSetReady(bool ready)
{
	g_ready = ready;
}

bool ModsCanSwitchWithoutRestart(const ModRuleset &wanted)
{
	if (!g_ready) return false;
	// A running game has the rules it started with; this is only for the menus and the room before the match.
	if (*reinterpret_cast<void *const *>(addr::kGame) != nullptr) return false;
	ModProfile p;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.known) return false;
		p = g_mount.profile;
	}
	const ModRuleset from = p.ruleset;
	p.ruleset = wanted;
	const ModRuleset to = ModsEffectiveRuleset(p);
	// The profile may not be able to mount what was asked (its package is not enabled): then it is not this switch.
	if (!ModsSameChoice(to, wanted)) return false;
	std::vector<std::string> fromFiles, toFiles;
	if (!LayerFiles(from, fromFiles) || !LayerFiles(to, toFiles)) return false;
	return ConstSwapDecide(ModsSameRuleset(from, to), fromFiles, toFiles) == ConstSwap::kReload;
}

bool ModsSwitchRulesetWithoutRestart(const ModRuleset &wanted)
{
	return ModsCanSwitchWithoutRestart(wanted) && ModsSwapMountedRuleset(wanted);
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
