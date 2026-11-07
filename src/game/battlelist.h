// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct BattleImage {
	int w = 0, h = 0;
	std::vector<uint32_t> bgra;
};

// Uncompressed 8, 16, 24 and 32-bit bitmaps, the kinds the game ships next to its adventures.
bool DecodeBattleBmp(const unsigned char *data, size_t size, BattleImage &out);

struct BattleTitle {
	std::string name, year;
};

// "El sitio de Alesia - 52 a.C." gives the name and the year; a title without a year at its end stays whole.
BattleTitle SplitBattleTitle(const std::string &title);

// The intro of a great battle, as a pattern for the files of the Movies folder: "adventures/GreatBattles/1_Great_Battles_Zama" gives
// "*_Intro_Great_B_Zama.avi". Empty for the other adventures, which have no intro of their own.
std::string BattleIntroPattern(const std::string &adventurePath);

// The adventure's text keeps the "\n" of its file as two characters.
std::string BattleDescriptionText(const std::string &raw);
