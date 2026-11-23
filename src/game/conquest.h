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
// The events of the map's own mouse handler (CVXUIConquestMap::OnMouse): a press picks the territory under it, a double click plays it.
inline constexpr int kConquestMouseDown = 0x111, kConquestMouseUp = 0x112, kConquestMouseDouble = 0x113;
inline constexpr uint8_t kConquestNoTerritory = 0xff;

enum ConquestStanding { kStandingOwned, kStandingConquerable, kStandingOutOfReach, kStandingCount };

// 0xAARRGGBB, with the colour key of the game's bitmap turned transparent.
struct ConquestImage {
	int w = 0, h = 0;
	std::vector<uint32_t> argb;
};

// Which territory each pixel of the map belongs to (kConquestNoTerritory for none), from its territories.bmp.
struct ConquestRegions {
	int w = 0, h = 0;
	std::vector<uint8_t> territory;
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
	// The map as the game paints it (global.bmp tinted by who holds each territory), without the outline of the chosen one.
	std::shared_ptr<const ConquestImage> map;
	std::shared_ptr<const ConquestRegions> regions;
};

// Game thread only.
ConquestSnapshot ConquestRead();
void ConquestPress(int id);
void ConquestSetDifficulty(int index);
// Hands a mouse event to the map's own handler at a pixel of its picture, as a click on the game's map would.
void ConquestMapMouse(int event, int x, int y);
