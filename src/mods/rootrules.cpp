// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/rootrules.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "mods/volumes.h"

namespace mods_impl {

namespace {
std::string Normal(std::string s)
{
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	while (!s.empty() && s[0] == '/') s.erase(0, 1);
	return s;
}

bool Starts(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

bool Ends(const std::string &s, const char *suffix)
{
	const size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
}

std::string RootGamePath(const std::string &modPath)
{
	return VolumeRealPath(Normal(modPath));
}

bool RootIsMenuSkin(const std::string &p)
{
	if (Starts(p, "data/interface/menu/") || Starts(p, "ui/menu/")) return true;
	if (!Starts(p, "currentlang/") || p.find('/', 12) != std::string::npos) return false;
	const std::string name = p.substr(12);
	if (name == "menubackground.bmp" || name == "menu_16_9.bmp" || name == "menu_4_3.bmp" || name == "tips.xml" || name == "help.xml") return true;
	return Starts(name, "logo") && Ends(name, ".bmp");
}

std::vector<std::string> RootGlobalFiles(const std::vector<std::string> &rootLoose, const GameFileSet &game)
{
	std::vector<std::string> out;
	for (const std::string &raw : rootLoose) {
		const std::string real = RootGamePath(raw);
		if (game.count(real) && !RootIsMenuSkin(real)) out.push_back(Normal(raw));
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

bool RootGlobalApplies(const std::string &packFolder, const std::string &rulesetPack)
{
	return !rulesetPack.empty() && Normal(packFolder) == Normal(rulesetPack);
}

std::unordered_set<std::string> RootRefusals(const std::vector<std::string> &global)
{
	std::unordered_set<std::string> out;
	for (const std::string &p : global) {
		out.insert(Normal(p));
		out.insert(RootGamePath(p));
	}
	return out;
}

bool RootRefuses(const std::unordered_set<std::string> &refusals, const std::string &asked)
{
	return !refusals.empty() && refusals.count(Normal(asked)) != 0;
}

}
