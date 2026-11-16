// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/seasondecors.h"

#include <cctype>
#include <cstdlib>
#include <map>

#include "core/strutil.h"

namespace {
std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
	while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
	return s.substr(a, b - a);
}

using strutil::Lower;

const char *SuffixOf(int season)
{
	if (season == 2) return " autumn";
	if (season == 3) return " winter";
	return nullptr;
}
}

std::vector<SeasonDecorPair> SeasonDecorPairs(const std::string &decorsIni, int season)
{
	std::vector<SeasonDecorPair> out;
	const char *suffix = SuffixOf(season);
	if (!suffix) return out;
	std::map<std::string, unsigned> types;
	std::string section;
	size_t pos = 0;
	while (pos < decorsIni.size()) {
		size_t end = decorsIni.find('\n', pos);
		if (end == std::string::npos) end = decorsIni.size();
		const std::string line = Trim(decorsIni.substr(pos, end - pos));
		pos = end + 1;
		if (line.empty() || line[0] == ';') continue;
		if (line[0] == '[') {
			const size_t close = line.find(']');
			section = close == std::string::npos ? std::string() : Lower(Trim(line.substr(1, close - 1)));
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos || section.empty()) continue;
		if (Lower(Trim(line.substr(0, eq))) != "type") continue;
		const long value = strtol(Trim(line.substr(eq + 1)).c_str(), nullptr, 10);
		if (value > 0 && value < 256) types[section] = static_cast<unsigned>(value);
	}
	const std::string tail = suffix;
	for (const auto &entry : types) {
		const std::string &name = entry.first;
		if (name.size() <= tail.size() || name.compare(name.size() - tail.size(), tail.size(), tail) != 0) continue;
		const auto base = types.find(name.substr(0, name.size() - tail.size()));
		if (base == types.end()) continue;
		SeasonDecorPair pair;
		pair.base = base->second;
		pair.twin = entry.second;
		out.push_back(pair);
	}
	return out;
}
