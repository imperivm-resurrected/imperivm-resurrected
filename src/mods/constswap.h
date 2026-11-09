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
	kReload   // only data/const.ini changes
};

// files: everything a ruleset folder holds (paths relative to the folder, '/' separators, documents left out). A ruleset can be
// loaded again only if it holds nothing but patches/data/const.ini, or nothing at all (the game's own rules, a package without a
// layer). Any other file, even one the two rulesets share, could differ or need the game to read it again, so it keeps the restart.
bool ConstSwapLayerOk(const std::vector<std::string> &files);

ConstSwap ConstSwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to);
