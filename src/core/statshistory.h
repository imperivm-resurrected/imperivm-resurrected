// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Saved histories store values in this order: new metrics go at the end, before the count.
enum StatMetric {
	kStatScore, kStatMilitary, kStatDevelopment, kStatArmyStrength,
	kStatUnitsAlive, kStatUnitsBuilt, kStatUnitsMax, kStatLevelMax,
	kStatKilled, kStatLost, kStatDamageDealt, kStatDamageTaken,
	kStatGoldHeld, kStatFoodHeld, kStatGoldSpent, kStatFoodSpent,
	kStatGoldTaxes, kStatGoldCaptured, kStatGoldOther, kStatGoldTotal,
	kStatInGame,  // 1 while the player has not lost; files from before it count as 1
	kStatMetricCount
};

struct StatSample {
	uint32_t ms = 0;
	int32_t values[kStatMetricCount] = {};
};

constexpr int kStatMaxPlayers = 16;

struct StatsPlayerInfo {
	int index = 0;
	std::string name;
	uint32_t rgb = 0;
	bool human = false, local = false, ally = false;
	int team = 0;  // the lowest player index among the allies
};

// What a game's players did over time. Keeps a bounded number of samples: when one player reaches the limit
// every other sample of all of them goes, and samples are taken half as often from then on.
class StatsHistory {
public:
	static constexpr size_t kMaxSamples = 600;
	static constexpr uint32_t kBaseIntervalMs = 10000;

	void Clear();
	void Add(int player, const StatSample &sample);
	const std::vector<StatSample> &Samples(int player) const;
	uint32_t IntervalMs() const { return kBaseIntervalMs << halvings_; }
	uint32_t LastMs() const { return lastMs_; }
	bool Empty() const { return !hasSamples_; }
	// Drops what comes after a time, as when a history is older than its save says.
	void TrimAfter(uint32_t ms);
	int Halvings() const { return halvings_; }
	void SetHalvings(int halvings);

private:
	friend bool StatsDecode(const std::string &data, StatsHistory &history, std::vector<StatsPlayerInfo> &players);
	static constexpr int kMaxHalvings = 16;
	void AddRestored(int player, const StatSample &sample);
	void Halve();
	std::vector<StatSample> samples_[kStatMaxPlayers];
	int halvings_ = 0;
	uint32_t lastMs_ = 0;
	bool hasSamples_ = false;
};

// What goes in a save's .stats file. Decoding checks everything, as the file can be damaged or from another version.
std::string StatsEncode(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players);
bool StatsDecode(const std::string &data, StatsHistory &history, std::vector<StatsPlayerInfo> &players);

// Allies of allies are one team, named by its lowest player index.
void StatsGroupTeams(const bool active[kStatMaxPlayers], const bool allied[kStatMaxPlayers][kStatMaxPlayers], int teams[kStatMaxPlayers]);

struct StatsTeam {
	int id = 0;
	std::vector<StatsPlayerInfo> members;
};
std::vector<StatsTeam> StatsTeams(const std::vector<StatsPlayerInfo> &players);

// How the values of a team's players add up: the best level, the average kill ratio, and the sum of the rest.
int StatsCombine(StatMetric metric, const std::vector<int> &values);
struct StatsPoint {
	uint32_t ms = 0;
	int value = 0;
};
std::vector<StatsPoint> StatsTeamSeries(const StatsHistory &history, const std::vector<StatsPlayerInfo> &members, StatMetric metric);

struct StatsElimination {
	int player = 0;
	uint32_t ms = 0;
};
// In order of time. A player already out in their first sample has no known moment and is not listed.
std::vector<StatsElimination> StatsEliminations(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players);
bool StatsStillInGame(const StatsHistory &history, int player);
// The team that is left when all the others have lost, or -1.
int StatsWinningTeam(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players);
// Who has the highest last value of a metric, above zero; -1 if nobody.
int StatsBestPlayer(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players, StatMetric metric);

struct StatsWindow {
	uint32_t from = 0, to = 0;
};
// The part of a window between two fractions of its width, in either order and at least minSpan long, always inside the limits.
StatsWindow StatsZoom(StatsWindow window, double a, double b, uint32_t minSpan, StatsWindow limits);

// The last sample at or before the time, or the first one if they all come later. Null if there are none.
const StatSample *StatsSampleAt(const std::vector<StatSample> &samples, uint32_t ms);

struct StatsAxis {
	int top = 4, step = 1;
};
// Round numbers for a chart's value axis, about four divisions up to at least the maximum.
StatsAxis StatsNiceAxis(int maximum);

// "12:05", or "1:02:09" from the first hour.
std::string StatsFormatTime(uint32_t ms);
// "950", "12.5k", "3.2M".
std::string StatsFormatShort(int value);
