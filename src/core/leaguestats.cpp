// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leaguestats.h"

#include <algorithm>

void LeagueTickStats::Add(int player, const LeagueStatsPoint &p)
{
	if (player < 0 || player >= kStatMaxPlayers) return;
	if (p.tick < lastTick_) Clear();
	lastTick_ = p.tick;
	if (!Due(p.tick)) return;
	std::vector<LeagueStatsPoint> &list = points_[player];
	if (!list.empty() && list.back().tick == p.tick) list.back() = p;
	else list.push_back(p);
	if (list.size() <= kMax) return;
	every_ *= 2;
	for (std::vector<LeagueStatsPoint> &l : points_) {
		l.erase(std::remove_if(l.begin(), l.end(), [&](const LeagueStatsPoint &x) { return x.tick % every_ != 0; }), l.end());
	}
}

const std::vector<LeagueStatsPoint> &LeagueTickStats::Points(int player) const
{
	static const std::vector<LeagueStatsPoint> none;
	return player >= 0 && player < kStatMaxPlayers ? points_[player] : none;
}

bool LeagueTickStats::Empty() const
{
	return std::all_of(std::begin(points_), std::end(points_), [](const std::vector<LeagueStatsPoint> &l) { return l.empty(); });
}

void LeagueTickStats::Clear()
{
	for (std::vector<LeagueStatsPoint> &l : points_) l.clear();
	every_ = kBaseEvery;
	lastTick_ = 0;
}

Json LeagueStatsJson(const LeagueTickStats &stats, const std::vector<LeagueStatsSeat> &seats)
{
	Json o = Json::MakeObject();
	o.Set("v", Json::MakeNumber(1));
	o.Set("every", Json::MakeNumber(stats.Every()));
	o.Set("metrics", Json::MakeNumber(kStatMetricCount));
	Json players = Json::MakeArray();
	for (const LeagueStatsSeat &seat : seats) {
		const std::vector<LeagueStatsPoint> &points = stats.Points(seat.index);
		if (points.empty()) continue;
		Json p = Json::MakeObject();
		p.Set("index", Json::MakeNumber(seat.index));
		p.Set("steamId", seat.steamId.empty() ? Json() : Json::MakeString(seat.steamId));
		p.Set("name", Json::MakeString(seat.name));
		p.Set("team", Json::MakeNumber(seat.team));
		Json list = Json::MakeArray();
		for (const LeagueStatsPoint &x : points) {
			Json row = Json::MakeArray();
			row.arr.push_back(Json::MakeNumber(x.tick));
			row.arr.push_back(Json::MakeNumber(x.ms));
			for (const int32_t v : x.values) row.arr.push_back(Json::MakeNumber(v));
			list.arr.push_back(row);
		}
		p.Set("points", list);
		players.arr.push_back(p);
	}
	o.Set("players", players);
	return o;
}

bool LeagueParseMatchDetail(const Json &j, LeagueMatchDetail &out)
{
	out = LeagueMatchDetail();
	if (j.type != Json::Object || !j.Get("players")) return false;
	out.id = j.GetInt64("id");
	out.playedAt = j.GetInt64("playedAt");
	out.queue = j.GetString("queue");
	out.endedBy = j.GetString("endedBy");
	out.statsCheck = j.GetString("statsCheck", "none");
	out.durationMs = static_cast<uint32_t>(std::max<int64_t>(0, j.GetInt64("durationMs")));
	if (const Json *map = j.Get("map"); map && map->type == Json::Object) {
		out.mapName = map->GetString("name");
		out.mapSize = map->GetString("size");
	}
	for (const Json &p : j.Get("players")->arr) {
		if (p.type != Json::Object) continue;
		LeagueDetailPlayer x;
		x.steamId = p.GetString("steamId");
		x.name = p.GetString("name");
		x.civ = p.GetString("civ");
		x.result = p.GetString("result");
		x.team = p.GetInt("team");
		const Json *delta = p.Get("gloryDelta");
		x.hasDelta = delta && delta->type == Json::Number;
		x.gloryDelta = x.hasDelta ? static_cast<int>(delta->num) : 0;
		out.players.push_back(x);
	}
	const Json *stats = j.Get("stats");
	const int metrics = stats && stats->type == Json::Object ? stats->GetInt("metrics") : 0;
	const Json *players = stats ? stats->Get("players") : nullptr;
	if (players && players->type == Json::Array) {
		for (const Json &p : players->arr) {
			const Json *points = p.Get("points");
			if (p.type != Json::Object || !points || points->type != Json::Array) continue;
			LeagueDetailStats s;
			s.index = p.GetInt("index");
			s.team = p.GetInt("team");
			s.steamId = p.GetString("steamId");
			s.name = p.GetString("name");
			for (const Json &row : points->arr) {
				if (row.type != Json::Array || row.arr.size() < 2) continue;
				LeagueStatsPoint x;
				x.tick = static_cast<uint32_t>(row.arr[0].num);
				x.ms = static_cast<uint32_t>(row.arr[1].num);
				// A later mod may send more values: the ones this mod knows are read; fewer leave the rest at 0.
				const size_t n = std::min<size_t>(row.arr.size() - 2, std::min<size_t>(kStatMetricCount, static_cast<size_t>(std::max(0, metrics))));
				for (size_t i = 0; i < n; i++) x.values[i] = static_cast<int32_t>(static_cast<int64_t>(row.arr[i + 2].num));
				s.points.push_back(x);
			}
			if (!s.points.empty()) out.stats.push_back(s);
		}
	}
	return true;
}

const LeagueDetailStats *LeagueDetailStatsOf(const LeagueMatchDetail &d, const LeagueDetailPlayer &p)
{
	for (const LeagueDetailStats &s : d.stats) {
		if (!p.steamId.empty() && s.steamId == p.steamId) return &s;
	}
	for (const LeagueDetailStats &s : d.stats) {
		if (s.steamId.empty() && !p.name.empty() && s.name == p.name) return &s;
	}
	return nullptr;
}
