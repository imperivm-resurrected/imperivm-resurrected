// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The map of a conquest (ConquestGame.ini) stays the game's: these read its state and press its own controls.
inline constexpr int kConquestStart = 0x1005, kConquestSave = 0x1006, kConquestQuit = 0x1007, kConquestSelect = 0x1008;

enum ConquestStanding { kStandingOwned, kStandingConquerable, kStandingOutOfReach, kStandingCount };

// 0xAARRGGBB, with the colour key of the game's bitmap turned transparent.
struct ConquestImage {
	int w = 0, h = 0;
	std::vector<uint32_t> argb;
};

struct ConquestTerritory {
	std::string name, description, bonus;
	int standing = kStandingOutOfReach;
	bool disabled = false;
	std::shared_ptr<const ConquestImage> shield;
};

struct ConquestButton {
	bool visible = false, enabled = false;
};

struct ConquestSnapshot {
	bool open = false;
	bool choosing = false;
	std::vector<ConquestTerritory> territories;
	std::vector<int> path;
	int selected = -1;
	std::vector<std::string> difficulties;
	int difficulty = -1;
	ConquestButton start, select, save, quit;
	// What the map paints over a mid-tone land for each standing, 0xRRGGBB.
	uint32_t colors[kStandingCount] = {0xb0a06e, 0xb08280, 0x968c78};
	// The game's own question (quit?) is drawn over this rectangle, in screen pixels.
	bool confirm = false;
	int confirmRect[4] = {};
};

// Game thread only.
ConquestSnapshot ConquestRead();
void ConquestPress(int id);
void ConquestSetDifficulty(int index);
