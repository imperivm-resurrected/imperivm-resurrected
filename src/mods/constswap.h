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

// FullHotReload (experimental): a ruleset made only of patches to files under data/ (xml, ini, vs). Those are read by the game when it
// starts, so the DLL makes the game read them again (game/constreload.cpp). Anything that is not a patch of data/ (paks, loose files,
// hidden.txt, rle.mmp.tail, civs, specials, modes, options, events, rules, translations, patches to units/ or buildings/...) keeps the restart.
bool FullSwapLayerOk(const std::vector<std::string> &files);

// Whether the text of a const.ini can be loaded: it has a [GamePlay] section (every constant the game reads is in it). A file that
// cannot be read, or an empty one, would leave the game's table empty.
bool ConstIniHasGamePlay(const std::string &text);

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to);

// The same with FullHotReload: a pair that is not const-only is kFull when both rulesets pass FullSwapLayerOk and full is true.
ConstSwap SwapDecide(bool sameRuleset, bool full, const std::vector<std::string> &from, const std::vector<std::string> &to);
