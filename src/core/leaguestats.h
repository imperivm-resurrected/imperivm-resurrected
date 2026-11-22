// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/statshistory.h"

// One player's values at a tick of the game.
struct LeagueStatsPoint {
	uint32_t tick = 0, ms = 0;
	int32_t values[kStatMetricCount] = {};
};

// What the league reports of a game's players: their values at ticks of the game, the same ticks on every computer. Every computer runs
// the same game, so two reports of one game hold the same numbers at the ticks both have, and the league can tell when one was changed.
// Samples are taken every `Every()` ticks; past kMax of a player, every other one goes and the step doubles. Which ticks stay depends only
// on the step, never on when this computer started counting: a player who rejoined late keeps a subset of the others' ticks.
class LeagueTickStats {
public:
	static constexpr uint32_t kBaseEvery = 100;
	static constexpr size_t kMax = 60;
	bool Due(uint32_t tick) const { return tick != 0 && tick % every_ == 0; }
	// A tick lower than the last one seen starts again (a new game, or the same one loaded for a rejoin).
	void Add(int player, const LeagueStatsPoint &p);
	const std::vector<LeagueStatsPoint> &Points(int player) const;
	uint32_t Every() const { return every_; }
	bool Empty() const;
	void Clear();

private:
	std::vector<LeagueStatsPoint> points_[kStatMaxPlayers];
	uint32_t every_ = kBaseEvery;
	uint32_t lastTick_ = 0;
};

// A player of the report: their seat (index), their account when known, name and team.
struct LeagueStatsSeat {
	int index = 0;
	std::string steamId, name;
	int team = 0;
};
// {v: 1, every, metrics, players: [{index, steamId|null, name, team, points: [[tick, ms, v0...], ...]}]}: only the seats given.
Json LeagueStatsJson(const LeagueTickStats &stats, const std::vector<LeagueStatsSeat> &seats);

// GET /v1/league/matches/:id/detail: a finished ranked game for its page.
struct LeagueDetailPlayer {
	std::string steamId, name, civ, result;
	int team = 0;
	// The Gloria won or lost; hasDelta is false in a placement game or a void one.
	int gloryDelta = 0;
	bool hasDelta = false;
};
struct LeagueDetailStats {
	int index = 0, team = 0;
	std::string steamId, name;
	std::vector<LeagueStatsPoint> points;
};
// The two reports' numbers: "verified" (both agree), "mismatch", "single" (one sent numbers) or "none".
struct LeagueMatchDetail {
	int64_t id = 0, playedAt = 0;
	std::string queue, endedBy, statsCheck = "none", mapName, mapSize;
	uint32_t durationMs = 0;
	std::vector<LeagueDetailPlayer> players;
	std::vector<LeagueDetailStats> stats;
};
bool LeagueParseMatchDetail(const Json &j, LeagueMatchDetail &out);
// The numbers of a player of the page: by account, or by name for a seat without one. Null when the game sent none for them.
const LeagueDetailStats *LeagueDetailStatsOf(const LeagueMatchDetail &d, const LeagueDetailPlayer &p);
