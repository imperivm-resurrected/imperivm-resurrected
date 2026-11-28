// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueroomoptions.h"

#include <cctype>
#include <utility>

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

const char *YesNo(bool v)
{
	return v ? "yes" : "no";
}

}

LeagueRoomRequest LeagueRoomRequestOf(const LeagueRoomSettings &s)
{
	LeagueRoomRequest r;
	r.preset = s.preset;
	r.fog = s.fog;
	r.explored = s.explored;
	r.hideCivs = s.hideCivs;
	r.cheats = s.cheats;
	r.timers = s.timers;
	r.diplomacy = s.diplomacy;
	return r;
}

LeagueRoomRequest LeagueRoomRequestOf(const TourneySettings &s)
{
	LeagueRoomRequest r;
	r.preset = s.preset;
	r.fog = s.fog;
	r.explored = s.explored;
	r.hideCivs = s.hideCivs ? 1 : 0;
	r.cheats = 0;
	r.timers = s.timers;
	r.diplomacy = s.diplomacy;
	r.speed = s.speed;
	return r;
}

LeagueRoomPreset LeagueRoomPresetOf(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "classic") return LeagueRoomPreset::Classic;
	if (s == "various") return LeagueRoomPreset::Various;
	return LeagueRoomPreset::Competitive;
}

const char *LeagueRoomPresetId(LeagueRoomPreset p)
{
	switch (p) {
	case LeagueRoomPreset::Classic: return "classic";
	case LeagueRoomPreset::Various: return "various";
	default: return "competitive";
	}
}

const char *LeagueRoomClimateName(int climate)
{
	return climate == 1 ? "autumn" : climate == 2 ? "winter" : "spring";
}

// The presets of the normal room (mpui's kPresets) and of the cups (TourneyPresetRules): competitive 5000 gold, normal population, no
// bonuses, fog, unexplored, diplomacy locked; classic the game's own gold and population with bonuses, fog, unexplored, diplomacy free;
// various 10000 gold, high population, bonuses, no fog, explored, diplomacy free. Competitive and various turn shared control on.
LeagueRoomOptions LeagueRoomOptionsFor(const LeagueRoomRequest &r)
{
	LeagueRoomOptions o;
	o.preset = LeagueRoomPresetOf(r.preset);
	switch (o.preset) {
	case LeagueRoomPreset::Classic:
		o.goldKey = "Default@gold";
		o.populationKey = "Normal@population";
		o.bonuses = true;
		o.fog = true;
		o.explored = false;
		o.lockDiplomacy = false;
		break;
	case LeagueRoomPreset::Various:
		o.goldKey = "10000@gold";
		o.populationKey = "High@population";
		o.bonuses = true;
		o.fog = false;
		o.explored = true;
		o.lockDiplomacy = false;
		o.sharedControl = true;
		break;
	default:
		o.goldKey = "5000@gold";
		o.populationKey = "Normal@population";
		o.bonuses = false;
		o.fog = true;
		o.explored = false;
		o.lockDiplomacy = true;
		o.sharedControl = true;
		break;
	}
	if (r.fog >= 0) o.fog = r.fog != 0;
	if (r.explored >= 0) o.explored = r.explored != 0;
	const std::string diplomacy = Lower(r.diplomacy);
	if (diplomacy == "locked") o.lockDiplomacy = true;
	else if (diplomacy == "free" || diplomacy == "open") o.lockDiplomacy = false;
	o.hideCivs = r.hideCivs == 1;
	o.cheats = r.cheats == 1;
	o.timers = r.timers != 0;
	o.climate = r.climate >= 0 && r.climate <= 2 ? r.climate : 0;
	o.gameSpeed = r.speed >= 70 && r.speed <= 300 ? r.speed * 10 : -1;
	return o;
}

std::string LeagueRoomOptionsText(const LeagueRoomOptions &o)
{
	std::string s = std::string("preset ") + LeagueRoomPresetId(o.preset);
	s += ", gold " + o.goldKey + ", population " + o.populationKey;
	s += ", game type item " + std::to_string(o.gameType) + ", limit item " + std::to_string(o.limit);
	s += std::string(", bonuses ") + YesNo(o.bonuses) + ", fog " + YesNo(o.fog) + ", explored " + YesNo(o.explored);
	s += std::string(", shared support ") + YesNo(o.sharedSupport) + ", shared control " + YesNo(o.sharedControl);
	s += std::string(", diplomacy ") + (o.lockDiplomacy ? "locked" : "free") + ", hidden civilizations " + YesNo(o.hideCivs);
	s += std::string(", free attachment ") + YesNo(o.freeAttach) + ", cheats " + YesNo(o.cheats) + ", respawn timers " + YesNo(o.timers);
	s += std::string(", neutral sites ") + (o.allSites ? "all" : "some removed") + ", season " + LeagueRoomClimateName(o.climate);
	s += std::string(", content options ") + (o.contentDefaults ? "default" : "kept") + ", game speed " + std::to_string(o.gameSpeed);
	return s;
}

std::string LeagueRoomItemText(const std::string &s)
{
	static const std::pair<const char *, char> kAccents[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}, {"\xC3\xB3", 'o'}, {"\xC3\xBA", 'u'},
		{"\xC3\xB1", 'n'}, {"\xC3\xA0", 'a'}, {"\xC3\xA8", 'e'}, {"\xC3\xAC", 'i'}, {"\xC3\xB2", 'o'}, {"\xC3\xB9", 'u'}};
	std::string out;
	for (size_t i = 0; i < s.size();) {
		bool done = false;
		for (const auto &a : kAccents) {
			if (s.compare(i, 2, a.first) == 0) {
				out += a.second;
				i += 2;
				done = true;
				break;
			}
		}
		if (done) continue;
		const unsigned char c = static_cast<unsigned char>(s[i++]);
		if (isalnum(c)) out += static_cast<char>(tolower(c));
		else if (!out.empty() && out.back() != ' ') out += ' ';
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

int LeagueRoomItemFor(const std::vector<std::string> &items, const std::vector<char> &disabled, const std::vector<std::string> &wanted)
{
	for (size_t k = 0; k < items.size(); k++) {
		if (k < disabled.size() && disabled[k]) continue;
		const std::string item = LeagueRoomItemText(items[k]);
		for (const std::string &w : wanted) {
			if (!w.empty() && item == w) return static_cast<int>(k);
		}
	}
	return -1;
}
