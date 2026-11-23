// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/matchhistory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

MatchResult MatchResultOf(const MatchFacts &m)
{
	if (m.durationMs < kMatchAbandonedMs && m.kills == 0 && m.losses == 0) return kMatchAbandoned;
	return m.lost ? kMatchLost : kMatchWon;
}

double MatchMetricValue(const MatchFacts &m, MatchMetric metric)
{
	switch (metric) {
	case kMetricKills: return m.kills;
	case kMetricLosses: return m.losses;
	case kMetricKillsPerLoss: return static_cast<double>(m.kills) / std::max<uint32_t>(1, m.losses);
	case kMetricArmy: return m.armyMax;
	case kMetricGold: return m.gold;
	case kMetricDuration: return m.durationMs / 60000.0;
	default: return 0;
	}
}

int MatchLeagueOf(const std::vector<MatchTag> &tags, int64_t recordedSec, uint32_t durationMs)
{
	const int64_t slack = 120;
	int league = kMatchOpen;
	int64_t best = 0;
	for (const MatchTag &t : tags) {
		if (t.league == kMatchOpen || recordedSec < t.startSec - slack || recordedSec > t.startSec + durationMs / 1000 + slack) continue;
		if (league == kMatchOpen || t.startSec > best) {
			league = t.league;
			best = t.startSec;
		}
	}
	return league;
}

int MatchCivKey(int race, bool named)
{
	return named && race >= 0 ? race : kCivUnknown;
}

std::vector<size_t> MatchSelect(const std::vector<MatchFacts> &games, MatchMode mode, MatchOutcome outcome, size_t limit, int race)
{
	std::vector<size_t> out;
	for (size_t i = 0; i < games.size() && (!limit || out.size() < limit); i++) {
		const MatchFacts &m = games[i];
		if (race != kCivAll && m.race != race) continue;
		if (mode == kModeSkirmish && m.multi) continue;
		if (mode == kModeMultiplayer && !m.multi) continue;
		if (mode == kModeLeague && m.league != kMatchLeague) continue;
		if (mode == kModeCup && m.league != kMatchCup) continue;
		const MatchResult r = MatchResultOf(m);
		if (outcome == kOutcomeVictories && r != kMatchWon) continue;
		if (outcome == kOutcomeDefeats && r != kMatchLost) continue;
		out.push_back(i);
	}
	return out;
}

double MatchPercentile(std::vector<double> values, double fraction)
{
	if (values.empty()) return 0;
	std::sort(values.begin(), values.end());
	const size_t rank = static_cast<size_t>(std::max(0.0, std::min(1.0, fraction)) * (values.size() - 1));
	return values[rank];
}

double MatchAverage(const std::vector<double> &values)
{
	if (values.empty()) return 0;
	double sum = 0;
	for (double v : values) sum += v;
	return sum / values.size();
}

double MatchChartTop(const std::vector<double> &values, double floor)
{
	if (values.empty()) return floor;
	// The scale is the biggest game, so no bar is ever cut: a cut bar read as a drawing fault, and the exact value of a small
	// game is in its tooltip anyway.
	return std::max(*std::max_element(values.begin(), values.end()), floor);
}

double MatchMetricAverage(const std::vector<MatchFacts> &games, MatchMetric metric)
{
	std::vector<double> values;
	for (const MatchFacts &m : games) {
		if (MatchResultOf(m) != kMatchAbandoned) values.push_back(MatchMetricValue(m, metric));
	}
	return MatchAverage(values);
}

std::string MatchNoteLine(const MatchNote &n)
{
	char buf[160];
	snprintf(buf, sizeof(buf), "%lld %lld %lld %lld %lld", static_cast<long long>(n.startSec), static_cast<long long>(n.endSec),
		static_cast<long long>(n.goldSpent), static_cast<long long>(n.scriptKills), static_cast<long long>(n.scriptDeaths));
	return buf;
}

bool MatchNoteParse(const std::string &line, MatchNote &out)
{
	long long v[5] = {};
	if (sscanf_s(line.c_str(), "%lld %lld %lld %lld %lld", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5) return false;
	if (v[0] <= 0 || v[1] < v[0]) return false;
	MatchNote n;
	n.startSec = v[0];
	n.endSec = v[1];
	n.goldSpent = std::max(-1LL, v[2]);
	// Counts the game cannot have taken from a script are no counts.
	n.scriptKills = v[3] >= 0 && v[4] >= 0 ? v[3] : -1;
	n.scriptDeaths = v[3] >= 0 && v[4] >= 0 ? v[4] : -1;
	out = n;
	return true;
}

const MatchNote *MatchNoteOf(const std::vector<MatchNote> &notes, int64_t recordedSec)
{
	const int64_t slack = 120;
	const MatchNote *best = nullptr;
	for (const MatchNote &n : notes) {
		if (recordedSec < n.startSec - slack || recordedSec > n.endSec + slack) continue;
		if (!best || n.startSec > best->startSec) best = &n;
	}
	return best;
}

void MatchApplyNote(MatchFacts &m, const MatchNote &n)
{
	m.goldSpent = n.goldSpent;
	m.scriptKills = n.scriptKills;
	m.scriptDeaths = n.scriptDeaths;
}

uint32_t MatchKillsOf(const MatchFacts &m)
{
	return m.scriptKills >= 0 && m.scriptDeaths >= 0 ? static_cast<uint32_t>(std::min<int64_t>(m.scriptKills, UINT32_MAX)) : m.kills;
}

uint32_t MatchDeathsOf(const MatchFacts &m)
{
	return m.scriptKills >= 0 && m.scriptDeaths >= 0 ? static_cast<uint32_t>(std::min<int64_t>(m.scriptDeaths, UINT32_MAX)) : m.losses;
}

double MatchKillRatio(uint64_t kills, uint64_t deaths)
{
	return static_cast<double>(kills) / std::max<uint64_t>(1, deaths);
}

MatchCivRecord MatchCivRecordOf(const std::vector<MatchFacts> &games, int race)
{
	MatchCivRecord r;
	r.race = race;
	double sums[9] = {}, goldSpent = 0;
	for (size_t i = 0; i < games.size(); i++) {
		const MatchFacts &m = games[i];
		if (race != kCivAll && m.race != race) continue;
		r.games++;
		if (!m.lost) r.wins++;
		const uint32_t kills = MatchKillsOf(m), deaths = MatchDeathsOf(m);
		r.kills += kills;
		r.deaths += deaths;
		if (m.scriptKills >= 0 && m.scriptDeaths >= 0) r.scriptGames++;
		if (m.bestLevel > r.bestLevel) {
			r.bestLevel = m.bestLevel;
			r.bestGame = static_cast<int>(i);
		}
		if (MatchResultOf(m) == kMatchAbandoned) continue;
		r.ran++;
		const double values[9] = {static_cast<double>(kills), static_cast<double>(deaths), static_cast<double>(m.damageDealt), static_cast<double>(m.damageTaken),
			static_cast<double>(m.gold), static_cast<double>(m.food), static_cast<double>(m.produced), static_cast<double>(m.armyMax), m.durationMs / 60000.0};
		for (int k = 0; k < 9; k++) sums[k] += values[k];
		if (m.goldSpent >= 0) {
			r.goldSpentGames++;
			goldSpent += static_cast<double>(m.goldSpent);
		}
	}
	r.winPercent = r.games > 0 ? static_cast<int>(static_cast<int64_t>(r.wins) * 100 / r.games) : 0;
	r.killRatio = MatchKillRatio(r.kills, r.deaths);
	if (r.ran) {
		double *averages[9] = {&r.kills1, &r.deaths1, &r.damageDealt, &r.damageTaken, &r.gold, &r.food, &r.produced, &r.army, &r.minutes};
		for (int k = 0; k < 9; k++) *averages[k] = sums[k] / r.ran;
	}
	if (r.goldSpentGames) r.goldSpent = goldSpent / r.goldSpentGames;
	return r;
}

std::vector<int> MatchCivsPlayed(const std::vector<MatchFacts> &games)
{
	std::vector<std::pair<int, int>> counts;
	for (const MatchFacts &m : games) {
		auto it = std::find_if(counts.begin(), counts.end(), [&](const std::pair<int, int> &c) { return c.first == m.race; });
		if (it == counts.end()) counts.push_back({m.race, 1});
		else it->second++;
	}
	std::sort(counts.begin(), counts.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
		if ((a.first < 0) != (b.first < 0)) return b.first < 0;
		return a.second != b.second ? a.second > b.second : a.first < b.first;
	});
	std::vector<int> out;
	for (const auto &c : counts) out.push_back(c.first);
	return out;
}

int64_t MatchDayNumber(int year, int month, int day)
{
	// Howard Hinnant's days_from_civil.
	const int64_t y = static_cast<int64_t>(year) - (month <= 2 ? 1 : 0);
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const int64_t yoe = y - era * 400;
	const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

MatchAge MatchAgeOf(int year, int month, int day, int todayYear, int todayMonth, int todayDay)
{
	const int64_t days = MatchDayNumber(todayYear, todayMonth, todayDay) - MatchDayNumber(year, month, day);
	MatchAge a;
	if (days <= 0) a.unit = kAgeToday;
	else if (days == 1) a.unit = kAgeYesterday;
	else if (days < 7) {
		a.unit = kAgeDays;
		a.count = static_cast<int>(days);
	}
	else if (days < 30) {
		a.unit = kAgeWeeks;
		a.count = static_cast<int>(days / 7);
	}
	else if (days < 365) {
		a.unit = kAgeMonths;
		a.count = static_cast<int>(days / 30);
	}
	else {
		a.unit = kAgeYears;
		a.count = static_cast<int>(days / 365);
	}
	return a;
}
