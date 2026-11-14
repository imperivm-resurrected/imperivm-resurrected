// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"

// The ranked 1v1 queue of the league server, as the mod reads it: the season, the queue, the match ticket, the ladder and what a
// report gave, plus the maths of the screens (the Glory bar, the countdown and the result's animation). Nothing here talks to the
// network or draws.

// The room options the server fixes for every ranked match; -1 (or empty) where the season says nothing.
struct LeagueRoomSettings {
	std::string preset;
	int hideCivs = -1, fog = -1, explored = -1, cheats = -1;
	std::string diplomacy;
	// Seconds of delay for spectators; 0 without them, -1 when not said.
	int specDelay = -1;
	int pauseCount = -1, pauseSeconds = -1;
};
LeagueRoomSettings LeagueParseRoomSettings(const Json *v);

// GET /v1/league/season.
struct LeagueSeason {
	int number = 0;
	std::string name, theme, contentKey;
	// The season's name in every language the server has ("es", "en", "it").
	std::map<std::string, std::string> names;
	int64_t startsAt = 0, endsAt = 0;
	bool beta = false, active = false;
	std::vector<LeagueMapFacts> maps;
	LeagueRoomSettings settings;
};
bool LeagueParseSeason(const Json &j, LeagueSeason &out);
// The name in `lang` ("es", "en" or "it"), the one the server chose otherwise.
std::string LeagueSeasonName(const LeagueSeason &s, const std::string &lang);
// Whole days left until the end, rounded up; 0 once it is over.
int LeagueDaysLeft(int64_t endsAt, int64_t nowMs);
// How much of the season has gone, 0 to 1.
float LeagueSeasonElapsed(const LeagueSeason &s, int64_t nowMs);
// "es", "en" or "it" from the game's language ("spanish", "Italiano"...): English for anything else.
std::string LeagueLangCode(const std::string &gameLanguage);

// A player as the queue and the ticket show them.
struct LeaguePlayerCard {
	std::string steamId, name;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	bool accepted = false, host = false;
};

enum class LeagueQueueStatus { Unknown, Idle, Searching, Found };

// POST and GET /v1/league/queue.
struct LeagueQueueAnswer {
	LeagueQueueStatus status = LeagueQueueStatus::Unknown;
	int64_t since = 0;
	int searching = 0, estimateSec = 0;
	// Found: the match waiting for answers. Idle: the match being played, if any (a restarted mod finds it so).
	int64_t matchId = 0;
	LeaguePlayerCard opponent;
	LeagueMapFacts map;
	int64_t acceptBy = 0;
	bool accepted = false;
};
bool LeagueParseQueue(const Json &j, LeagueQueueAnswer &out);

enum class LeagueMatchStatus { Unknown, Accepting, Ready, Playing, Done, Cancelled };

// The `ranked` block of a report's answer and of a finished ticket: what one game did to the player's standing.
struct LeagueRankedResult {
	bool present = false;
	int gloryBefore = 0, gloryAfter = 0, delta = 0;
	LeagueTier tierBefore = LeagueTier::None, tierAfter = LeagueTier::None;
	int divisionBefore = 0, divisionAfter = 0;
	bool promoted = false, demoted = false;
	int shield = 0, placementLeft = 0;
};
LeagueRankedResult LeagueParseRankedResult(const Json *v);

// GET /v1/league/matches/:id, the answers to accept and to the room.
struct LeagueMatchTicket {
	int64_t id = 0;
	LeagueMatchStatus status = LeagueMatchStatus::Unknown;
	std::string host, lobby, lobbyKey, contentKey, outcome;
	LeagueMapFacts map;
	LeagueRoomSettings settings;
	int64_t acceptBy = 0;
	std::vector<LeaguePlayerCard> players;
	LeagueRankedResult ranked;
};
bool LeagueParseMatch(const Json &j, LeagueMatchTicket &out);
// The player of the ticket with that SteamID, or the other one; null when there is none.
const LeaguePlayerCard *LeagueTicketPlayer(const LeagueMatchTicket &t, const std::string &steamId, bool other);
// The lobby the server gives, as a number; 0 when there is none yet.
uint64_t LeagueTicketLobby(const LeagueMatchTicket &t);
// "ranked:<id>", what the host's Steam room carries in its "irm" data.
std::string LeagueRankedRoomTag(int64_t matchId);

struct LeagueBoardRow {
	int position = 0;
	std::string steamId, name;
	LeagueTier tier = LeagueTier::None;
	int division = 0, glory = 0, wins = 0, losses = 0;
};

// GET /v1/league/leaderboard.
struct LeagueBoard {
	int season = 0, total = 0, offset = 0;
	// The player's own place with around=me; 0 when not ranked.
	int me = 0;
	std::vector<LeagueBoardRow> rows;
};
bool LeagueParseBoard(const Json &j, LeagueBoard &out);
// The row of a SteamID in the page, or null.
const LeagueBoardRow *LeagueBoardFind(const LeagueBoard &b, const std::string &steamId);
// The offset of the page of `limit` rows that holds `position` (1 = first).
int LeagueBoardPageOf(int position, int limit);
// Rounded win rate in percent; 0 without games.
int LeagueWinRate(int wins, int losses);

// POST /v1/league/matches/report.
struct LeagueReportAnswer {
	std::string status, outcome;
	LeagueRankedResult ranked;
};
bool LeagueParseReportAnswer(const Json &j, LeagueReportAnswer &out);

// The body of an error: {error, message} and what some errors add.
struct LeagueError {
	std::string code, message, contentKey;
	int64_t until = 0, matchId = 0;
};
LeagueError LeagueParseError(const Json &j);

// Where some Glory leaves a player: their rank and how far into its division (0 to 99). Legate I has no top: `capped` is false there
// and `into` keeps counting.
struct LeagueGloryPlace {
	LeagueTier tier = LeagueTier::Recruit;
	int division = 3, into = 0;
	bool capped = true;
};
LeagueGloryPlace LeagueGloryPlaceOf(int glory);
// The next division up from a tier and division ("Centurion II" -> "Centurion I", "Legionary I" -> "Signifer III"); None at the top.
void LeagueNextDivision(LeagueTier tier, int division, LeagueTier &nextTier, int &nextDivision);
// About how many wins are still needed to reach the next division, at a typical 20 Glory a win; at least 1.
int LeagueWinsToNext(int glory);

// How much of the 20 s window to accept is left, 1 to 0.
constexpr int64_t kLeagueAcceptWindowMs = 20000;
float LeagueCountdownFraction(int64_t acceptBy, int64_t nowMs, int64_t windowMs = kLeagueAcceptWindowMs);
// Whole seconds left, rounded up; never negative.
int LeagueCountdownSeconds(int64_t acceptBy, int64_t nowMs);
// "1:05" or "12:40" for a running time.
std::string LeagueClockText(int64_t ms);

// The badges a player may wear next to their name, in the order the profile lists them: the live rank, every season and trophy of
// theirs, and none. `chosen` marks the one the server has.
struct LeagueBadgeChoice {
	std::string kind, id;
	LeagueBadge look;
	bool chosen = false;
};
std::vector<LeagueBadgeChoice> LeagueBadgeChoices(const LeagueMe &me);

// The result screen's animation (RankedUiAnimacion): the bar, the counter and the emblems at a moment after the screen opened.
enum class LeagueResultKind { Steady, Promotion, Demotion, Placement, PlacementDone };
LeagueResultKind LeagueResultKindOf(const LeagueRankedResult &r);
int LeagueResultDurationMs(LeagueResultKind kind);

struct LeagueResultFrame {
	LeagueResultKind kind = LeagueResultKind::Steady;
	// The rank whose name and emblem are shown now: the old one until the change, the new one after.
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	bool switched = false;
	// The Glory the counter shows.
	int glory = 0;
	// How full the bar is, 0 to 1, and the faint part that is about to be won (or that was lost, `ghostLoss`).
	float bar = 0, ghostFrom = 0, ghostTo = 0;
	bool ghostLoss = false;
	bool deltaShown = false;
	// 0-255: the white flash over the bar, the old emblem fading, the new one coming in and the rays of a new tier.
	int flash = 0, oldAlpha = 255, newAlpha = 0, rays = 0;
	// The new emblem's size, 85 to 100 %.
	int newScale = 100;
	bool shieldShown = false;
	bool done = false;
};
// The frame at `ms` after the screen opened; anything past the end is the end, which is also what skipping shows.
LeagueResultFrame LeagueResultAt(const LeagueRankedResult &r, uint32_t ms);
