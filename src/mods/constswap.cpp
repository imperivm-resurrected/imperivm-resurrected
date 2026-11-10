// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/constswap.h"

#include <cctype>
#include <cstring>

namespace {
std::string Normal(std::string s)
{
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}
}

bool ConstSwapLayerOk(const std::vector<std::string> &files)
{
	for (const std::string &f : files) {
		if (Normal(f) != "patches/data/const.ini") return false;
	}
	return true;
}

namespace {
bool StartsWith(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

std::string Extension(const std::string &f)
{
	const size_t dot = f.find_last_of('.'), slash = f.find_last_of('/');
	return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : f.substr(dot);
}
}

unsigned FullSwapKindOf(const std::string &raw)
{
	const std::string f = Normal(raw);
	const std::string ext = Extension(f);
	if (f == "hidden.txt") return kFullHidden;
	// A file in a subfolder or with no name is not one of these.
	if (StartsWith(f, "patches/data/") && f.size() > 13 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kFullPatch;
	if (StartsWith(f, "data/") && f.size() > 5 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kFullLoose;
	if (StartsWith(f, "modes/") && f.size() > 6 && f.find('/', 6) == std::string::npos && ext == ".json") return kFullModes;
	return kFullOther;
}

unsigned FullSwapKinds(const std::vector<std::string> &files)
{
	unsigned kinds = kFullNone;
	for (const std::string &f : files) kinds |= FullSwapKindOf(f);
	return kinds;
}

bool FullSwapLayerOk(const std::vector<std::string> &files)
{
	return (FullSwapKinds(files) & kFullOther) == 0;
}

std::string FullSwapKindsText(unsigned kinds)
{
	std::string out;
	auto add = [&](unsigned bit, const char *name) {
		if (kinds & bit) out += (out.empty() ? "" : ", ") + std::string(name);
	};
	add(kFullPatch, "patches of data/");
	add(kFullLoose, "files in data/");
	add(kFullModes, "modes");
	add(kFullHidden, "hidden.txt");
	add(kFullOther, "other files");
	return out.empty() ? "nothing" : out;
}

bool SaveSwitchesInPlace(bool plainGame, bool needsActivation, bool rulesetOnly, bool somethingExtra, bool switchQualifies)
{
	return plainGame && needsActivation && rulesetOnly && !somethingExtra && switchQualifies;
}

bool ConstIniHasGamePlay(const std::string &text)
{
	size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
	while (at < text.size()) {
		size_t end = text.find_first_of("\r\n", at);
		if (end == std::string::npos) end = text.size();
		size_t from = at, to = end;
		while (from < to && isspace(static_cast<unsigned char>(text[from]))) from++;
		while (to > from && isspace(static_cast<unsigned char>(text[to - 1]))) to--;
		if (Normal(text.substr(from, to - from)) == "[gameplay]") return true;
		at = end + 1;
	}
	return false;
}

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	if (sameRuleset) return ConstSwap::kNothing;
	return ConstSwapLayerOk(from) && ConstSwapLayerOk(to) ? ConstSwap::kReload : ConstSwap::kRestart;
}

ConstSwap SwapDecide(bool sameRuleset, bool full, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	const ConstSwap simple = ConstSwapDecide(sameRuleset, from, to);
	if (simple != ConstSwap::kRestart) return simple;
	return full && FullSwapLayerOk(from) && FullSwapLayerOk(to) ? ConstSwap::kFull : ConstSwap::kRestart;
}
