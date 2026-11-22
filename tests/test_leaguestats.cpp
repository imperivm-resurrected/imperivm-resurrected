// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <string>

#include "core/json.h"
#include "core/leaguestats.h"

namespace {

LeagueStatsPoint At(uint32_t tick, int32_t killed)
{
	LeagueStatsPoint p;
	p.tick = tick;
	p.ms = tick * 100;
	p.values[kStatKilled] = killed;
	return p;
}

void TicksAndHalving()
{
	LeagueTickStats s;
	CHECK(s.Empty());
	CHECK(!s.Due(0));
	CHECK(!s.Due(150));
	CHECK(s.Due(200));
	// Ticks that are not due are not kept.
	s.Add(0, At(150, 1));
	CHECK(s.Empty());
	for (uint32_t t = 100; t <= LeagueTickStats::kMax * 100; t += 100) {
		s.Add(0, At(t, static_cast<int32_t>(t)));
		s.Add(1, At(t, 0));
	}
	CHECK_EQ(s.Points(0).size(), LeagueTickStats::kMax);
	CHECK_EQ(s.Every(), 100u);
	// One more: every other one goes, for every player, and only even steps stay.
	s.Add(0, At((LeagueTickStats::kMax + 1) * 100, 1));
	CHECK_EQ(s.Every(), 200u);
	CHECK_EQ(s.Points(0).size(), LeagueTickStats::kMax / 2);
	CHECK_EQ(s.Points(1).size(), LeagueTickStats::kMax / 2);
	CHECK_EQ(s.Points(0).front().tick, 200u);
	CHECK(!s.Due(300));
	// A late computer that started at tick 3000 keeps a subset of the same ticks.
	LeagueTickStats late;
	for (uint32_t t = 3000; t <= 6100; t += 100) late.Add(0, At(t, static_cast<int32_t>(t)));
	for (const LeagueStatsPoint &p : late.Points(0)) CHECK_EQ(p.tick % 100, 0u);
	// A lower tick starts again.
	s.Add(0, At(100, 5));
	CHECK_EQ(s.Every(), 100u);
	CHECK_EQ(s.Points(0).size(), 1u);
	CHECK(s.Points(1).empty());
}

void JsonOfSeats()
{
	LeagueTickStats s;
	s.Add(2, At(100, 7));
	s.Add(2, At(200, 9));
	s.Add(5, At(200, 3));
	LeagueStatsSeat a;
	a.index = 2;
	a.steamId = "76561198000000401";
	a.name = "Varo";
	a.team = 1;
	LeagueStatsSeat ai;
	ai.index = 6;
	ai.name = "AI";
	const Json j = LeagueStatsJson(s, {a, ai});
	CHECK_EQ(static_cast<int>(j.GetInt("v")), 1);
	CHECK_EQ(static_cast<int>(j.GetInt("every")), 100);
	CHECK_EQ(static_cast<int>(j.GetInt("metrics")), static_cast<int>(kStatMetricCount));
	const Json *players = j.Get("players");
	CHECK(players && players->arr.size() == 1);
	const Json &p = players->arr[0];
	CHECK_EQ(p.GetString("steamId"), std::string("76561198000000401"));
	const Json *points = p.Get("points");
	CHECK(points && points->arr.size() == 2);
	CHECK_EQ(points->arr[1].arr.size(), static_cast<size_t>(kStatMetricCount + 2));
	CHECK_EQ(static_cast<int>(points->arr[1].arr[0].num), 200);
	CHECK_EQ(static_cast<int>(points->arr[1].arr[2 + kStatKilled].num), 9);
}

void Detail()
{
	Json j;
	CHECK(Json::Parse(R"({"id":7,"queue":"1v1","playedAt":1800000000000,"durationMs":1500000,"endedBy":"victory","map":{"name":"continent","size":"small"},
		"players":[{"steamId":"76561198000000401","name":"Varo","team":1,"civ":"rome","result":"win","gloryDelta":18},
			{"steamId":"76561198000000402","name":"Tulia","team":2,"civ":"gaul","result":"loss","gloryDelta":null}],
		"stats":{"v":1,"every":100,"metrics":3,"players":[{"index":0,"steamId":"76561198000000401","name":"Varo","team":1,"points":[[100,10000,5,6,7],[200,20000,8,9,10]]}]},
		"statsCheck":"verified"})", j));
	LeagueMatchDetail d;
	CHECK(LeagueParseMatchDetail(j, d));
	CHECK(d.id == 7);
	CHECK(d.durationMs == 1500000u);
	CHECK_EQ(d.statsCheck, std::string("verified"));
	CHECK_EQ(d.mapName, std::string("continent"));
	CHECK(d.players.size() == 2);
	CHECK(d.players[0].hasDelta && d.players[0].gloryDelta == 18);
	CHECK(!d.players[1].hasDelta);
	const LeagueDetailStats *s = LeagueDetailStatsOf(d, d.players[0]);
	CHECK(s && s->points.size() == 2);
	// Fewer values than this mod knows: the rest stay at 0.
	CHECK_EQ(s->points[1].values[2], 10);
	CHECK_EQ(s->points[1].values[3], 0);
	CHECK(LeagueDetailStatsOf(d, d.players[1]) == nullptr);
	CHECK(!LeagueParseMatchDetail(Json(), d));
}

}

int main()
{
	TicksAndHalving();
	JsonOfSeats();
	Detail();
	return test::Summary("leaguestats");
}
