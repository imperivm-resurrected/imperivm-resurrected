// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include "core/matchhistory.h"

namespace {

MatchFacts Game(uint32_t minutes, bool multi, bool lost, uint32_t kills, uint32_t losses)
{
	MatchFacts m;
	m.durationMs = minutes * 60000;
	m.multi = multi;
	m.lost = lost;
	m.kills = kills;
	m.losses = losses;
	return m;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

void Leagues()
{
	const int64_t start = 1763650000;
	const std::vector<MatchTag> tags = {{start - 3600, kMatchCup}, {start, kMatchLeague}};
	// Recorded at its start (to the minute) or at its end, 10 minutes later.
	CHECK_EQ(MatchLeagueOf(tags, start - 40, 600000), static_cast<int>(kMatchLeague));
	CHECK_EQ(MatchLeagueOf(tags, start + 600, 600000), static_cast<int>(kMatchLeague));
	CHECK_EQ(MatchLeagueOf(tags, start - 3600 + 1200, 1200000), static_cast<int>(kMatchCup));
	// A game well before or after the notes is an ordinary one.
	CHECK_EQ(MatchLeagueOf(tags, start + 2000, 600000), static_cast<int>(kMatchOpen));
	CHECK_EQ(MatchLeagueOf(tags, start - 7200, 600000), static_cast<int>(kMatchOpen));
	CHECK_EQ(MatchLeagueOf({}, start, 600000), static_cast<int>(kMatchOpen));

	std::vector<MatchFacts> games(4);
	for (MatchFacts &g : games) {
		g.multi = true;
		g.durationMs = 20 * 60000;
		g.kills = 5;
	}
	games[1].league = kMatchLeague;
	games[2].league = kMatchCup;
	games[3].multi = false;
	const std::vector<size_t> league = MatchSelect(games, kModeLeague, kOutcomeAll, 0), cup = MatchSelect(games, kModeCup, kOutcomeAll, 0);
	CHECK_EQ(league.size(), size_t(1));
	CHECK_EQ(league[0], size_t(1));
	CHECK_EQ(cup.size(), size_t(1));
	CHECK_EQ(cup[0], size_t(2));
	CHECK_EQ(MatchSelect(games, kModeMultiplayer, kOutcomeAll, 0).size(), size_t(3));
}

void Results()
{
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(20, false, false, 10, 3))), static_cast<int>(kMatchWon));
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(20, false, true, 10, 3))), static_cast<int>(kMatchLost));
	// Short and with nothing killed or lost: abandoned, whatever the game recorded.
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(0, false, true, 0, 0))), static_cast<int>(kMatchAbandoned));
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(2, true, false, 0, 0))), static_cast<int>(kMatchAbandoned));
	// A short game with a fight in it, or a long quiet one, counts.
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(2, false, true, 0, 1))), static_cast<int>(kMatchLost));
	CHECK_EQ(static_cast<int>(MatchResultOf(Game(3, false, false, 0, 0))), static_cast<int>(kMatchWon));
}

void Metrics()
{
	MatchFacts m = Game(90, false, false, 30, 12);
	m.armyMax = 140;
	m.gold = 5000;
	CHECK(Near(MatchMetricValue(m, kMetricKills), 30));
	CHECK(Near(MatchMetricValue(m, kMetricLosses), 12));
	CHECK(Near(MatchMetricValue(m, kMetricKillsPerLoss), 2.5));
	CHECK(Near(MatchMetricValue(m, kMetricArmy), 140));
	CHECK(Near(MatchMetricValue(m, kMetricGold), 5000));
	CHECK(Near(MatchMetricValue(m, kMetricDuration), 90));
	CHECK(Near(MatchMetricValue(Game(10, false, false, 7, 0), kMetricKillsPerLoss), 7));
}

void Select()
{
	const std::vector<MatchFacts> games = {Game(20, true, false, 5, 1), Game(1, false, true, 0, 0), Game(30, false, true, 2, 9),
		Game(40, true, true, 8, 8), Game(25, false, false, 9, 2)};
	CHECK_EQ(MatchSelect(games, kModeAll, kOutcomeAll, 0).size(), size_t(5));
	CHECK_EQ(MatchSelect(games, kModeAll, kOutcomeAll, 3).size(), size_t(3));
	const std::vector<size_t> multi = MatchSelect(games, kModeMultiplayer, kOutcomeAll, 0);
	CHECK_EQ(multi.size(), size_t(2));
	CHECK_EQ(multi[0], size_t(0));
	CHECK_EQ(multi[1], size_t(3));
	CHECK_EQ(MatchSelect(games, kModeSkirmish, kOutcomeAll, 0).size(), size_t(3));
	const std::vector<size_t> won = MatchSelect(games, kModeAll, kOutcomeVictories, 0);
	CHECK_EQ(won.size(), size_t(2));
	// The abandoned game is in neither victories nor defeats.
	const std::vector<size_t> lost = MatchSelect(games, kModeAll, kOutcomeDefeats, 0);
	CHECK_EQ(lost.size(), size_t(2));
	CHECK_EQ(lost[0], size_t(2));
	CHECK_EQ(MatchSelect(games, kModeSkirmish, kOutcomeDefeats, 0).size(), size_t(1));
	CHECK_EQ(MatchSelect({}, kModeAll, kOutcomeAll, 20).size(), size_t(0));
}

void Scale()
{
	CHECK(Near(MatchPercentile({}, 0.9), 0));
	CHECK(Near(MatchPercentile({5}, 0.9), 5));
	CHECK(Near(MatchPercentile({1, 2, 3, 4, 5}, 0.5), 3));
	CHECK(Near(MatchPercentile({10, 0}, 0.5), 0));
	CHECK(Near(MatchPercentile({10, 0}, 1), 10));
	CHECK(Near(MatchPercentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 0.9), 10));
	CHECK(Near(MatchAverage({}), 0));
	CHECK(Near(MatchAverage({2, 4, 9}), 5));
	// The scale is always the biggest game: no bar is cut, whatever the others are.
	CHECK(Near(MatchChartTop({40, 55, 60, 38, 70, 52, 1630, 45, 61, 49}, 1), 1630));
	CHECK(Near(MatchChartTop({0, 0, 0, 0, 0, 0, 0, 0, 250, 300}, 1), 300));
	// The case seen in the game: small games, one of about 435 kills and one of about 900.
	CHECK(Near(MatchChartTop({3, 0, 2, 435, 0, 1, 5, 900, 0, 4}, 1), 900));
	CHECK(Near(MatchChartTop({40, 55, 60, 38, 70, 52}, 1), 70));
	CHECK(Near(MatchChartTop({3, 400}, 1), 400));
	CHECK(Near(MatchChartTop({0.2, 0.1}, 0.5), 0.5));
	CHECK(Near(MatchChartTop({}, 1), 1));
	CHECK(Near(MatchChartTop({0, 0, 0}, 1), 1));
	const std::vector<MatchFacts> games = {Game(20, false, false, 10, 2), Game(1, false, true, 0, 0), Game(30, false, true, 20, 8)};
	CHECK(Near(MatchMetricAverage(games, kMetricKills), 15));
	CHECK(Near(MatchMetricAverage(games, kMetricDuration), 25));
	CHECK(Near(MatchMetricAverage({}, kMetricKills), 0));
}

void Ages()
{
	CHECK_EQ(static_cast<int>(MatchDayNumber(1970, 1, 1)), 0);
	CHECK_EQ(static_cast<int>(MatchDayNumber(2000, 3, 1) - MatchDayNumber(2000, 2, 28)), 2);
	CHECK_EQ(static_cast<int>(MatchDayNumber(2026, 1, 1) - MatchDayNumber(2025, 12, 31)), 1);
	auto age = [](int y, int m, int d) { return MatchAgeOf(y, m, d, 2026, 11, 11); };
	CHECK_EQ(static_cast<int>(age(2026, 11, 11).unit), static_cast<int>(kAgeToday));
	// A date after today, as with a wrong clock, reads as today.
	CHECK_EQ(static_cast<int>(age(2026, 12, 1).unit), static_cast<int>(kAgeToday));
	CHECK_EQ(static_cast<int>(age(2026, 11, 10).unit), static_cast<int>(kAgeYesterday));
	CHECK_EQ(static_cast<int>(age(2026, 11, 9).unit), static_cast<int>(kAgeDays));
	CHECK_EQ(age(2026, 11, 9).count, 2);
	CHECK_EQ(age(2026, 11, 5).count, 6);
	CHECK_EQ(static_cast<int>(age(2026, 11, 4).unit), static_cast<int>(kAgeWeeks));
	CHECK_EQ(age(2026, 11, 4).count, 1);
	CHECK_EQ(age(2026, 10, 13).count, 4);
	CHECK_EQ(static_cast<int>(age(2026, 10, 12).unit), static_cast<int>(kAgeMonths));
	CHECK_EQ(age(2026, 10, 12).count, 1);
	CHECK_EQ(age(2026, 1, 1).count, 10);
	CHECK_EQ(static_cast<int>(age(2025, 11, 11).unit), static_cast<int>(kAgeYears));
	CHECK_EQ(age(2023, 11, 1).count, 3);
}

}

int main()
{
	Results();
	Metrics();
	Select();
	Leagues();
	Scale();
	Ages();
	return test::Summary("matchhistory");
}
