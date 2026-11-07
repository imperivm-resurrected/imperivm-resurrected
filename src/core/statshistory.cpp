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

void StatsHistory::AddRestored(int player, const StatSample &sample)
{
	samples_[player].push_back(sample);
	lastMs_ = std::max(lastMs_, sample.ms);
	hasSamples_ = true;
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

void StatsHistory::TrimAfter(uint32_t ms)
{
	lastMs_ = 0;
	hasSamples_ = false;
	for (std::vector<StatSample> &samples : samples_) {
		while (!samples.empty() && samples.back().ms > ms) samples.pop_back();
		if (samples.empty()) continue;
		lastMs_ = std::max(lastMs_, samples.back().ms);
		hasSamples_ = true;
	}
}

void StatsHistory::SetHalvings(int halvings)
{
	halvings_ = std::max(0, std::min(halvings, kMaxHalvings));
}

const std::vector<StatSample> &StatsHistory::Samples(int player) const
{
	static const std::vector<StatSample> kNone;
	return player >= 0 && player < kStatMaxPlayers ? samples_[player] : kNone;
}

namespace {
const char kMagic[4] = {'I', 'M', 'S', 'H'};
const uint32_t kVersion = 1, kMaxMetrics = 256, kMaxName = 256;

void PutU32(std::string &out, uint32_t v)
{
	for (int i = 0; i < 4; i++) out.push_back(static_cast<char>(v >> (i * 8) & 255));
}

struct Reader {
	const std::string &data;
	size_t at = 0;
	bool U32(uint32_t &v)
	{
		if (data.size() - at < 4) return false;
		v = 0;
		for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(static_cast<unsigned char>(data[at + i])) << (i * 8);
		at += 4;
		return true;
	}
	bool Text(std::string &text, uint32_t size)
	{
		if (data.size() - at < size) return false;
		text.assign(data, at, size);
		at += size;
		return true;
	}
};
}

std::string StatsEncode(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players)
{
	std::string out(kMagic, sizeof(kMagic));
	PutU32(out, kVersion);
	PutU32(out, kStatMetricCount);
	PutU32(out, static_cast<uint32_t>(history.Halvings()));
	PutU32(out, static_cast<uint32_t>(players.size()));
	for (const StatsPlayerInfo &info : players) {
		PutU32(out, static_cast<uint32_t>(info.index));
		PutU32(out, info.rgb);
		PutU32(out, (info.human ? 1u : 0u) | (info.local ? 2u : 0u) | (info.ally ? 4u : 0u) | static_cast<uint32_t>(info.team) << 8);
		const std::string name = info.name.substr(0, kMaxName);
		PutU32(out, static_cast<uint32_t>(name.size()));
		out += name;
		const std::vector<StatSample> &samples = history.Samples(info.index);
		PutU32(out, static_cast<uint32_t>(samples.size()));
		for (const StatSample &s : samples) {
			PutU32(out, s.ms);
			for (const int32_t value : s.values) PutU32(out, static_cast<uint32_t>(value));
		}
	}
	return out;
}

bool StatsDecode(const std::string &data, StatsHistory &history, std::vector<StatsPlayerInfo> &players)
{
	StatsHistory read;
	std::vector<StatsPlayerInfo> readPlayers;
	if (data.size() < sizeof(kMagic) || data.compare(0, sizeof(kMagic), kMagic, sizeof(kMagic)) != 0) return false;
	Reader r{data, sizeof(kMagic)};
	uint32_t version = 0, metrics = 0, halvings = 0, count = 0;
	if (!r.U32(version) || !r.U32(metrics) || !r.U32(halvings) || !r.U32(count)) return false;
	if (version != kVersion || metrics == 0 || metrics > kMaxMetrics || count > kStatMaxPlayers) return false;
	bool seen[kStatMaxPlayers] = {};
	for (uint32_t p = 0; p < count; p++) {
		StatsPlayerInfo info;
		uint32_t index = 0, rgb = 0, flags = 0, nameSize = 0, samples = 0;
		if (!r.U32(index) || !r.U32(rgb) || !r.U32(flags) || !r.U32(nameSize)) return false;
		if (index >= kStatMaxPlayers || seen[index] || nameSize > kMaxName || !r.Text(info.name, nameSize) || !r.U32(samples)) return false;
		if (samples > StatsHistory::kMaxSamples) return false;
		seen[index] = true;
		info.index = static_cast<int>(index);
		info.rgb = rgb;
		info.human = (flags & 1) != 0;
		info.local = (flags & 2) != 0;
		info.ally = (flags & 4) != 0;
		const uint32_t team = flags >> 8;
		info.team = team < kStatMaxPlayers ? static_cast<int>(team) : 0;
		uint32_t previous = 0;
		for (uint32_t i = 0; i < samples; i++) {
			StatSample s;
			if (!r.U32(s.ms) || (i && s.ms <= previous)) return false;
			previous = s.ms;
			s.values[kStatInGame] = 1;
			for (uint32_t m = 0; m < metrics; m++) {
				uint32_t value = 0;
				if (!r.U32(value)) return false;
				if (m < kStatMetricCount) s.values[m] = static_cast<int32_t>(value);
			}
			read.AddRestored(info.index, s);
		}
		readPlayers.push_back(info);
	}
	read.SetHalvings(static_cast<int>(std::min<uint32_t>(halvings, 64)));
	history = read;
	players = readPlayers;
	return true;
}

void StatsGroupTeams(const bool active[kStatMaxPlayers], const bool allied[kStatMaxPlayers][kStatMaxPlayers], int teams[kStatMaxPlayers])
{
	for (int i = 0; i < kStatMaxPlayers; i++) teams[i] = i;
	bool changed = true;
	while (changed) {
		changed = false;
		for (int a = 0; a < kStatMaxPlayers; a++) {
			for (int b = a + 1; b < kStatMaxPlayers; b++) {
				if (!active[a] || !active[b] || !allied[a][b] || teams[a] == teams[b]) continue;
				const int low = std::min(teams[a], teams[b]);
				teams[a] = teams[b] = low;
				changed = true;
			}
		}
	}
}

std::vector<StatsTeam> StatsTeams(const std::vector<StatsPlayerInfo> &players)
{
	std::vector<StatsTeam> teams;
	for (const StatsPlayerInfo &info : players) {
		auto it = std::find_if(teams.begin(), teams.end(), [&](const StatsTeam &t) { return t.id == info.team; });
		if (it == teams.end()) {
			teams.push_back(StatsTeam());
			teams.back().id = info.team;
			it = teams.end() - 1;
		}
		it->members.push_back(info);
	}
	return teams;
}

int StatsCombine(StatMetric metric, const std::vector<int> &values)
{
	if (values.empty()) return 0;
	if (metric == kStatLevelMax) return *std::max_element(values.begin(), values.end());
	long long total = 0;
	for (const int value : values) total += value;
	if (metric == kStatMilitary) total /= static_cast<long long>(values.size());
	return static_cast<int>(total);
}

std::vector<StatsPoint> StatsTeamSeries(const StatsHistory &history, const std::vector<StatsPlayerInfo> &members, StatMetric metric)
{
	std::vector<uint32_t> times;
	for (const StatsPlayerInfo &info : members) {
		for (const StatSample &s : history.Samples(info.index)) times.push_back(s.ms);
	}
	std::sort(times.begin(), times.end());
	times.erase(std::unique(times.begin(), times.end()), times.end());
	std::vector<StatsPoint> points;
	points.reserve(times.size());
	for (const uint32_t ms : times) {
		std::vector<int> values;
		for (const StatsPlayerInfo &info : members) {
			if (const StatSample *s = StatsSampleAt(history.Samples(info.index), ms)) values.push_back(s->values[metric]);
		}
		StatsPoint point;
		point.ms = ms;
		point.value = StatsCombine(metric, values);
		points.push_back(point);
	}
	return points;
}

std::vector<StatsElimination> StatsEliminations(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players)
{
	std::vector<StatsElimination> out;
	for (const StatsPlayerInfo &info : players) {
		bool wasIn = false;
		for (const StatSample &s : history.Samples(info.index)) {
			if (s.values[kStatInGame]) {
				wasIn = true;
			}
			else if (wasIn) {
				StatsElimination e;
				e.player = info.index;
				e.ms = s.ms;
				out.push_back(e);
				break;
			}
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const StatsElimination &a, const StatsElimination &b) { return a.ms < b.ms; });
	return out;
}

bool StatsStillInGame(const StatsHistory &history, int player)
{
	const std::vector<StatSample> &samples = history.Samples(player);
	return !samples.empty() && samples.back().values[kStatInGame] != 0;
}

int StatsWinningTeam(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players)
{
	const std::vector<StatsTeam> teams = StatsTeams(players);
	int alive = -1, count = 0;
	for (const StatsTeam &team : teams) {
		for (const StatsPlayerInfo &info : team.members) {
			if (!StatsStillInGame(history, info.index)) continue;
			alive = team.id;
			count++;
			break;
		}
	}
	return count == 1 && teams.size() > 1 ? alive : -1;
}

int StatsBestPlayer(const StatsHistory &history, const std::vector<StatsPlayerInfo> &players, StatMetric metric)
{
	int best = -1, top = 0;
	for (const StatsPlayerInfo &info : players) {
		const std::vector<StatSample> &samples = history.Samples(info.index);
		if (samples.empty() || samples.back().values[metric] <= top) continue;
		top = samples.back().values[metric];
		best = info.index;
	}
	return best;
}

StatsWindow StatsZoom(StatsWindow window, double a, double b, uint32_t minSpan, StatsWindow limits)
{
	if (a > b) std::swap(a, b);
	a = std::max(0.0, std::min(1.0, a));
	b = std::max(0.0, std::min(1.0, b));
	const double length = window.to > window.from ? static_cast<double>(window.to - window.from) : 0.0;
	StatsWindow out;
	out.from = window.from + static_cast<uint32_t>(length * a);
	out.to = window.from + static_cast<uint32_t>(length * b);
	const uint32_t total = limits.to > limits.from ? limits.to - limits.from : 0;
	minSpan = std::min(minSpan, total);
	if (out.to - out.from < minSpan) {
		const uint32_t missing = minSpan - (out.to - out.from);
		out.from = out.from - limits.from > missing / 2 ? out.from - missing / 2 : limits.from;
		out.to = out.from + minSpan;
	}
	if (out.to > limits.to) {
		out.from -= out.to - limits.to;
		out.to = limits.to;
	}
	return out;
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
