// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/statshistory.h"

#include <algorithm>
#include <cstdio>

void StatsHistory::Clear()
{
	for (std::vector<StatSample> &samples : samples_) samples.clear();
	halvings_ = 0;
	lastMs_ = 0;
	hasSamples_ = false;
}

void StatsHistory::Add(int player, const StatSample &sample)
{
	if (player < 0 || player >= kStatMaxPlayers) return;
	std::vector<StatSample> &samples = samples_[player];
	if (!samples.empty() && samples.back().ms == sample.ms) samples.back() = sample;
	else samples.push_back(sample);
	lastMs_ = std::max(lastMs_, sample.ms);
	hasSamples_ = true;
	if (samples.size() >= kMaxSamples) Halve();
}

void StatsHistory::Halve()
{
	for (std::vector<StatSample> &samples : samples_) {
		if (samples.size() < 3) continue;
		const StatSample last = samples.back();
		size_t kept = 0;
		for (size_t i = 0; i < samples.size(); i += 2) samples[kept++] = samples[i];
		samples.resize(kept);
		if (samples.back().ms != last.ms) samples.push_back(last);
	}
	halvings_++;
}

const std::vector<StatSample> &StatsHistory::Samples(int player) const
{
	static const std::vector<StatSample> kNone;
	return player >= 0 && player < kStatMaxPlayers ? samples_[player] : kNone;
}

const StatSample *StatsSampleAt(const std::vector<StatSample> &samples, uint32_t ms)
{
	if (samples.empty()) return nullptr;
	const auto after = std::upper_bound(samples.begin(), samples.end(), ms, [](uint32_t t, const StatSample &s) { return t < s.ms; });
	return after == samples.begin() ? &samples.front() : &*(after - 1);
}

StatsAxis StatsNiceAxis(int maximum)
{
	StatsAxis axis;
	if (maximum <= 0) return axis;
	const double rough = maximum / 4.0;
	double scale = 1;
	while (scale * 10 <= rough) scale *= 10;
	int step = 1;
	for (const int nice : {1, 2, 5, 10}) {
		step = static_cast<int>(nice * scale);
		if (step >= rough) break;
	}
	step = std::max(step, 1);
	axis.step = step;
	axis.top = (maximum + step - 1) / step * step;
	return axis;
}

std::string StatsFormatTime(uint32_t ms)
{
	const uint32_t total = ms / 1000, hours = total / 3600, minutes = total / 60 % 60, seconds = total % 60;
	char text[32];
	if (hours) snprintf(text, sizeof(text), "%u:%02u:%02u", hours, minutes, seconds);
	else snprintf(text, sizeof(text), "%u:%02u", minutes, seconds);
	return text;
}

std::string StatsFormatShort(int value)
{
	char text[32];
	const int size = value < 0 ? -value : value;
	if (size >= 1000000) snprintf(text, sizeof(text), "%.1fM", value / 1000000.0);
	else if (size >= 10000) snprintf(text, sizeof(text), "%.1fk", value / 1000.0);
	else snprintf(text, sizeof(text), "%d", value);
	return text;
}
