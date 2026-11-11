// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// What the history of a player's games needs of each one, as the game records it at the end of a skirmish or multiplayer game.
struct MatchFacts {
	int year = 0, month = 0, day = 0;
	uint32_t durationMs = 0;
	bool multi = false, lost = false;
	uint32_t kills = 0, losses = 0, armyMax = 0, gold = 0;
};

enum MatchResult { kMatchWon, kMatchLost, kMatchAbandoned };

// A game that never really ran: shorter than kMatchAbandonedMs with no unit killed and none lost. It may be recorded as won
// or lost; it is kept, only told apart.
constexpr uint32_t kMatchAbandonedMs = 3 * 60 * 1000;
MatchResult MatchResultOf(const MatchFacts &m);

enum MatchMetric { kMetricKills, kMetricLosses, kMetricKillsPerLoss, kMetricArmy, kMetricGold, kMetricDuration, kMetricCount };
// Kills per loss counts a game without losses as one loss; the duration is in minutes.
double MatchMetricValue(const MatchFacts &m, MatchMetric metric);

enum MatchMode { kModeAll, kModeSkirmish, kModeMultiplayer };
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

enum MatchAgeUnit { kAgeToday, kAgeYesterday, kAgeDays, kAgeWeeks, kAgeMonths, kAgeYears };
struct MatchAge {
	MatchAgeUnit unit = kAgeToday;
	int count = 0;
};
// How long ago a date was, in calendar days from today: days up to a week, then weeks, months (from 30 days) and years (from 365).
MatchAge MatchAgeOf(int year, int month, int day, int todayYear, int todayMonth, int todayDay);
// Days since 1970-01-01 of a calendar date.
int64_t MatchDayNumber(int year, int month, int day);
