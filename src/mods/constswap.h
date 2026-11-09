// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// What a change of ruleset needs. The game reads most of what a ruleset carries once per process, so the change restarts the game,
// except when the rulesets differ only in data/const.ini: that file can be loaded again between matches (game/constreload.cpp).
enum class ConstSwap {
	kNothing, // the same ruleset
	kRestart, // anything else
	kReload,  // only data/const.ini changes
	kFull     // only patches of data/ change, and the game can be made to read all its data again (FullHotReload)
};

// files: everything a ruleset folder holds (paths relative to the folder, '/' separators, documents left out). A ruleset can be
// loaded again only if it holds nothing but patches/data/const.ini, or nothing at all (the game's own rules, a package without a
// layer). Any other file, even one the two rulesets share, could differ or need the game to read it again, so it keeps the restart.
bool ConstSwapLayerOk(const std::vector<std::string> &files);

// FullHotReload (experimental): a ruleset made only of the kinds below. The game reads them when it starts, so the DLL makes the game read
// them again (game/constreload.cpp). Anything else (paks, art, sounds, texts, rle.mmp.tail, civs, specials, options, events, rules,
// patches to units/ or buildings/...) keeps the restart.
bool FullSwapLayerOk(const std::vector<std::string> &files);

// What kinds of files a ruleset has for FullHotReload, to say it in the log and to test each one: patches to data/ (kFullPatch), new or
// replacing files under data/ (kFullLoose: .xml, .ini, .vs; the game reads them when it starts or when the match does, from the loose
// layer that is mounted in place), the lobby's mode files modes/*.json (kFullModes) and hidden.txt (kFullHidden). kFullOther is anything else.
enum FullSwapKind : unsigned { kFullNone = 0, kFullPatch = 1, kFullLoose = 2, kFullModes = 4, kFullHidden = 8, kFullOther = 16 };
unsigned FullSwapKindOf(const std::string &file);
unsigned FullSwapKinds(const std::vector<std::string> &files);
std::string FullSwapKindsText(unsigned kinds);

// Whether the text of a const.ini can be loaded: it has a [GamePlay] section (every constant the game reads is in it). A file that
// cannot be read, or an empty one, would leave the game's table empty.
bool ConstIniHasGamePlay(const std::string &text);

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to);

// The same with FullHotReload: a pair that is not const-only is kFull when both rulesets pass FullSwapLayerOk and full is true.
ConstSwap SwapDecide(bool sameRuleset, bool full, const std::vector<std::string> &from, const std::vector<std::string> &to);
