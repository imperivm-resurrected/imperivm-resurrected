// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/civs.h"

#include <string>
#include <vector>

bool CivSetPrefix(Civ &c, const std::string &prefix)
{
	if (prefix.empty() || prefix.size() > 4 || c.base < 0 || c.base >= 8) return false;
	for (char ch : prefix) {
		if (!(ch >= 'A' && ch <= 'Z') && !(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9')) return false;
	}
	if (prefix.size() > 1 && !(prefix[0] >= 'A' && prefix[0] <= 'Z')) return false;
	c.letterLow = prefix;
	for (char &ch : c.letterLow) {
		if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
	}
	c.letterUp = prefix;
	if (c.letterUp[0] >= 'a' && c.letterUp[0] <= 'z') c.letterUp[0] -= 'a' - 'A';
	c.ownLetter = c.letterLow != std::string(1, kGameCivPrefixes[c.base]);
	return true;
}

int CivPrefixClash(const Civ &c, const std::vector<const Civ *> &accepted)
{
	if (!c.ownLetter) return -1;
	for (int b = 0; b < 8; b++) {
		if (c.letterLow == std::string(1, kGameCivPrefixes[b])) return b;
	}
	for (const Civ *other : accepted) {
		if (c.letterLow == other->letterLow) return other->index;
	}
	return -1;
}
