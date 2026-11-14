// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "core/leaguemaps.h"
#include "core/log.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "game/gameroom.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

const int kOptMapType = 0x1000, kOptMapSize = 0x01000008, kOptPopulation = 0x1004, kOptGold = 0x1005, kOptNoFog = 0x1007, kOptExplored = 0x1008,
		  kOptNoBonuses = 0x10a7;

// Lower-case letters and digits, the accents of Spanish and Italian taken off: the room's items in the game's language.
std::string Plain(const std::string &s)
{
	static const std::pair<const char *, char> kAccents[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}, {"\xC3\xB3", 'o'}, {"\xC3\xBA", 'u'},
		{"\xC3\xB1", 'n'}, {"\xC3\xA0", 'a'}, {"\xC3\xA8", 'e'}, {"\xC3\xAC", 'i'}, {"\xC3\xB2", 'o'}, {"\xC3\xB9", 'u'}};
	std::string out;
	for (size_t i = 0; i < s.size();) {
		bool done = false;
		for (const auto &a : kAccents) {
			if (s.compare(i, 2, a.first) == 0) {
				out += a.second;
				i += 2;
				done = true;
				break;
			}
		}
		if (done) continue;
		const unsigned char c = static_cast<unsigned char>(s[i++]);
		if (isalnum(c)) out += static_cast<char>(tolower(c));
		else if (!out.empty() && out.back() != ' ') out += ' ';
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

// "scenarios/torneo_1vs1_egypt": a scenario's path as the room's selector and the room name it, compared.
std::string MapKey(std::string p)
{
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

std::string FileOfKey(const std::string &key)
{
	const size_t slash = key.find_last_of('/');
	return slash == std::string::npos ? key : key.substr(slash + 1);
}

GameRoomOption *FindOption(GameRoomSnapshot &g, int id)
{
	for (GameRoomOption &o : g.options) {
		if (o.id == id) return &o;
	}
	return nullptr;
}

// Each of these is true once the room holds what the tournament wants, or the game has nothing to set it with.
bool SetCombo(GameRoomSnapshot &g, int id, const std::vector<std::string> &words)
{
	GameRoomOption *o = FindOption(g, id);
	if (!o || o->checkbox) return true;
	int want = -1;
	for (size_t k = 0; k < o->items.size() && want < 0; k++) {
		if (k < o->itemDisabled.size() && o->itemDisabled[k]) continue;
		const std::string item = Plain(o->items[k]);
		for (const std::string &w : words) {
			if (item == w) want = static_cast<int>(k);
		}
	}
	if (want < 0 || o->sel == want) return true;
	o->sel = want;
	Predict(kPredOptSel, id, want);
	GameRoomSetOption(id, want);
	return false;
}

bool SetCheck(GameRoomSnapshot &g, int id, bool value)
{
	GameRoomOption *o = FindOption(g, id);
	if (!o || !o->checkbox || o->value == value) return true;
	o->value = value;
	Predict(kPredOptVal, id, value);
	GameRoomToggleOption(id);
	return false;
}

// A random map's type or size, by the game's id behind each item; a size waits for its type, which decides the sizes the game offers.
bool SetMapItem(GameRoomSnapshot &g, int id, const std::string &want, bool typeDone, RoomLock &k)
{
	GameRoomOption *o = FindOption(g, id);
	if (want.empty() || !o) return true;
	bool byText = false;
	const int i = LeagueMapItemFor(o->keys, o->items, o->itemDisabled, want, byText);
	if (i < 0) {
		if (typeDone || id != kOptMapSize) {
			k.mapAllowed = true;
			LogW("league", "the tournament's map \"%s\" is not in the room's list (0x%x)", want.c_str(), id);
			return true;
		}
		return false;
	}
	if (byText) LogW("league", "no map id behind the room's items; \"%s\" picked by its name (\"%s\")", want.c_str(), o->items[i].c_str());
	if (o->sel == i) return true;
	o->sel = i;
	Predict(kPredOptSel, id, i);
	GameRoomSetOption(id, i);
	return false;
}

// A scenario, through the room's own selector as a player picks one: the first item of the map type opens it, then the scenario is
// selected and accepted.
bool PickScenario(GameRoomSnapshot &g, const TourneyMap &m, RoomLock &k)
{
	const std::string want = MapKey(CupScenarioPath(m.file));
	if (!g.picker && !g.scenario.empty() && MapKey(g.scenario) == want) {
		k.pick = -1;
		return true;
	}
	if (g.picker) {
		if (k.pick >= 0) {
			GameRoomPickerAccept();
			k.pick = -1;
			return false;
		}
		int idx = -1;
		for (size_t i = 0; i < g.pickerPaths.size() && idx < 0; i++) {
			if (MapKey(g.pickerPaths[i]) == want) idx = static_cast<int>(i);
		}
		for (size_t i = 0; i < g.pickerPaths.size() && idx < 0; i++) {
			if (FileOfKey(MapKey(g.pickerPaths[i])) == FileOfKey(want)) idx = static_cast<int>(i);
		}
		if (idx < 0) {
			GameRoomPickerCancel();
			k.mapAllowed = true;
			LogW("league", "the tournament's scenario %s is not in the room's selector (%u scenarios); the host picks the map", m.file.c_str(),
				static_cast<unsigned>(g.pickerPaths.size()));
			return true;
		}
		LogI("league", "the tournament's scenario %s is chosen in the room's selector", m.file.c_str());
		GameRoomPickerSelect(idx);
		k.pick = idx;
		return false;
	}
	const GameRoomOption *type = FindOption(g, kOptMapType);
	if (!type) {
		k.mapAllowed = true;
		return true;
	}
	GameRoomSetOption(kOptMapType, type->sel == 0 ? 1 : 0);
	return false;
}

}

bool ApplyTourneyRoom(const TourneyNext &next, RoomLock &k)
{
	GameRoomSnapshot &g = g_s.game;
	const TourneyRoomRules r = TourneyRulesOf(next.settings);
	bool done = true;
	// The room's own rules off (cheats, free attachment), the tournament's diplomacy and hidden civilizations on; the content's options kept.
	const unsigned rules = (g.rules & ~kRuleBaseMask) | (r.lockedDiplomacy ? kRuleLockDiplomacy : 0u) | (r.hideCivs ? kRuleBlindPick : 0u);
	if (rules != g.rules) {
		g.rules = rules;
		GameRoomSetRules(rules);
		done = false;
	}
	// Every preset keeps the ruins, caves, item holders and strongholds.
	if (g.siteFlags & kSiteMask) {
		g.siteFlags &= ~kSiteMask;
		GameRoomSetSiteFlags(g.siteFlags);
		done = false;
	}
	switch (r.preset) {
	case TourneyPreset::Classic:
		done = SetCombo(g, kOptGold, {"por defecto", "default", "predefinito", "predefinita"}) && done;
		done = SetCombo(g, kOptPopulation, {"normal", "normale"}) && done;
		done = SetCheck(g, kOptNoBonuses, false) && done;
		break;
	case TourneyPreset::Various:
		done = SetCombo(g, kOptGold, {"10000"}) && done;
		done = SetCombo(g, kOptPopulation, {"alta", "high", "elevata"}) && done;
		done = SetCheck(g, kOptNoBonuses, false) && done;
		break;
	default:
		done = SetCombo(g, kOptGold, {"5000"}) && done;
		done = SetCombo(g, kOptPopulation, {"normal", "normale"}) && done;
		done = SetCheck(g, kOptNoBonuses, true) && done;
		break;
	}
	done = SetCheck(g, kOptNoFog, !r.fog) && done;
	done = SetCheck(g, kOptExplored, r.explored) && done;
	if (next.map.Scenario()) done = (k.mapAllowed || PickScenario(g, next.map, k)) && done;
	else if (next.map.Known() && !k.mapAllowed) {
		const bool type = SetMapItem(g, kOptMapType, next.map.name, done, k);
		done = SetMapItem(g, kOptMapSize, next.map.size, type, k) && type && done;
	}
	return done;
}

}
