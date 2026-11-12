// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/savename.h"

#include <cstring>

namespace {

size_t TitleDash(const std::string &baseName, const std::vector<std::string> &titles, int &title)
{
	size_t best = std::string::npos, bestLen = 0;
	for (size_t i = 0; i < titles.size(); i++) {
		const std::string &t = titles[i];
		if (t.empty() || t.size() <= bestLen || baseName.size() < t.size() + 2) continue;
		const size_t at = baseName.size() - t.size();
		if (baseName[at - 1] != '-' || _strnicmp(baseName.c_str() + at, t.c_str(), t.size()) != 0) continue;
		best = at - 1;
		bestLen = t.size();
		title = static_cast<int>(i);
	}
	return best;
}

size_t GuessDash(const std::string &baseName)
{
	size_t last = std::string::npos;
	for (size_t i = baseName.size() - (baseName.empty() ? 0 : 1); i-- > 1;) {
		if (baseName[i] != '-') continue;
		if (last == std::string::npos) last = i;
		const bool spaced = baseName[i - 1] == ' ' && baseName[i + 1] == ' ';
		if (!spaced) return i;
	}
	return last;
}

}

SaveNameParts SaveNameSplit(const std::string &baseName, const std::vector<std::string> &titles)
{
	SaveNameParts out;
	size_t dash = TitleDash(baseName, titles, out.title);
	if (dash == std::string::npos) dash = GuessDash(baseName);
	if (dash == std::string::npos) {
		out.name = baseName;
		return out;
	}
	out.name = baseName.substr(0, dash);
	out.campaign = baseName.substr(dash + 1);
	return out;
}
