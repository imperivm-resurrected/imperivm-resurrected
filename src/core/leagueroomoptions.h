// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "core/leagueranked.h"
#include "core/tournament.h"

// The complete set of options a ranked or cup room is played with. The game keeps the host's last room (its profile's player.ini and
// the mod's imperivm-mod.ini), so every option the room carries is set here, not only the ones the league names: a ranked game after a
// normal game in winter must not be played in winter.

enum class LeagueRoomPreset { Competitive, Classic, Various };

// What the league asks for: the ranked season's or the cup's settings. -1 or empty when it does not say.
struct LeagueRoomRequest {
	std::string preset;
	int fog = -1, explored = -1, hideCivs = -1, cheats = -1;
	// The respawn timers of the big map: 0 forbidden, 1 allowed.
	int timers = -1;
	// "locked", "free" (or "open").
	std::string diplomacy;
	// The time of year of a random map: 0 spring, 1 autumn, 2 winter. A scenario keeps its own.
	int climate = -1;
};
LeagueRoomRequest LeagueRoomRequestOf(const LeagueRoomSettings &s);
LeagueRoomRequest LeagueRoomRequestOf(const TourneySettings &s);

struct LeagueRoomOptions {
	LeagueRoomPreset preset = LeagueRoomPreset::Competitive;
	// The game's keys of the starting gold (0x1005) and population (0x1004) choices (game.loc.xml), shown in each player's language.
	std::string goldKey, populationKey;
	// 0x1002, as the room lists it: its first mode, elimination (the content's Alternative under that name, when it has one).
	int gameType = 0;
	// 0x1003, the first item; elimination has none.
	int limit = 0;
	// 0x10a7 off when true; 0x1007 off when true; 0x1008.
	bool bonuses = false, fog = true, explored = false;
	// 0x1009 (the mod has it always on) and 0x100a.
	bool sharedSupport = true, sharedControl = false;
	// The mod's rules.
	bool lockDiplomacy = true, hideCivs = false, freeAttach = false, cheats = false, timers = true;
	// Every neutral site of a random map (ruins, caves, item holders, strongholds) is kept.
	bool allSites = true;
	int climate = 0;
	// The content's own room options at their defaults.
	bool contentDefaults = true;
	// The game's speed in the room's options (+0x5c): -1 is the normal speed of GamePlay/NormalSpeed.
	int gameSpeed = -1;
};

LeagueRoomPreset LeagueRoomPresetOf(const std::string &id);
const char *LeagueRoomPresetId(LeagueRoomPreset p);
// The preset's options with the request's switches laid over them; unknown presets are the competitive one.
LeagueRoomOptions LeagueRoomOptionsFor(const LeagueRoomRequest &r);
// One line with every option, for the log.
std::string LeagueRoomOptionsText(const LeagueRoomOptions &o);
const char *LeagueRoomClimateName(int climate);

// A room's item as it is compared: lower-case letters and digits, the accents of Spanish and Italian taken off, anything else one space.
std::string LeagueRoomItemText(const std::string &s);
// The first item that is not disabled and reads as one of `wanted` (already through LeagueRoomItemText); -1 when none does.
int LeagueRoomItemFor(const std::vector<std::string> &items, const std::vector<char> &disabled, const std::vector<std::string> &wanted);
