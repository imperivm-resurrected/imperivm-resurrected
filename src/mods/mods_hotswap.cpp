// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>

#include "MinHook.h"
#include "core/log.h"
#include "addr/state.h"
#include "hooks/media.h"
#include "mods/hotswap.h"
#include "mods/mods_internal.h"
#include "mods/vfs.h"

using namespace mods_impl;

namespace {
// The ruleset whose data the game holds. The game reads it once per process, so it is the ruleset that was mounted when it started; it
// only changes when game/hotreload.cpp reads another one. Not known until the first swap.
ModRuleset g_engineRules;
bool g_engineKnown = false;
// game/hotreload.cpp has hooked the start of a match. Without that hook no ruleset change can skip the restart.
std::atomic<bool> g_ready{false};
// A switch chosen while a game was running, to be mounted at the start of the next game (see GameKindCanSwitchWhileRunning).
ModRuleset g_pending;
bool g_hasPending = false;
unsigned g_pendingTick = 0;

// The game object that exists, or null in the menus and the rooms.
void *RunningGame()
{
	return *reinterpret_cast<void *const *>(addr::kGame);
}

void RememberRuleset(const ModRuleset &wanted)
{
	const std::string name = ModsMountedProfile().name;
	ModProfile p = ModsLoadProfile(name);
	p.ruleset = wanted;
	if (!ModsSaveProfile(p)) LogW("content", "the rules could not be saved in the profile \"%s\"; they stay for this session", name.c_str());
	ModsMountedChoiceSet(wanted);
}

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

// What changes with a ruleset: the files of the folder it mounts and the global files of the root of its package (SwapFilesOf), compared with
// the ruleset `other`. False if the ruleset is not there. Nothing for the game's own rules. A package entry (a package with civilizations and no
// ruleset) mounts no folder, but its global root files come with it.
bool LayerFiles(const ModRuleset &r, const ModRuleset &other, std::vector<std::string> &out)
{
	out.clear();
	if (r.pack.empty()) return true;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if (!SameFolder(m.folder, r.pack)) continue;
		std::vector<std::string> layerFiles;
		if (!r.id.empty()) {
			const ModLayer *layer = ModsFindLayer(m, r.id);
			if (!layer) return false;
			layerFiles = layer->files;
		}
		out = SwapFilesOf(layerFiles, m.globalLoose, SameFolder(m.folder, other.pack));
		return true;
	}
	return false;
}

// Whether the packs of two rulesets are the same pack, for the global files of a root: both none counts.
bool SamePack(const ModRuleset &a, const ModRuleset &b)
{
	return SameFolder(a.pack, b.pack);
}
}

bool ModsSameMount(const ModRuleset &a, const ModRuleset &b)
{
	if (!ModsSameRuleset(a, b)) return false;
	if (SamePack(a, b)) return true;
	// The same folders are mounted. What differs is which package's root files that replace the game's are in use: only those of the package whose
	// ruleset is chosen (mods/rootrules.h), so two choices of different packs are the same mount only if neither pack has any.
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if ((SameFolder(m.folder, a.pack) || SameFolder(m.folder, b.pack)) && !m.globalLoose.empty()) return false;
	}
	return true;
}

void ModsHotReloadSetReady(bool ready)
{
	g_ready = ready;
}

bool ModsHotSwitchAvailable()
{
	return g_ready;
}

bool ModsCanSwitchWithoutRestart(const ModRuleset &wanted)
{
	if (!g_ready) return false;
	// A game that is running has the rules it started with and keeps them until it is gone. A switch chosen then waits for the next game (see
	// ModsSwitchRulesetWithoutRestart); it is only possible for the kinds of game whose load ends in LoadGameData on a new game object.
	if (void *game = RunningGame()) {
		if (!GameKindCanSwitchWhileRunning(GameKindOfVtable(*static_cast<const uintptr_t *>(game)))) return false;
	}
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
	if (!ModsSameChoice(to, wanted) || ModsSameMount(from, to)) return false;
	// What the game has to be made to read is the difference between the rules it holds and the ones asked for.
	std::vector<std::string> fromFiles, toFiles;
	if (!LayerFiles(engine, to, fromFiles) || !LayerFiles(to, engine, toFiles)) return false;
	return SwapDecide(ModsSameMount(engine, to), fromFiles, toFiles) != HotSwap::kRestart;
}

namespace {
// Mounts wanted now, or, if a game is running, keeps it for the start of the next one. deferred says which.
bool SwitchOrWait(const ModRuleset &wanted, bool &deferred)
{
	deferred = false;
	if (!ModsCanSwitchWithoutRestart(wanted)) return false;
	if (!RunningGame()) return ModsSwapMountedRuleset(wanted);
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_pending = wanted;
		g_hasPending = true;
		g_pendingTick = GetTickCount();
	}
	deferred = true;
	LogI("content", "ruleset %s%s%s chosen while a game is running: it is mounted when the next game starts, after this one is gone", wanted.pack.c_str(),
		wanted.id.empty() ? "" : ":", wanted.id.c_str());
	return true;
}
}

bool ModsSwitchRulesetWithoutRestart(const ModRuleset &wanted)
{
	bool deferred;
	return SwitchOrWait(wanted, deferred);
}

ModsPending ModsApplyPendingSwitch()
{
	ModRuleset wanted;
	unsigned tick;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_hasPending) return ModsPending::kNone;
		g_hasPending = false;
		wanted = g_pending;
		tick = g_pendingTick;
	}
	if (!PendingSwitchStillValid(tick, GetTickCount())) {
		LogW("content", "the ruleset %s:%s that was chosen %u ms ago for the next game is dropped (too old: the load did not start a game)", wanted.pack.c_str(),
			wanted.id.c_str(), GetTickCount() - tick);
		return ModsPending::kNone;
	}
	// The profile keeps it first: if mounting fails the game restarts, and the new process has to mount this one.
	RememberRuleset(wanted);
	if (!ModsSwapMountedRuleset(wanted)) return ModsPending::kFailed;
	LogI("content", "ruleset %s%s%s mounted at the start of the game, after the running one was gone", wanted.pack.c_str(), wanted.id.empty() ? "" : ":", wanted.id.c_str());
	return ModsPending::kApplied;
}

void ModsDropPendingSwitch(const char *why)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_hasPending) return;
	g_hasPending = false;
	LogW("content", "the ruleset %s:%s chosen for the next game is dropped (%s)", g_pending.pack.c_str(), g_pending.id.c_str(), why);
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
		// The global files of the root of the package whose ruleset is now in use are mounted and the other packs' are ignored.
		PublishRootRefusals(now);
		PublishVolumeFiles(now);
		PublishLayerDiag(now);
		PublishFileSources(now);
	}
	for (void (*hook)() : SwapHooks()) hook();
	std::vector<std::string> files;
	LayerFiles(next, before, files);
	LogI("content", "ruleset changed without a restart: %s%s%s -> %s%s%s (files of the new one: %s; loose layer: %s%s)", before.pack.c_str(),
		before.id.empty() ? "" : ":", before.id.c_str(), next.pack.c_str(), next.id.empty() ? "" : ":", next.id.c_str(),
		HotSwapKindsText(HotSwapKinds(files)).c_str(), newLoose.empty() ? "none" : "mounted", oldLoose.empty() ? "" : ", the old one taken out");
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
	if (ModsSameMount(engine, mounted)) return ModsReload::kNone;
	std::vector<std::string> fromFiles, toFiles;
	if (!LayerFiles(engine, mounted, fromFiles) || !LayerFiles(mounted, engine, toFiles)) return ModsReload::kRestart;
	return g_ready && SwapDecide(false, fromFiles, toFiles) == HotSwap::kReload ? ModsReload::kReload : ModsReload::kRestart;
}

void ModsOnRulesetSwapped(void (*hook)())
{
	SwapHooks().push_back(hook);
}

void ModsHotReloadDone()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_engineRules = g_mount.profile.ruleset;
}

bool ModsSwitchRulesetAndRemember(const ModRuleset &wanted)
{
	bool deferred;
	if (!SwitchOrWait(wanted, deferred)) return false;
	// A switch that waits for the next game is remembered when it is mounted (ModsApplyPendingSwitch).
	if (!deferred) RememberRuleset(wanted);
	return true;
}

bool ModsFindRoomRuleset(const std::vector<RoomPackFact> &packs, const std::string &rulesetPackage, ModRuleset &out)
{
	out = ModRuleset();
	const RoomPackFact *with = nullptr;
	for (const RoomPackFact &f : packs) {
		if (f.layer.empty()) continue;
		if (with) return false;
		with = &f;
	}
	if (with && !rulesetPackage.empty()) return false;
	if (!with && !rulesetPackage.empty()) {
		// The room's ruleset is a package with no ruleset of its own: this computer needs that exact copy, mounted.
		const RoomPackFact *fact = nullptr;
		for (const RoomPackFact &f : packs) {
			if (_stricmp(f.id.c_str(), rulesetPackage.c_str()) == 0) fact = &f;
		}
		if (!fact) return false;
		std::vector<std::string> mounted;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			mounted = g_mount.profile.mods;
		}
		std::lock_guard<std::mutex> lock(g_lock);
		for (const ModInfo &m : g_mods) {
			if (_stricmp(m.id.c_str(), fact->id.c_str()) != 0 || m.version != fact->version || ModsCompareRoomCopy(m, fact->copyKey) != RoomCopy::kSame) continue;
			if (!m.layers.empty() || std::none_of(mounted.begin(), mounted.end(), [&](const std::string &f) { return SameFolder(f, m.folder); })) continue;
			out.pack = m.folder;
			return true;
		}
		return false;
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
