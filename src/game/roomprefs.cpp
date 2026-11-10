// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/roomprefs.h"

namespace {
const char kHex[] = "0123456789ABCDEF";

int HexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

bool PlainInIni(unsigned char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.' || c == '(' || c == ')' || c == ':';
}
}

// INI files go through the Windows code page and trim spaces, so anything else is written as %XX to come back byte for byte.
std::string RoomPrefEncode(const std::string &text)
{
	std::string out;
	for (const char ch : text) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (PlainInIni(c)) {
			out += ch;
			continue;
		}
		out += '%';
		out += kHex[c >> 4];
		out += kHex[c & 15];
	}
	return out;
}

std::string RoomPrefDecode(const std::string &text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '%' && i + 2 < text.size()) {
			const int high = HexValue(text[i + 1]), low = HexValue(text[i + 2]);
			if (high >= 0 && low >= 0) {
				out += static_cast<char>(high << 4 | low);
				i += 2;
				continue;
			}
		}
		out += text[i];
	}
	return out;
}

int RoomPrefItemIndex(const std::vector<std::string> &items, const std::string &text)
{
	for (size_t i = 0; i < items.size(); i++) {
		if (items[i] == text) return static_cast<int>(i);
	}
	return -1;
}

// The bits of a mod's option depend on which other options are loaded, so a value is only trusted by the option's id.
// Rules saved by a version without ids have no value for the season, which every version since then writes: those bits are read once.
unsigned RoomRulesFromSaved(const std::vector<ModOption> &options, bool haveSaved, unsigned saved, unsigned baseMask, const std::map<std::string, int> &values)
{
	unsigned rules = haveSaved ? saved & baseMask : 0;
	const bool oldRules = haveSaved && values.find(kSeasonOptionId) == values.end();
	for (const ModOption &o : options) {
		const auto it = values.find(o.id);
		const int value = it != values.end() ? it->second : oldRules ? ModOptionGet(o, saved) : o.def;
		rules = ModOptionSet(o, rules, value);
	}
	return rules;
}
