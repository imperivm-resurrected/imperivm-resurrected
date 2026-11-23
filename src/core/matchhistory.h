// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// What the history of a player's games needs of each one, as the game records it at the end of a skirmish or multiplayer game.
struct MatchFacts {
	int year = 0, month = 0, day = 0;
	uint32_t durationMs = 0;
	bool multi = false, lost = false;
	uint32_t kills = 0, losses = 0, armyMax = 0, gold = 0;
	// A league (ranked) game or a cup game, as this computer noted when it started (MatchLeagueOf); the game does not record it.
	int league = 0;
	// The civilization played (-1 unknown, 10 or higher from a mod) and the rest of what the game records.
	int race = -1;
	uint32_t food = 0, produced = 0, damageDealt = 0, damageTaken = 0, bestLevel = 0;
	// What this computer noted of the game itself (MatchNote), which the game does not record: the gold spent, and the kills and
	// deaths a script counted. -1 when there is no note, or no such count in it.
	int64_t goldSpent = -1, scriptKills = -1, scriptDeaths = -1;
};

enum MatchLeague { kMatchOpen, kMatchLeague, kMatchCup };
// A league or cup game this computer started: when (seconds since 1970) and which (MatchLeague).
struct MatchTag {
	int64_t startSec = 0;
	int league = kMatchOpen;
};
// Which of the noted games a recorded one is: the game records its time to the minute, at its start or its end, so a note that
// started from two minutes before that time to two minutes past its length tells it; the latest such note when several do.
int MatchLeagueOf(const std::vector<MatchTag> &tags, int64_t recordedSec, uint32_t durationMs);

// What this computer notes of a game when the local player wins or loses it, for the profile: when it ran (seconds since 1970, from
// this computer's clock) and the figures the game does not keep in its record of the game.
struct MatchNote {
	int64_t startSec = 0, endSec = 0;
	int64_t goldSpent = -1, scriptKills = -1, scriptDeaths = -1;
};
// One line of the notes file: "<start> <end> <gold spent> <script kills> <script deaths>", -1 for what is not known.
std::string MatchNoteLine(const MatchNote &n);
bool MatchNoteParse(const std::string &line, MatchNote &out);
// The note of a recorded game, or null. The game stamps its record with the time of the start or of the end of the game, to the
// minute: the note of a game that ran from two minutes before that time to two minutes after it, the one that started last when
// several did.
const MatchNote *MatchNoteOf(const std::vector<MatchNote> &notes, int64_t recordedSec);
void MatchApplyNote(MatchFacts &m, const MatchNote &n);

enum MatchResult { kMatchWon, kMatchLost, kMatchAbandoned };

// A game that never really ran: shorter than kMatchAbandonedMs with no unit killed and none lost. It may be recorded as won
// or lost; it is kept, only told apart.
constexpr uint32_t kMatchAbandonedMs = 3 * 60 * 1000;
MatchResult MatchResultOf(const MatchFacts &m);

enum MatchMetric { kMetricKills, kMetricLosses, kMetricKillsPerLoss, kMetricArmy, kMetricGold, kMetricDuration, kMetricCount };
// Kills per loss counts a game without losses as one loss; the duration is in minutes.
double MatchMetricValue(const MatchFacts &m, MatchMetric metric);

// League and cup games are multiplayer games too: "Multiplayer" keeps them.
enum MatchMode { kModeAll, kModeSkirmish, kModeMultiplayer, kModeLeague, kModeCup };
enum MatchOutcome { kOutcomeAll, kOutcomeVictories, kOutcomeDefeats };
// The positions in `games` (newest first) that pass both filters, newest first, at most `limit` of them (0: all). Victories
// and defeats leave the abandoned games out; "all" keeps them.
std::vector<size_t> MatchSelect(const std::vector<MatchFacts> &games, MatchMode mode, MatchOutcome outcome, size_t limit);

// The value at a fraction of the way up the sorted values, taking the rank below: never a blend with a huge value above it.
// Zero without values.
double MatchPercentile(std::vector<double> values, double fraction);
double MatchAverage(const std::vector<double> &values);
// The top of a chart of the values: the highest value, so every bar fits whole. Never below `floor`.
double MatchChartTop(const std::vector<double> &values, double floor);
// The average of a metric over the games that really ran.
double MatchMetricAverage(const std::vector<MatchFacts> &games, MatchMetric metric);

// The kills and deaths of a game for its kill ratio: a script's count when the game has both, the game's own otherwise.
uint32_t MatchKillsOf(const MatchFacts &m);
uint32_t MatchDeathsOf(const MatchFacts &m);
// Kills per death over many games: the totals divided, no deaths counting as one.
double MatchKillRatio(uint64_t kills, uint64_t deaths);

constexpr int kCivAll = -2;
// A player's games with one civilization, or with all of them (kCivAll). The games and the wins count every recorded game, as the
// profile's summary does. The averages count the games that really ran, and those of the noted figures only the games noted.
struct MatchCivRecord {
	int race = kCivAll;
	int games = 0, wins = 0, ran = 0, winPercent = 0;
	uint64_t kills = 0, deaths = 0;
	double killRatio = 0;
	// Averages over the games that ran.
	double kills1 = 0, deaths1 = 0, damageDealt = 0, damageTaken = 0, gold = 0, food = 0, produced = 0, army = 0, minutes = 0;
	int goldSpentGames = 0, scriptGames = 0;
	double goldSpent = 0;
	uint32_t bestLevel = 0;
	// The position in the games of the one with the best level, or -1.
	int bestGame = -1;
};
MatchCivRecord MatchCivRecordOf(const std::vector<MatchFacts> &games, int race);
// The civilizations played, most games first and then by number, an unknown one (-1) last.
std::vector<int> MatchCivsPlayed(const std::vector<MatchFacts> &games);

enum MatchAgeUnit { kAgeToday, kAgeYesterday, kAgeDays, kAgeWeeks, kAgeMonths, kAgeYears };
struct MatchAge {
	MatchAgeUnit unit = kAgeToday;
	int count = 0;
};
// How long ago a date was, in calendar days from today: days up to a week, then weeks, months (from 30 days) and years (from 365).
MatchAge MatchAgeOf(int year, int month, int day, int todayYear, int todayMonth, int todayDay);
// Days since 1970-01-01 of a calendar date.
int64_t MatchDayNumber(int year, int month, int day);
