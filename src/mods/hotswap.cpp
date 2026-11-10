// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/hotswap.h"

#include "addr/net.h"
#include "addr/world.h"
#include "mods/savelib.h"

#include <cctype>
#include <cstring>

namespace {
std::string Normal(std::string s)
{
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}
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

unsigned HotSwapKindOf(const std::string &raw)
{
	const std::string f = Normal(raw);
	const std::string ext = Extension(f);
	if (f == "hidden.txt") return kHotHidden;
	// A file in a subfolder or with no name is not one of these.
	if (StartsWith(f, "patches/data/") && f.size() > 13 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kHotPatch;
	if (StartsWith(f, "data/") && f.size() > 5 && (ext == ".xml" || ext == ".ini" || ext == ".vs")) return kHotLoose;
	if (StartsWith(f, "modes/") && f.size() > 6 && f.find('/', 6) == std::string::npos && ext == ".json") return kHotModes;
	return kHotOther;
}

unsigned HotSwapKinds(const std::vector<std::string> &files)
{
	unsigned kinds = kHotNone;
	for (const std::string &f : files) kinds |= HotSwapKindOf(f);
	return kinds;
}

bool HotSwapLayerOk(const std::vector<std::string> &files)
{
	return (HotSwapKinds(files) & kHotOther) == 0;
}

std::string HotSwapKindsText(unsigned kinds)
{
	std::string out;
	auto add = [&](unsigned bit, const char *name) {
		if (kinds & bit) out += (out.empty() ? "" : ", ") + std::string(name);
	};
	add(kHotPatch, "patches of data/");
	add(kHotLoose, "files in data/");
	add(kHotModes, "modes");
	add(kHotHidden, "hidden.txt");
	add(kHotOther, "other files");
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

HotSwap SwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to)
{
	if (sameRuleset) return HotSwap::kNothing;
	return HotSwapLayerOk(from) && HotSwapLayerOk(to) ? HotSwap::kReload : HotSwap::kRestart;
}

GameKind GameKindOfVtable(uintptr_t vtable)
{
	if (vtable == addr::kGameSingleVtable) return GameKind::kSkirmish;
	if (vtable == addr::kMultiplayerVtable) return GameKind::kMultiplayer;
	if (vtable == addr::kGameAdvVtable) return GameKind::kAdventure;
	if (vtable == addr::kGameConqVtable) return GameKind::kConquest;
	if (vtable == addr::kGameEditVtable) return GameKind::kEditor;
	return GameKind::kUnknown;
}

const char *GameKindName(GameKind kind)
{
	switch (kind) {
	case GameKind::kSkirmish: return "single-player";
	case GameKind::kMultiplayer: return "multiplayer";
	case GameKind::kAdventure: return "adventure";
	case GameKind::kConquest: return "conquest";
	case GameKind::kEditor: return "editor";
	default: return "unknown";
	}
}

bool SaveKindSwitchesInPlace(int saveKind)
{
	return saveKind == kSaveStrategy || saveKind == kSaveAdventure || saveKind == kSaveConquest;
}
