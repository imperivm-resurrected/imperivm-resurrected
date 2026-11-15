// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"

// What the league's seasons leave behind, as the mod reads it: the list of seasons, a season's hall of fame, a player's card, and the
// texts and rules of the rewards (the vexillum, the avatar frame and the title). Nothing here talks to the network or draws.

// One season of GET /v1/league/seasons.
struct LeagueSeasonInfo {
	int id = 0;
	std::string name;
	int64_t startsAt = 0, endsAt = 0;
	bool closed = false, beta = false;
};
// {seasons: [...]}, newest first as the server sends them.
bool LeagueParseSeasons(const Json &j, std::vector<LeagueSeasonInfo> &out);
const LeagueSeasonInfo *LeagueSeasonById(const std::vector<LeagueSeasonInfo> &seasons, int id);

// GET /v1/league/seasons/:id/hall: the best ten of a season among those with the elite's games.
struct LeagueHallRow {
	int place = 0;
	std::string steamId, name;
	LeagueTier tier = LeagueTier::None;
	int division = 0, glory = 0, games = 0;
};
struct LeagueHall {
	int season = 0;
	std::string name;
	std::vector<LeagueHallRow> top;
};
bool LeagueParseHall(const Json &j, LeagueHall &out);

// GET /v1/league/players/:steamid: a player's season, as the card of the full leaderboard shows it.
struct LeagueCivShare {
	std::string civ;
	int games = 0, wins = 0;
};
struct LeaguePlayerProfile {
	std::string steamId, name;
	int season = 0;
	LeagueTier tier = LeagueTier::None;
	int division = 0, glory = 0, place = 0;
	bool elite = false;
	LeaguePeak peak;
	int games = 0, wins = 0, losses = 0, streak = 0;
	uint32_t avgDurationMs = 0;
	// The three civilizations played most, and the last ten games from the player's side.
	std::vector<LeagueCivShare> civs;
	std::vector<LeagueHistoryGame> recent;
	LeagueFrame frame, title;
};
bool LeagueParsePlayerProfile(const Json &j, LeaguePlayerProfile &out);

// "I", "IV", "XII"; the test season is "0".
std::string LeagueRoman(int n);
// The plate of a season's frame, "T · I": the initial of the rank's name (already translated) and the season.
std::string LeagueFramePlate(const std::string &tierName, int season);
// The title a season gives, in the game's language: "Tribune of Trial of Arms", or "Tribune of Season I" when the season has no name.
std::string LeagueSeasonTitle(LeagueTier tier, bool founder, const std::string &seasonName, int season);
// "W3" for three wins in a row, "L2" for two losses ("V3" and "D2" in Spanish); a dash without a streak.
std::string LeagueStreakText(int streak);

// The games a season asks for its vexillum, and for the frame and the title as well.
constexpr int kLeagueVexillumGames = 10, kLeagueFrameGames = 25;

// What the season being played would leave the player if it closed now.
struct LeagueRewardPreview {
	int games = 0;
	// The games the next reward asks for: 10 for the vexillum, then 25 for the frame and the title; 0 once everything is won.
	int goal = kLeagueVexillumGames;
	bool vexillum = false, frame = false;
	// The test season gives the Founder's vexillum in place of the rank's.
	bool founder = false;
	// Still in the placement games: there is no rank to reward yet.
	bool placing = false;
	// The rank of the rewards: the highest reached, but the elite only by the place held at the end.
	LeagueTier tier = LeagueTier::None;
	// The rank after it and the Glory still missing for it (0 for the elite, which goes by place); None at the top.
	LeagueTier nextTier = LeagueTier::None;
	int gloryToNext = 0;
};
LeagueRewardPreview LeagueRewardsToday(const LeagueRanked &rk, bool testSeason);

// The player's reward of a season, or null.
const LeagueReward *LeagueRewardOf(const LeagueMe &me, int season);
// The rewards that gave a frame and a title, newest first.
std::vector<LeagueReward> LeagueTitleRewards(const LeagueMe &me);
// The frame a reward gave, for drawing.
LeagueFrame LeagueFrameOfReward(const LeagueReward &r);
