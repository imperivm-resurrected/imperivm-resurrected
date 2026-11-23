// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/matchhistory.h"

// GET /v1/league/players/:steamid/profile: a player's statistics as the league keeps them, of their league and cup games only. The
// figures are sums: the stat figures over `statsGames` (the games whose report had them), the rest over `games`.
struct LeagueProfileSums {
	int games = 0, wins = 0, losses = 0, statsGames = 0;
	uint64_t kills = 0, deaths = 0, damageDealt = 0, damageTaken = 0, gold = 0, units = 0, army = 0;
	double minutes = 0;
};

struct LeagueProfileCiv {
	std::string civ;
	LeagueProfileSums sums;
};

struct LeagueProfileGame {
	// ms since 1970.
	int64_t at = 0, matchId = 0;
	bool cup = false, won = false;
	// "1v1" or "2v2" for a ranked game; the cup's name for a cup game.
	std::string queue, cupName, civ, map;
	uint32_t durationMs = 0, kills = 0, deaths = 0;
	// SteamID and name of each opponent.
	std::vector<std::pair<std::string, std::string>> opponents;
};

struct LeagueProfileTrophy {
	LeagueMetal metal = LeagueMetal::Gold;
	std::string title;
	int season = 0;
	int64_t awardedAt = 0;
};

struct LeagueProfile {
	std::string steamId, name, avatarUrl;
	// The patron's tier as the server names it, and the place in the honour roll; empty for none.
	std::string patronTier;
	int patronPlace = 0;
	LeagueBadge badge;
	LeagueFrame frame, title;
	// The season's standing in each queue (present false for null), with the Glory after each game once placed.
	LeagueRanked ranked, ranked2v2;
	LeagueProfileSums totals;
	// The most games first.
	std::vector<LeagueProfileCiv> civs;
	// Newest first.
	std::vector<LeagueProfileGame> recent;
	std::vector<LeagueProfileTrophy> trophies;
	std::vector<LeagueCupTitle> cupTitles;
};
bool LeagueParseProfile(const Json &j, LeagueProfile &out);
std::string LeagueProfilePath(const std::string &encodedSteamId, const std::string &lang);

// What the profile's civilizations tab shows of the sums, as it shows a player's own games: the averages of the stat figures over the
// games with figures (`ran`), the duration over every game. `race` is the key the screen gives the civilization.
MatchCivRecord LeagueProfileRecord(const LeagueProfileSums &s, int race);
