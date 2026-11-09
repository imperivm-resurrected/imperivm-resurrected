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
std::atomic<bool> g_ready{false}, g_fullReady{false};

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

// The VFS name of the folder of a ruleset that has loose files ("mods/<pack>/rulesets/<id>/", as MountProfileLayers names it); empty if none.
std::string LooseLayerPrefix(const ModRuleset &r)
{
	ModInfo m;
	if (r.Empty() || !ModsFind(r.pack, m)) return std::string();
	const ModLayer *layer = ModsFindLayer(m, r.id);
	return layer && layer->hasLoose ? "mods/" + m.folder + "/" + layer->path + "/" : std::string();
}

std::vector<void (*)()> &SwapHooks()
{
	static std::vector<void (*)()> hooks;
	return hooks;
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

void ModsFullReloadSetReady(bool ready)
{
	g_fullReady = ready;
}

bool ModsHotSwitchEnabled()
{
	return g_ready || g_fullReady;
}

bool ModsCanSwitchWithoutRestart(const ModRuleset &wanted, bool allowFull)
{
	const bool full = g_fullReady && allowFull;
	if (!g_ready && !full) return false;
	// A running game has the rules it started with; this is only for the menus and the room before the match.
	if (*reinterpret_cast<void *const *>(addr::kGame) != nullptr) return false;
	ModProfile p;
	ModRuleset engine;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_mount.known) return false;
		p = g_mount.profile;
		engine = g_engineKnown ? g_engineRules : p.ruleset;
	}
	const ModRuleset from = p.ruleset;
	p.ruleset = wanted;
	const ModRuleset to = ModsEffectiveRuleset(p);
	// The profile may not be able to mount what was asked (its package is not enabled): then it is not this switch.
	if (!ModsSameChoice(to, wanted) || ModsSameRuleset(from, to)) return false;
	// What the game has to be made to read is the difference between the rules it holds and the ones asked for.
	std::vector<std::string> fromFiles, toFiles;
	if (!LayerFiles(engine, fromFiles) || !LayerFiles(to, toFiles)) return false;
	switch (SwapDecide(ModsSameRuleset(engine, to), full, fromFiles, toFiles)) {
	case ConstSwap::kNothing: return true;
	case ConstSwap::kReload: return g_ready || full;
	case ConstSwap::kFull: return full;
	default: return false;
	}
}

bool ModsSwitchRulesetWithoutRestart(const ModRuleset &wanted, bool allowFull)
{
	return ModsCanSwitchWithoutRestart(wanted, allowFull) && ModsSwapMountedRuleset(wanted);
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
	const bool sameFiles = ModsSameRuleset(before, next);
	const std::string oldLoose = sameFiles ? std::string() : LooseLayerPrefix(before), newLoose = sameFiles ? std::string() : LooseLayerPrefix(next);
	// The loose files of the new ruleset are mounted before anything else changes, so a failure leaves everything as it was.
	if (!newLoose.empty() && !MountLooseFolder(newLoose)) {
		LogE("content", "the loose files of the ruleset %s could not be mounted; nothing was changed", newLoose.c_str());
		return false;
	}
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
	if (!oldLoose.empty()) UnmountLooseFolder(oldLoose);
	VfsResetPatchedAll();
	MediaSetModDirs(dirs);
	{
		// The files the packs hide are the ones of the new ruleset's hidden.txt; the game asks the hidden set on every open and search.
		ModProfile now;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			now = g_mount.profile;
		}
		ApplyHiddenFiles(now);
		PublishVolumeFiles(now);
		PublishLayerDiag(now);
	}
	for (void (*hook)() : SwapHooks()) hook();
	std::vector<std::string> files;
	LayerFiles(next, files);
	LogI("content", "ruleset changed without a restart: %s%s%s -> %s%s%s (files of the new one: %s; loose layer: %s%s)", before.pack.c_str(),
		before.id.empty() ? "" : ":", before.id.c_str(), next.pack.c_str(), next.id.empty() ? "" : ":", next.id.c_str(),
		FullSwapKindsText(FullSwapKinds(files)).c_str(), newLoose.empty() ? "none" : "mounted", oldLoose.empty() ? "" : ", the old one taken out");
	return true;
}

ModsReload ModsReloadPlan()
{
	ModRuleset engine, mounted;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_engineKnown || !g_mount.known) return ModsReload::kNone;
		engine = g_engineRules;
		mounted = g_mount.profile.ruleset;
	}
	if (ModsSameRuleset(engine, mounted)) return ModsReload::kNone;
	std::vector<std::string> fromFiles, toFiles;
	if (!LayerFiles(engine, fromFiles) || !LayerFiles(mounted, toFiles)) return ModsReload::kRestart;
	switch (SwapDecide(false, g_fullReady, fromFiles, toFiles)) {
	case ConstSwap::kReload: return g_ready ? ModsReload::kConst : g_fullReady ? ModsReload::kFull : ModsReload::kRestart;
	case ConstSwap::kFull: return ModsReload::kFull;
	default: return ModsReload::kRestart;
	}
}

void ModsOnRulesetSwapped(void (*hook)())
{
	SwapHooks().push_back(hook);
}

bool ModsConstReloadPending()
{
	return ModsReloadPlan() != ModsReload::kNone;
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

bool ModsSwitchRulesetAndRemember(const ModRuleset &wanted, bool allowFull)
{
	if (!ModsSwitchRulesetWithoutRestart(wanted, allowFull)) return false;
	const std::string name = ModsMountedProfile().name;
	ModProfile p = ModsLoadProfile(name);
	p.ruleset = wanted;
	if (!ModsSaveProfile(p)) LogW("content", "the rules could not be saved in the profile \"%s\"; they stay for this session", name.c_str());
	ModsMountedChoiceSet(wanted);
	return true;
}

bool ModsFindRoomRuleset(const std::vector<RoomPackFact> &packs, ModRuleset &out)
{
	out = ModRuleset();
	const RoomPackFact *with = nullptr;
	for (const RoomPackFact &f : packs) {
		if (f.layer.empty()) continue;
		if (with) return false;
		with = &f;
	}
	if (!with) return true;
	std::vector<std::string> mounted;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		mounted = g_mount.profile.mods;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if (_stricmp(m.id.c_str(), with->id.c_str()) != 0 || m.version != with->version) continue;
		if (ModsCompareRoomCopy(m, with->copyKey) != RoomCopy::kSame || ModsCompareRoomLayer(m, with->layer, with->layerKey) != RoomCopy::kSame) continue;
		if (std::none_of(mounted.begin(), mounted.end(), [&](const std::string &f) { return SameFolder(f, m.folder); })) continue;
		const ModLayer *layer = ModsFindLayer(m, with->layer);
		if (!layer) continue;
		out.pack = m.folder;
		out.id = layer->id;
		return true;
	}
	return false;
}
