// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/gamemodes.h"
#include "mods/lang.h"
#include "mods/modoptions.h"
#include "ui/mpui_internal.h"
#include "ui/mpui_league_room.h"

namespace mpui_impl {
namespace {

const int kOptGameType = 0x1002, kOptLimit = 0x1003, kOptPopulation = 0x1004, kOptGold = 0x1005, kOptNoFog = 0x1007, kOptExplored = 0x1008,
		  kOptSharedSupport = 0x1009, kOptSharedControl = 0x100a, kOptNoBonuses = 0x10a7;
const int kLockStep = -1;
// One change of each option at most this often: the room is read again in between, and a check toggled twice goes back.
const DWORD kStepEveryMs = 2000;

struct Applied {
	uint64_t room = 0;
	bool have = false;
	LeagueRoomOptions want;
	std::string logged;
	std::map<int, DWORD> setAt;
	// Changes sent that the room did not take (a disabled control: the game type of a preset map, shared control without teams).
	std::map<int, int> misses;
	std::set<int> warned;
};
// After this many, the option is left as the room has it, or the start would wait for ever.
const int kMaxMisses = 3;
std::mutex g_mx;
Applied g_applied;

struct Step {
	int id = 0;
	bool checkbox = false;
	int index = 0;
	bool value = false;
};

unsigned RulesOf(const LeagueRoomOptions &o, unsigned roomRules)
{
	unsigned r = (o.lockDiplomacy ? kRuleLockDiplomacy : 0u) | (o.hideCivs ? kRuleBlindPick : 0u) | (o.freeAttach ? kRuleFreeAttach : 0u) |
		(o.cheats ? kRuleCheats : 0u) | (o.timers ? 0u : kRuleNoTimers);
	unsigned content = o.contentDefaults ? ModOptionsDefaults() : roomRules & ModOptionsMask();
	for (const ModOption &m : ModOptions()) {
		if (m.id == kSeasonOptionId) content = ModOptionSet(m, content, o.climate);
	}
	return (r | content) & kRuleMask;
}

const GameRoomOption *FindOption(const GameRoomSnapshot &g, int id)
{
	for (const GameRoomOption &o : g.options) {
		if (o.id == id) return &o;
	}
	return nullptr;
}

void WarnOnce(int id, const char *what)
{
	if (!g_applied.warned.insert(id).second) return;
	LogW("league", "the room's option 0x%x is left as it is: %s", id, what);
}

// The game's key of a choice ("5000@gold") as this player's game shows it, and its English words.
std::vector<std::string> KeyWords(const std::string &key)
{
	const size_t at = key.find('@');
	return {LeagueRoomItemText(LangGameTextShown(key)), LeagueRoomItemText(key.substr(0, at))};
}

// What the room still needs, in order: the game type first, since a content's mode fixes some of the others.
std::vector<Step> Needed(const LeagueRoomOptions &want, const GameRoomSnapshot &g, bool log)
{
	std::vector<Step> out;
	auto need = [&](const Step &s, bool wrong) {
		if (!wrong) {
			g_applied.misses.erase(s.id);
			return;
		}
		const auto it = g_applied.misses.find(s.id);
		if (it != g_applied.misses.end() && it->second >= kMaxMisses) {
			if (log) WarnOnce(s.id, "the room does not take the change");
			return;
		}
		out.push_back(s);
	};
	const GameMode *mode = GameModeFor(g.mode.c_str());
	auto fixedByMode = [&](int id) {
		if (!GameModeLocksOption(mode, id)) return false;
		if (log) WarnOnce(id, "the room's mode fixes it");
		return true;
	};
	auto combo = [&](int id, int index) {
		const GameRoomOption *o = FindOption(g, id);
		if (!o || o->checkbox || o->items.empty() || fixedByMode(id)) return;
		if (index < 0 || index >= static_cast<int>(o->items.size())) {
			if (log) WarnOnce(id, "the room does not have the league's choice");
			return;
		}
		need({id, false, index, false}, o->sel != index);
	};
	auto keyed = [&](int id, const std::string &key) {
		const GameRoomOption *o = FindOption(g, id);
		if (!o || o->checkbox || key.empty() || fixedByMode(id)) return;
		const int index = LeagueRoomItemFor(o->items, o->itemDisabled, KeyWords(key));
		if (index < 0) {
			if (log) WarnOnce(id, ("the room has no \"" + key + "\"").c_str());
			return;
		}
		need({id, false, index, false}, o->sel != index);
	};
	auto check = [&](int id, bool value) {
		const GameRoomOption *o = FindOption(g, id);
		if (!o || !o->checkbox || fixedByMode(id)) return;
		need({id, true, 0, value}, o->value != value);
	};
	combo(kOptGameType, want.gameType);
	if (!out.empty()) return out;
	combo(kOptLimit, want.limit);
	keyed(kOptGold, want.goldKey);
	keyed(kOptPopulation, want.populationKey);
	check(kOptNoBonuses, !want.bonuses);
	check(kOptNoFog, !want.fog);
	check(kOptExplored, want.explored);
	check(kOptSharedSupport, want.sharedSupport);
	check(kOptSharedControl, want.sharedControl);
	const unsigned sites = want.allSites ? 0u : kSiteMask;
	need({kLockStep, false, 0, false}, !g.optionsLocked || (g.rules & kRuleMask) != RulesOf(want, g.rules) ||
			(g.randomMap && (g.siteFlags & kSiteMask) != sites) || g.gameSpeed != want.gameSpeed);
	return out;
}

bool Due(int id)
{
	const DWORD now = GetTickCount();
	auto it = g_applied.setAt.find(id);
	if (it != g_applied.setAt.end() && now - it->second < kStepEveryMs) return false;
	g_applied.setAt[id] = now;
	return true;
}

void Send(const Step &s, const LeagueRoomOptions &want, GameRoomSnapshot &g)
{
	if (s.id == kLockStep) {
		GameRoomLockOptions(RulesOf(want, g.rules), want.allSites ? 0u : kSiteMask);
		return;
	}
	for (GameRoomOption &o : g.options) {
		if (o.id != s.id) continue;
		if (s.checkbox) {
			o.value = s.value;
			Predict(kPredOptVal, s.id, s.value);
			GameRoomToggleOption(s.id);
		}
		else {
			o.sel = s.index;
			Predict(kPredOptSel, s.id, s.index);
			GameRoomSetOption(s.id, s.index);
		}
		return;
	}
}

}

bool LeagueRoomApply(const LeagueRoomOptions &want, GameRoomSnapshot &g, uint64_t room, const char *what)
{
	if (!g.active) return false;
	GameRoomKeepPlayerPrefs();
	if (!g.host) return true;
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_applied.room != room) {
		g_applied = Applied();
		g_applied.room = room;
	}
	g_applied.want = want;
	g_applied.have = true;
	const std::string text = LeagueRoomOptionsText(want);
	if (g_applied.logged != text) {
		g_applied.logged = text;
		LogI("league", "%s room: every option is set to the league's: %s; rules 0x%x", what, text.c_str(), RulesOf(want, g.rules));
	}
	if (g.busy) return false;
	const std::vector<Step> steps = Needed(want, g, true);
	for (const Step &s : steps) {
		if (!Due(s.id)) continue;
		g_applied.misses[s.id]++;
		Send(s, want, g);
	}
	return steps.empty();
}

bool LeagueRoomHeld(const GameRoomSnapshot &g, uint64_t room)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_applied.have || g_applied.room != room || !g.active || !g.host) return true;
	return Needed(g_applied.want, g, false).empty();
}

}
