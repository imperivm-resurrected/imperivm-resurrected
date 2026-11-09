// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/civsets.h"

#include <algorithm>
#include <mutex>

namespace {
const char kSeparator = '|';
std::mutex g_lock;
std::vector<std::string> g_sets;
}

bool CivSetsAllow(const std::vector<std::string> &sets, const std::string &set)
{
	return sets.empty() || std::find(sets.begin(), sets.end(), set) != sets.end();
}

std::string CivSetsJoin(const std::vector<std::string> &sets)
{
	std::string out;
	for (const std::string &s : sets) out += (out.empty() ? "" : std::string(1, kSeparator)) + s;
	return out;
}

std::vector<std::string> CivSetsSplit(const std::string &text)
{
	std::vector<std::string> out;
	for (size_t from = 0; from <= text.size();) {
		size_t to = text.find(kSeparator, from);
		if (to == std::string::npos) to = text.size();
		const std::string item = text.substr(from, to - from);
		if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
		from = to + 1;
	}
	return out;
}

std::vector<std::string> CivSetsToggle(const std::vector<std::string> &known, const std::vector<std::string> &now, const std::string &set)
{
	std::vector<std::string> chosen;
	for (const std::string &s : known) {
		if (CivSetsAllow(now, s) != (s == set)) chosen.push_back(s);
	}
	if (chosen.empty()) return now;
	if (chosen.size() == known.size()) chosen.clear();
	return chosen;
}

std::vector<std::string> CivSetsDefault(const std::vector<std::string> &known, const std::string &packSet)
{
	if (known.size() < 2) return {};
	const auto has = [&](const std::string &s) { return std::find(known.begin(), known.end(), s) != known.end(); };
	if (!packSet.empty() && has(packSet)) return {packSet};
	if (has(kCivSetGame)) return {kCivSetGame};
	return {};
}

std::vector<std::string> CivRoomSets()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_sets;
}

void CivRoomSetsSet(const std::vector<std::string> &sets)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_sets = sets;
}
