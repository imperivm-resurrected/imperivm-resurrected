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

MatchFacts CivGame(int race, uint32_t minutes, bool lost, uint32_t kills, uint32_t losses, uint32_t damage)
{
	MatchFacts m = Game(minutes, false, lost, kills, losses);
	m.race = race;
	m.damageDealt = damage;
	m.gold = 1000;
	return m;
}

void Civs()
{
	std::vector<MatchFacts> games = {CivGame(2, 20, false, 30, 10, 9000), CivGame(5, 30, true, 6, 24, 3000), CivGame(2, 10, true, 10, 10, 1000),
		CivGame(2, 1, false, 0, 0, 0), CivGame(-1, 15, false, 4, 1, 500), CivGame(5, 25, false, 20, 4, 6000), CivGame(7, 40, true, 1, 1, 100)};
	games[2].bestLevel = 4;
	games[5].bestLevel = 6;
	// Most games first, the same count by number, the unknown civilization last.
	CHECK(MatchCivsPlayed(games) == std::vector<int>({2, 5, 7, -1}));
	CHECK(MatchCivsPlayed({}).empty());

	// Every game counts for the games and the wins, as in the profile's summary; the abandoned one stays out of the averages.
	const MatchCivRecord romans = MatchCivRecordOf(games, 2);
	CHECK_EQ(romans.games, 3);
	CHECK_EQ(romans.wins, 2);
	CHECK_EQ(romans.winPercent, 66);
	CHECK_EQ(romans.ran, 2);
	CHECK_EQ(static_cast<unsigned long long>(romans.kills), 40ULL);
	CHECK_EQ(static_cast<unsigned long long>(romans.deaths), 20ULL);
	CHECK(Near(romans.killRatio, 2));
	CHECK(Near(romans.kills1, 20));
	CHECK(Near(romans.damageDealt, 5000));
	CHECK(Near(romans.minutes, 15));
	CHECK(Near(romans.gold, 1000));
	CHECK_EQ(romans.bestLevel, 4u);
	CHECK_EQ(romans.bestGame, 2);
	CHECK_EQ(romans.goldSpentGames, 0);

	// The ratio is of the totals, not an average of each game's.
	const MatchCivRecord gauls = MatchCivRecordOf(games, 5);
	CHECK(Near(gauls.killRatio, 26.0 / 28.0));
	CHECK_EQ(gauls.winPercent, 50);
	CHECK_EQ(gauls.bestGame, 5);

	const MatchCivRecord all = MatchCivRecordOf(games, kCivAll);
	CHECK_EQ(all.games, 7);
	CHECK_EQ(all.wins, 4);
	CHECK_EQ(all.winPercent, 57);
	CHECK_EQ(all.ran, 6);
	CHECK_EQ(static_cast<unsigned long long>(all.kills), 71ULL);
	CHECK_EQ(all.bestLevel, 6u);

	const MatchCivRecord none = MatchCivRecordOf(games, 3);
	CHECK_EQ(none.games, 0);
	CHECK_EQ(none.winPercent, 0);
	CHECK_EQ(none.bestGame, -1);
	CHECK(Near(none.killRatio, 0));
	CHECK(Near(MatchKillRatio(7, 0), 7));

	// A script's count replaces the game's for the ratio, only with both kills and deaths; the gold spent averages its own games.
	games[0].scriptKills = 50;
	games[0].scriptDeaths = 5;
	games[0].goldSpent = 4000;
	games[2].scriptKills = 99;
	games[2].goldSpent = 2000;
	const MatchCivRecord counted = MatchCivRecordOf(games, 2);
	CHECK_EQ(counted.scriptGames, 1);
	CHECK_EQ(static_cast<unsigned long long>(counted.kills), 60ULL);
	CHECK_EQ(static_cast<unsigned long long>(counted.deaths), 15ULL);
	CHECK(Near(counted.killRatio, 4));
	CHECK_EQ(counted.goldSpentGames, 2);
	CHECK(Near(counted.goldSpent, 3000));
	CHECK_EQ(MatchKillsOf(games[2]), 10u);
	CHECK_EQ(MatchDeathsOf(games[0]), 5u);
}

void Notes()
{
	MatchNote n;
	n.startSec = 1763650000;
	n.endSec = 1763651800;
	n.goldSpent = 12345;
	CHECK_EQ(MatchNoteLine(n), std::string("1763650000 1763651800 12345 -1 -1"));
	MatchNote back;
	CHECK(MatchNoteParse(MatchNoteLine(n), back));
	CHECK((back.startSec) == 1763650000);
	CHECK((back.endSec) == 1763651800);
	CHECK((back.goldSpent) == 12345);
	CHECK((back.scriptKills) == -1);
	CHECK(MatchNoteParse("1763650000 1763651800 -1 40 12\n", back));
	CHECK((back.scriptKills) == 40);
	CHECK((back.scriptDeaths) == 12);
	// Half a count is none.
	CHECK(MatchNoteParse("1763650000 1763651800 -1 40 -1", back));
	CHECK((back.scriptKills) == -1);
	CHECK(!MatchNoteParse("1763650000 1763651800 5", back));
	CHECK(!MatchNoteParse("garbage", back));
	CHECK(!MatchNoteParse("1763651800 1763650000 1 1 1", back));

	// The game stamps its record with the start or the end, to the minute; a later game that started inside the window wins.
	const std::vector<MatchNote> notes = {{1763640000, 1763643000, 10, -1, -1}, {1763650000, 1763651800, 20, -1, -1}, {1763651900, 1763652000, 30, -1, -1}};
	CHECK((MatchNoteOf(notes, 1763650000 - 50)->goldSpent) == 20);
	CHECK((MatchNoteOf(notes, 1763651800 + 60)->goldSpent) == 30);
	CHECK((MatchNoteOf(notes, 1763641000)->goldSpent) == 10);
	CHECK(MatchNoteOf(notes, 1763645000) == nullptr);
	CHECK(MatchNoteOf({}, 1763650000) == nullptr);
	MatchFacts m;
	MatchApplyNote(m, notes[1]);
	CHECK((m.goldSpent) == 20);
	CHECK((m.scriptKills) == -1);
}

}

int main()
{
	Results();
	Metrics();
	Select();
	Civs();
	Notes();
	Leagues();
	Scale();
	Ages();
	return test::Summary("matchhistory");
}
