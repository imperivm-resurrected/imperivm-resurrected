// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/matchhistory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
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

std::vector<size_t> MatchSelect(const std::vector<MatchFacts> &games, MatchMode mode, MatchOutcome outcome, size_t limit)
{
	std::vector<size_t> out;
	for (size_t i = 0; i < games.size() && (!limit || out.size() < limit); i++) {
		const MatchFacts &m = games[i];
		if (mode == kModeSkirmish && m.multi) continue;
		if (mode == kModeMultiplayer && !m.multi) continue;
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
	std::vector<double> sorted = values;
	std::sort(sorted.begin(), sorted.end(), std::greater<double>());
	double top = sorted[0];
	// Only one game that dwarfs all the others is cut, so the rest stay readable. A percentile cut several big games when most
	// were short or abandoned (zeros), and the chart showed only cut bars. With few values there is nothing to compare with.
	if (sorted.size() >= 5 && sorted[1] > 0 && sorted[0] > sorted[1] * 2) top = sorted[1] * 1.15;
	return std::max(top, floor);
}

double MatchMetricAverage(const std::vector<MatchFacts> &games, MatchMetric metric)
{
	std::vector<double> values;
	for (const MatchFacts &m : games) {
		if (MatchResultOf(m) != kMatchAbandoned) values.push_back(MatchMetricValue(m, metric));
	}
	return MatchAverage(values);
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
