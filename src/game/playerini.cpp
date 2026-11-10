// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/playerini.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

typedef std::map<std::string, std::string> Section;

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string Trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
	return s.substr(a, b - a);
}

std::map<std::string, Section> ReadSections(const std::string &text)
{
	std::map<std::string, Section> out;
	Section *current = nullptr;
	size_t pos = 0;
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;
	while (pos < text.size()) {
		size_t end = text.find('\n', pos);
		if (end == std::string::npos) end = text.size();
		const std::string line = Trim(text.substr(pos, end - pos));
		pos = end + 1;
		if (line.empty() || line[0] == ';') continue;
		if (line[0] == '[') {
			const size_t close = line.find(']');
			current = &out[Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)))];
			continue;
		}
		const size_t eq = line.find('=');
		if (!current || eq == std::string::npos) continue;
		(*current)[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
	}
	return out;
}

// As CHashINI::GetInt: an optional minus sign and the digits that follow, wrapping like the game's int.
uint32_t ToInt(const Section &s, const char *key)
{
	const auto it = s.find(key);
	if (it == s.end()) return 0;
	const std::string &v = it->second;
	size_t i = 0;
	const bool negative = !v.empty() && v[0] == '-';
	if (negative) i++;
	uint32_t n = 0;
	for (; i < v.size() && v[i] >= '0' && v[i] <= '9'; i++) n = n * 10u + static_cast<uint32_t>(v[i] - '0');
	return negative ? 0u - n : n;
}

int ToSigned(const Section &s, const char *key) { return static_cast<int>(ToInt(s, key)); }

std::string ToText(const Section &s, const char *key)
{
	const auto it = s.find(key);
	return it == s.end() ? std::string() : it->second;
}

}

PlayerIni ParsePlayerIni(const std::string &text)
{
	PlayerIni out;
	const std::map<std::string, Section> sections = ReadSections(text);
	const auto player = sections.find("player");
	if (player != sections.end()) {
		const Section &p = player->second;
		out.name = ToText(p, "name");
		out.race = p.count("race") ? ToSigned(p, "race") : -1;
		out.games = ToSigned(p, "games");
		out.favoriteUnit = ToText(p, "fav");
		out.favoriteCount = ToSigned(p, "fval");
	}
	// The game reads game0, game1... and stops at the first one that is missing.
	for (int i = 0;; i++) {
		const auto it = sections.find("game" + std::to_string(i));
		if (it == sections.end()) break;
		const Section &s = it->second;
		PlayedGame g;
		g.index = i;
		g.year = ToSigned(s, "year");
		g.month = ToSigned(s, "month");
		g.day = ToSigned(s, "day");
		g.hour = ToSigned(s, "hour");
		g.minute = ToSigned(s, "minute");
		g.durationMs = ToInt(s, "duration");
		g.multi = ToInt(s, "multi") != 0;
		g.lost = ToInt(s, "lost") != 0;
		g.race = s.count("race") ? ToSigned(s, "race") : -1;
		g.gold = ToInt(s, "gold");
		g.food = ToInt(s, "food");
		g.unitsProduced = ToInt(s, "units_prod");
		g.unitsKilled = ToInt(s, "units_killed");
		g.unitsLost = ToInt(s, "units_lost");
		g.unitsMax = ToInt(s, "units_max");
		g.levelMax = ToInt(s, "level_max");
		g.levelMaxUnit = ToText(s, "level_max_unit");
		g.damageTaken = ToInt(s, "damage_taken");
		g.damageInflicted = ToInt(s, "damage_inflicted");
		g.killHealths = ToInt(s, "kill_healths");
		g.dieHealths = ToInt(s, "die_healths");
		out.played.push_back(g);
	}
	return out;
}

uint32_t GameRating(const PlayedGame &g)
{
	return ((g.killHealths >> 1) + 1000u + g.damageInflicted) * 100u / ((g.dieHealths >> 1) + 10000u + g.damageTaken);
}

int PercentOf(int part, int whole)
{
	return whole > 0 ? static_cast<int>(static_cast<int64_t>(part) * 100 / whole) : 0;
}

ProfileSummary SummarizeProfile(const PlayerIni &ini, const std::vector<RankStep> &ranks)
{
	ProfileSummary s;
	int64_t ratingSum = 0;
	std::map<int, int> races;
	for (const PlayedGame &g : ini.played) {
		if (g.multi) {
			s.multiGames++;
			if (!g.lost) s.multiWins++;
		}
		else {
			s.singleGames++;
			if (!g.lost) s.singleWins++;
		}
		s.playedMs += g.durationMs;
		s.kills += g.unitsKilled;
		ratingSum += static_cast<int32_t>(GameRating(g));
		if (g.race >= 0) races[g.race]++;
		if (s.bestLevel < g.levelMax) {
			s.bestLevel = g.levelMax;
			s.bestUnit = g.levelMaxUnit;
		}
		s.largestArmy = std::max(s.largestArmy, g.unitsMax);
	}
	s.games = s.singleGames + s.multiGames;
	s.rating = ratingSum / std::max(1, s.games);

	// The game's loop: it stops at the first rank whose points are above the rating and takes the one before.
	const int count = static_cast<int>(ranks.size());
	int i = 0;
	while (i < count && s.rating >= ranks[i].points) i++;
	s.rank = i - 1;
	s.nextRank = i < count ? i : -1;
	if (s.nextRank >= 0) s.nextPoints = ranks[s.nextRank].points;
	// CONST.INI gives one rank 2 points between 200 and 300, so the floor of the bar is the highest points reached so far.
	for (int k = 0; k < i; k++) s.rankPoints = std::max(s.rankPoints, ranks[k].points);

	int counted = 0;
	for (const auto &r : races) {
		counted += r.second;
		if (s.favoriteRace < 0 || r.second > races[s.favoriteRace]) s.favoriteRace = r.first;
	}
	if (s.favoriteRace >= 0) s.favoriteRacePercent = PercentOf(races[s.favoriteRace], counted);
	return s;
}

std::vector<PlayedGame> RecentGames(const PlayerIni &ini, size_t count)
{
	std::vector<PlayedGame> out = ini.played;
	const auto key = [](const PlayedGame &g) {
		return static_cast<int64_t>(g.year) * 100000000 + g.month * 1000000 + g.day * 10000 + g.hour * 100 + g.minute;
	};
	std::stable_sort(out.begin(), out.end(), [&](const PlayedGame &a, const PlayedGame &b) {
		return key(a) != key(b) ? key(a) > key(b) : a.index > b.index;
	});
	if (out.size() > count) out.resize(count);
	return out;
}
