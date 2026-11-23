// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "core/leaguemaps.h"
#include "core/leagueroomoptions.h"
#include "core/log.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "game/gameroom.h"
#include "ui/modui.h"
#include "ui/mpui_league_room.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

const int kOptMapType = 0x1000, kOptMapSize = 0x01000008;

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
		if (idx < 0 && k.offered != want) {
			GameRoomPickerOffer(CupScenarioPath(m.file), false);
			k.offered = want;
			return false;
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

bool ApplyTourneyTeam(RoomLock &k)
{
	GameRoomSnapshot &g = g_s.game;
	const int slot = g.localSlot;
	if (k.team < 1 || k.team >= g.teamChoices || slot < 0 || slot > 7 || !g.slots[slot].used) return false;
	GameRoomSlot &s = g.slots[slot];
	if (s.team == k.team) return true;
	// The game keeps a ready player's row as it is: the player is made not ready, put in the team, and gets ready again.
	if (LocalReady(g)) {
		if (k.teamUnready) return false;
		k.teamUnready = true;
		s.ready = false;
		Predict(kPredReady, slot, 0);
		GameRoomToggleReady();
		ModUiToast("The tournament puts you in your team: get ready again.");
		return false;
	}
	s.team = k.team;
	Predict(kPredTeam, slot, k.team);
	GameRoomSetTeam(slot, k.team);
	return false;
}

bool ApplyTourneyRoom(const TourneyNext &next, RoomLock &k)
{
	GameRoomSnapshot &g = g_s.game;
	// Every option but the map: the cup's preset and switches, and the league's defaults for the rest (core/leagueroomoptions.h).
	bool done = LeagueRoomApply(LeagueRoomOptionsFor(LeagueRoomRequestOf(next.settings)), g, g_s.room.id, "cup");
	const TourneyMap map = TourneyGameMap(next);
	if (map.Scenario()) done = (k.mapAllowed || PickScenario(g, map, k)) && done;
	else if (map.Known() && !k.mapAllowed) {
		const bool type = SetMapItem(g, kOptMapType, map.name, done, k);
		done = SetMapItem(g, kOptMapSize, map.size, type, k) && type && done;
	}
	return done;
}

}
