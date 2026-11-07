// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/statshistory.h"

static StatSample At(uint32_t ms, int value = 0)
{
	StatSample s;
	s.ms = ms;
	s.values[kStatScore] = value;
	return s;
}

static void AddAndClear()
{
	StatsHistory h;
	CHECK(h.Empty());
	h.Add(2, At(1000));
	h.Add(2, At(2000));
	CHECK(!h.Empty());
	CHECK_EQ(static_cast<int>(h.Samples(2).size()), 2);
	CHECK_EQ(static_cast<int>(h.Samples(3).size()), 0);
	CHECK_EQ(h.LastMs(), 2000u);
	h.Clear();
	CHECK(h.Empty());
	CHECK_EQ(h.LastMs(), 0u);
}

static void IgnoresBadPlayers()
{
	StatsHistory h;
	h.Add(-1, At(1));
	h.Add(kStatMaxPlayers, At(1));
	CHECK(h.Empty());
	CHECK(h.Samples(-1).empty());
	CHECK(h.Samples(kStatMaxPlayers).empty());
}

static void SameTimeReplaces()
{
	StatsHistory h;
	h.Add(0, At(1000, 5));
	h.Add(0, At(1000, 7));
	CHECK_EQ(static_cast<int>(h.Samples(0).size()), 1);
	CHECK_EQ(h.Samples(0)[0].values[kStatScore], 7);
}

static void StaysBoundedAndKeepsTheEnds()
{
	StatsHistory h;
	const uint32_t first = h.IntervalMs();
	uint32_t ms = 0;
	for (size_t i = 0; i < StatsHistory::kMaxSamples * 5; i++, ms += 1000) {
		h.Add(0, At(ms));
		h.Add(1, At(ms));
		CHECK(h.Samples(0).size() < StatsHistory::kMaxSamples);
		CHECK(h.Samples(1).size() < StatsHistory::kMaxSamples);
	}
	CHECK(h.IntervalMs() > first);
	CHECK_EQ(h.Samples(0).front().ms, 0u);
	CHECK_EQ(h.Samples(0).back().ms, ms - 1000);
	for (size_t i = 1; i < h.Samples(0).size(); i++) CHECK(h.Samples(0)[i - 1].ms < h.Samples(0)[i].ms);
}

static void FindsTheSampleAtATime()
{
	std::vector<StatSample> samples = {At(1000, 1), At(2000, 2), At(3000, 3)};
	CHECK(StatsSampleAt({}, 5) == nullptr);
	CHECK_EQ(StatsSampleAt(samples, 0)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 1000)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 1999)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 2000)->values[kStatScore], 2);
	CHECK_EQ(StatsSampleAt(samples, 9000)->values[kStatScore], 3);
}

static void AxisIsRoundAndCoversTheMaximum()
{
	CHECK_EQ(StatsNiceAxis(0).top, 4);
	CHECK_EQ(StatsNiceAxis(-5).top, 4);
	CHECK_EQ(StatsNiceAxis(1).step, 1);
	CHECK_EQ(StatsNiceAxis(100).step, 50);
	CHECK_EQ(StatsNiceAxis(100).top, 100);
	CHECK_EQ(StatsNiceAxis(2150).step, 1000);
	CHECK_EQ(StatsNiceAxis(2150).top, 3000);
	for (int maximum : {3, 7, 19, 240, 999, 1001, 54321, 2000000}) {
		const StatsAxis axis = StatsNiceAxis(maximum);
		CHECK(axis.top >= maximum);
		CHECK_EQ(axis.top % axis.step, 0);
		CHECK(axis.top / axis.step <= 8);
	}
}

static void FormatsTexts()
{
	CHECK_EQ(StatsFormatTime(0), std::string("0:00"));
	CHECK_EQ(StatsFormatTime(65000), std::string("1:05"));
	CHECK_EQ(StatsFormatTime(3729000), std::string("1:02:09"));
	CHECK_EQ(StatsFormatShort(950), std::string("950"));
	CHECK_EQ(StatsFormatShort(9999), std::string("9999"));
	CHECK_EQ(StatsFormatShort(12500), std::string("12.5k"));
	CHECK_EQ(StatsFormatShort(3200000), std::string("3.2M"));
	CHECK_EQ(StatsFormatShort(-12500), std::string("-12.5k"));
}

static StatsHistory SomeHistory(std::vector<StatsPlayerInfo> &players)
{
	StatsHistory h;
	StatsPlayerInfo a, b;
	a.index = 0;
	a.name = "THIONG";
	a.rgb = 0xff0000;
	a.human = a.local = true;
	b.index = 5;
	b.name = "Eóin (IA)";
	b.rgb = 0x00ff00;
	b.ally = true;
	players = {a, b};
	for (uint32_t ms = 10000; ms <= 50000; ms += 10000) {
		StatSample s = At(ms, static_cast<int>(ms / 100));
		s.values[kStatMetricCount - 1] = -7;
		h.Add(0, s);
		if (ms <= 30000) h.Add(5, s);
	}
	return h;
}

static void SavedHistoryComesBack()
{
	std::vector<StatsPlayerInfo> players, back;
	const StatsHistory h = SomeHistory(players);
	StatsHistory read;
	CHECK(StatsDecode(StatsEncode(h, players), read, back));
	CHECK_EQ(static_cast<int>(back.size()), 2);
	CHECK_EQ(back[1].index, 5);
	CHECK_EQ(back[1].name, std::string("Eóin (IA)"));
	CHECK_EQ(back[0].rgb, 0xff0000u);
	CHECK(back[0].human && back[0].local && !back[0].ally);
	CHECK(!back[1].human && !back[1].local && back[1].ally);
	CHECK_EQ(static_cast<int>(read.Samples(0).size()), 5);
	CHECK_EQ(static_cast<int>(read.Samples(5).size()), 3);
	CHECK_EQ(read.Samples(0)[4].values[kStatScore], 500);
	CHECK_EQ(read.Samples(0)[4].values[kStatMetricCount - 1], -7);
	CHECK_EQ(read.LastMs(), 50000u);
}

static void DamagedHistoryIsRefused()
{
	std::vector<StatsPlayerInfo> players, back;
	const StatsHistory h = SomeHistory(players);
	const std::string good = StatsEncode(h, players);
	StatsHistory keep;
	keep.Add(1, At(5, 9));
	CHECK(!StatsDecode("", keep, back));
	CHECK(!StatsDecode("hello there, this is no history", keep, back));
	for (size_t size = 0; size < good.size(); size += 37) CHECK(!StatsDecode(good.substr(0, size), keep, back));
	std::string wrongVersion = good;
	wrongVersion[4] = 99;
	CHECK(!StatsDecode(wrongVersion, keep, back));
	CHECK_EQ(keep.Samples(1)[0].values[kStatScore], 9);
}

static void EmptyHistoryRoundTrips()
{
	std::vector<StatsPlayerInfo> players, back;
	StatsHistory h = SomeHistory(players);
	std::string data = StatsEncode(h, std::vector<StatsPlayerInfo>{});
	StatsHistory read;
	CHECK(StatsDecode(data, read, back));
	CHECK(read.Empty());
}

static void TrimsWhatComesAfter()
{
	std::vector<StatsPlayerInfo> players;
	StatsHistory h = SomeHistory(players);
	h.TrimAfter(25000);
	CHECK_EQ(static_cast<int>(h.Samples(0).size()), 2);
	CHECK_EQ(static_cast<int>(h.Samples(5).size()), 2);
	CHECK_EQ(h.LastMs(), 20000u);
	h.TrimAfter(5);
	CHECK(h.Empty());
}

static StatsPlayerInfo Player(int index, int team, const char *name = "x")
{
	StatsPlayerInfo info;
	info.index = index;
	info.team = team;
	info.name = name;
	return info;
}

static StatSample Value(uint32_t ms, StatMetric metric, int value, bool inGame = true)
{
	StatSample s;
	s.ms = ms;
	s.values[metric] = value;
	s.values[kStatInGame] = inGame ? 1 : 0;
	return s;
}

static void GroupsAlliesOfAllies()
{
	bool active[kStatMaxPlayers] = {};
	bool allied[kStatMaxPlayers][kStatMaxPlayers] = {};
	for (int i = 0; i < 6; i++) active[i] = true;
	allied[1][3] = allied[3][5] = true;
	int teams[kStatMaxPlayers];
	StatsGroupTeams(active, allied, teams);
	CHECK_EQ(teams[0], 0);
	CHECK_EQ(teams[1], 1);
	CHECK_EQ(teams[3], 1);
	CHECK_EQ(teams[5], 1);
	CHECK_EQ(teams[2], 2);
	CHECK_EQ(teams[4], 4);
}

static void GroupsPlayersByTeam()
{
	const std::vector<StatsTeam> teams = StatsTeams({Player(0, 0), Player(1, 1), Player(2, 0), Player(3, 3)});
	CHECK_EQ(static_cast<int>(teams.size()), 3);
	CHECK_EQ(teams[0].id, 0);
	CHECK_EQ(static_cast<int>(teams[0].members.size()), 2);
	CHECK_EQ(teams[0].members[1].index, 2);
	CHECK_EQ(teams[2].id, 3);
}

static void CombinesEachMetricItsWay()
{
	CHECK_EQ(StatsCombine(kStatKilled, {}), 0);
	CHECK_EQ(StatsCombine(kStatKilled, {3, 4}), 7);
	CHECK_EQ(StatsCombine(kStatLevelMax, {3, 9, 4}), 9);
	CHECK_EQ(StatsCombine(kStatMilitary, {100, 50}), 75);
}

static void AddsTeamSeriesAcrossMembers()
{
	StatsHistory h;
	h.Add(0, Value(1000, kStatKilled, 1));
	h.Add(0, Value(2000, kStatKilled, 2));
	h.Add(1, Value(2000, kStatKilled, 10));
	h.Add(1, Value(3000, kStatKilled, 20));
	const std::vector<StatsPoint> points = StatsTeamSeries(h, {Player(0, 0), Player(1, 0)}, kStatKilled);
	CHECK_EQ(static_cast<int>(points.size()), 3);
	CHECK_EQ(points[0].value, 11);
	CHECK_EQ(points[1].value, 12);
	CHECK_EQ(points[2].ms, 3000u);
	CHECK_EQ(points[2].value, 22);
}

static void FindsEliminationsInOrder()
{
	StatsHistory h;
	h.Add(0, Value(1000, kStatScore, 1));
	h.Add(0, Value(5000, kStatScore, 1, false));
	h.Add(0, Value(6000, kStatScore, 1, false));
	h.Add(1, Value(1000, kStatScore, 1));
	h.Add(1, Value(2000, kStatScore, 1, false));
	h.Add(2, Value(1000, kStatScore, 1));
	h.Add(3, Value(1000, kStatScore, 1, false));
	const std::vector<StatsElimination> out = StatsEliminations(h, {Player(0, 0), Player(1, 1), Player(2, 2), Player(3, 3)});
	CHECK_EQ(static_cast<int>(out.size()), 2);
	CHECK_EQ(out[0].player, 1);
	CHECK_EQ(out[0].ms, 2000u);
	CHECK_EQ(out[1].player, 0);
	CHECK(StatsStillInGame(h, 2));
	CHECK(!StatsStillInGame(h, 3));
	CHECK(!StatsStillInGame(h, 9));
}

static void KnowsTheWinner()
{
	const std::vector<StatsPlayerInfo> players = {Player(0, 0), Player(1, 0), Player(2, 2)};
	StatsHistory h;
	h.Add(0, Value(1000, kStatScore, 1));
	h.Add(1, Value(1000, kStatScore, 1));
	h.Add(2, Value(1000, kStatScore, 1));
	CHECK_EQ(StatsWinningTeam(h, players), -1);
	h.Add(2, Value(2000, kStatScore, 1, false));
	CHECK_EQ(StatsWinningTeam(h, players), 0);
	h.Add(1, Value(2000, kStatScore, 1, false));
	CHECK_EQ(StatsWinningTeam(h, players), 0);
	h.Add(0, Value(2000, kStatScore, 1, false));
	CHECK_EQ(StatsWinningTeam(h, players), -1);
	CHECK_EQ(StatsWinningTeam(h, {Player(0, 0)}), -1);
}

static void FindsTheBest()
{
	const std::vector<StatsPlayerInfo> players = {Player(0, 0), Player(1, 1), Player(2, 2)};
	StatsHistory h;
	CHECK_EQ(StatsBestPlayer(h, players, kStatKilled), -1);
	h.Add(0, Value(1000, kStatKilled, 0));
	CHECK_EQ(StatsBestPlayer(h, players, kStatKilled), -1);
	h.Add(1, Value(1000, kStatKilled, 5));
	h.Add(2, Value(1000, kStatKilled, 5));
	CHECK_EQ(StatsBestPlayer(h, players, kStatKilled), 1);
	h.Add(2, Value(2000, kStatKilled, 8));
	CHECK_EQ(StatsBestPlayer(h, players, kStatKilled), 2);
}

static void SavedTeamsComeBackAndOldFilesCountAsInGame()
{
	std::vector<StatsPlayerInfo> players = {Player(5, 3, "a")}, back;
	StatsHistory h, read;
	h.Add(5, Value(1000, kStatScore, 9, false));
	std::string data = StatsEncode(h, players);
	CHECK(StatsDecode(data, read, back));
	CHECK_EQ(back[0].team, 3);
	CHECK(!StatsStillInGame(read, 5));
	// The same file as a version without the last metric wrote it.
	const size_t metricCountAt = 8;
	data[metricCountAt] = static_cast<char>(kStatMetricCount - 1);
	data.erase(data.size() - 4);
	CHECK(StatsDecode(data, read, back));
	CHECK(StatsStillInGame(read, 5));
}

int main()
{
	GroupsAlliesOfAllies();
	GroupsPlayersByTeam();
	CombinesEachMetricItsWay();
	AddsTeamSeriesAcrossMembers();
	FindsEliminationsInOrder();
	KnowsTheWinner();
	FindsTheBest();
	SavedTeamsComeBackAndOldFilesCountAsInGame();
	SavedHistoryComesBack();
	DamagedHistoryIsRefused();
	EmptyHistoryRoundTrips();
	TrimsWhatComesAfter();
	AddAndClear();
	IgnoresBadPlayers();
	SameTimeReplaces();
	StaysBoundedAndKeepsTheEnds();
	FindsTheSampleAtATime();
	AxisIsRoundAndCoversTheMaximum();
	FormatsTexts();
	return test::Summary("statshistory");
}
