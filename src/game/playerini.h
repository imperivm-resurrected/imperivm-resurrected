// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A game the profile recorded: a [gameN] section of profiles/<dir>/player.ini. The game writes one at the end of each
// skirmish and multiplayer game, never for adventures, Great Battles or Conquest.
struct PlayedGame {
	int index = 0;
	int year = 0, month = 0, day = 0, hour = 0, minute = 0;
	uint32_t durationMs = 0;
	bool multi = false, lost = false;
	int race = -1;
	uint32_t gold = 0, food = 0;
	uint32_t unitsProduced = 0, unitsKilled = 0, unitsLost = 0, unitsMax = 0;
	uint32_t levelMax = 0;
	std::string levelMaxUnit;
	uint32_t damageTaken = 0, damageInflicted = 0, killHealths = 0, dieHealths = 0;
};

struct PlayerIni {
	std::string name;
	int race = -1;
	int games = 0;
	std::string favoriteUnit;
	int favoriteCount = 0;
	std::vector<PlayedGame> played;
};

// Read only: the game signs the file with a hash and must stay the only one that writes it.
PlayerIni ParsePlayerIni(const std::string &text);

// RankPoints%d and RankName%d of the [Ranks] section of the game's constants, in their order, up to the first one without points.
struct RankStep {
	int points = 0;
	std::string name;
};

struct ProfileSummary {
	int games = 0, singleGames = 0, singleWins = 0, multiGames = 0, multiWins = 0;
	uint64_t playedMs = 0;
	uint64_t kills = 0;
	// The military rating is the mean over the games of the score the game gives each one; the rank follows it.
	int64_t rating = 0;
	int rank = -1, nextRank = -1;
	int rankPoints = 0, nextPoints = 0;
	int favoriteRace = -1, favoriteRacePercent = 0;
	uint32_t bestLevel = 0;
	std::string bestUnit;
	uint32_t largestArmy = 0;
};

// The same counts as CProfileStats::ComputeFromIni, which builds the game's own summary, so the rank matches the one the game shows.
ProfileSummary SummarizeProfile(const PlayerIni &ini, const std::vector<RankStep> &ranks);
uint32_t GameRating(const PlayedGame &g);
int PercentOf(int part, int whole);

// Newest first, by the date of each game and then by its order in the file.
std::vector<PlayedGame> RecentGames(const PlayerIni &ini, size_t count);
