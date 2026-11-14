// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leaguemaps.h"

#include <cctype>
#include <cstring>
#include <utility>

namespace {

struct MapId {
	const char *id, *name;
	// Words of its name in the game's English, Spanish and Italian, and words that mean another map ("Islas grandes" is not "Isla").
	std::vector<const char *> stems, never;
};

// The game's types (data.pak: data/RandomMap/*.rm.xml, all but "Empty") and the sizes of its lists, with the names it shows.
const MapId kMaps[] = {
	{"coastal", "Coastal", {"coast", "coster", "costier"}, {}},
	{"continent", "Continent", {"contin"}, {}},
	{"island", "Island", {"island", "isla", "isola"}, {"large", "small", "grand", "peque", "picc"}},
	{"large_islands", "Large islands", {"large island", "islas grand", "isole grand"}, {}},
	{"mediterranean", "Mediterranean", {"mediterr"}, {}},
	{"mountain_passes", "Mountainous", {"mountain", "montan", "montag", "pasos", "passi"}, {}},
	{"randommap", "Random shape", {"randommap", "random", "aleatori", "casual"}, {}},
	{"small_islands", "Small islands", {"small island", "islas peque", "isole picc"}, {}},
	{"tournament", "Tournament", {"tournament", "torneo"}, {}},
	{"small", "Small", {"small", "peque", "picc"}, {}},
	{"medium", "Normal", {"medium", "normal", "median"}, {}},
	{"large", "Large", {"large", "huge", "grand", "enorm"}, {"muy", "very", "molto"}},
};

const char *const kSizes[] = {"small", "medium", "large"};

// The types whose template lets two players in on a small map.
const char *const kSmallTypes[] = {"continent", "island", "randommap"};
const size_t kTypeCount = 9;

const MapId *Find(const std::string &id)
{
	for (const MapId &m : kMaps) {
		if (id == m.id) return &m;
	}
	return nullptr;
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

// Plain ASCII lower case, with the accents of Spanish and Italian taken off (in UTF-8 or in the game's Windows-1252) and anything
// that is not a letter or a digit as one space, without spaces at the ends.
std::string Folded(const std::string &s)
{
	static const std::pair<const char *, char> kUtf8[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}, {"\xC3\xB3", 'o'},
		{"\xC3\xBA", 'u'}, {"\xC3\xB1", 'n'}, {"\xC3\xA0", 'a'}, {"\xC3\xA8", 'e'}, {"\xC3\xAC", 'i'}, {"\xC3\xB2", 'o'}, {"\xC3\xB9", 'u'},
		{"\xC3\x81", 'a'}, {"\xC3\x89", 'e'}, {"\xC3\x8D", 'i'}, {"\xC3\x93", 'o'}, {"\xC3\x9A", 'u'}, {"\xC3\x91", 'n'}, {"\xC3\xBC", 'u'}};
	static const std::pair<unsigned char, char> kAnsi[] = {{0xE1, 'a'}, {0xE9, 'e'}, {0xED, 'i'}, {0xF3, 'o'}, {0xFA, 'u'}, {0xF1, 'n'},
		{0xE0, 'a'}, {0xE8, 'e'}, {0xEC, 'i'}, {0xF2, 'o'}, {0xF9, 'u'}, {0xC1, 'a'}, {0xC9, 'e'}, {0xCD, 'i'}, {0xD3, 'o'}, {0xDA, 'u'},
		{0xD1, 'n'}, {0xFC, 'u'}};
	std::string out;
	auto add = [&out](char c) {
		if (c != ' ') out += c;
		else if (!out.empty() && out.back() != ' ') out += ' ';
	};
	for (size_t i = 0; i < s.size();) {
		bool replaced = false;
		for (const auto &a : kUtf8) {
			if (s.compare(i, 2, a.first) == 0) {
				add(a.second);
				i += 2;
				replaced = true;
				break;
			}
		}
		if (replaced) continue;
		const unsigned char c = static_cast<unsigned char>(s[i++]);
		char plain = c < 0x80 && isalnum(c) ? static_cast<char>(tolower(c)) : ' ';
		for (const auto &a : kAnsi) {
			if (c == a.first) plain = a.second;
		}
		add(plain);
	}
	if (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

bool Has(const std::string &haystack, const char *needle)
{
	return *needle && haystack.find(needle) != std::string::npos;
}

}

std::string LeagueMapIdOfTemplate(const std::string &path)
{
	static const char kSuffix[] = ".rm.xml";
	const size_t suffix = sizeof(kSuffix) - 1;
	const size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	if (name.size() <= suffix || _stricmp(name.c_str() + name.size() - suffix, kSuffix) != 0) return std::string();
	name.resize(name.size() - suffix);
	std::string out;
	for (char c : name) out += c == ' ' ? '_' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return out;
}

std::string LeagueMapSizeIdAt(int index)
{
	return index >= 0 && index < static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0])) ? kSizes[index] : std::string();
}

bool LeagueMapTextMatches(const std::string &item, const std::string &id)
{
	std::string want = Lower(id);
	for (char &c : want) {
		if (c == '_') c = ' ';
	}
	want = Folded(want);
	if (want.empty()) return false;
	// The type list says "Random map: <name>" in the game's language: only the name counts.
	const size_t colon = item.rfind(':');
	const std::string have = " " + Folded(colon == std::string::npos ? item : item.substr(colon + 1)) + " ";
	const MapId *known = Find(Lower(id));
	if (known) {
		for (const char *word : known->never) {
			if (Has(have, word)) return false;
		}
	}
	if (Has(have, want.c_str())) return true;
	if (!known) return false;
	for (const char *stem : known->stems) {
		if (Has(have, stem)) return true;
	}
	return false;
}

int LeagueMapItemFor(const std::vector<std::string> &keys, const std::vector<std::string> &items, const std::vector<char> &disabled,
	const std::string &id, bool &byText)
{
	byText = false;
	if (id.empty()) return -1;
	const std::string want = Lower(id);
	auto usable = [&disabled](size_t k) { return k >= disabled.size() || !disabled[k]; };
	for (size_t k = 0; k < keys.size() && k < items.size(); k++) {
		if (usable(k) && !keys[k].empty() && keys[k] == want) return static_cast<int>(k);
	}
	for (size_t k = 0; k < items.size(); k++) {
		if (!usable(k) || !LeagueMapTextMatches(items[k], id)) continue;
		byText = true;
		return static_cast<int>(k);
	}
	return -1;
}

const char *LeagueMapName(const std::string &id)
{
	const MapId *m = Find(Lower(id));
	return m ? m->name : nullptr;
}

std::vector<std::string> LeagueMapTypes()
{
	std::vector<std::string> out;
	for (size_t i = 0; i < kTypeCount; i++) out.push_back(kMaps[i].id);
	return out;
}

std::vector<std::string> LeagueMapSizesOf(const std::string &type)
{
	std::vector<std::string> out;
	bool known = false;
	for (size_t i = 0; i < kTypeCount; i++) known = known || type == kMaps[i].id;
	if (!known) return out;
	bool small = false;
	for (const char *t : kSmallTypes) small = small || type == t;
	for (const char *s : kSizes) {
		if (small || strcmp(s, "small") != 0) out.push_back(s);
	}
	return out;
}

bool LeagueMapValid(const std::string &type, const std::string &size)
{
	for (const std::string &s : LeagueMapSizesOf(type)) {
		if (s == size) return true;
	}
	return false;
}
