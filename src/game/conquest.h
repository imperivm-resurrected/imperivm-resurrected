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
	std::string id, name, description, bonus;
	// The hardest difficulty the active profile won it at (1 to 3), 0 never.
	int laurel = 0;
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
	// The game's own question (quit?) is up; the game menu's screen draws it (gamemenuui).
	bool confirm = false;
	// The conquest's key for its laurels (ConquestKeyOf its file) and the hardest difficulty it was won whole at, 0 never.
	std::string key;
	int laurel = 0;
	// The map as the game paints it (global.bmp tinted by who holds each territory), without the outline of the chosen one.
	std::shared_ptr<const ConquestImage> map;
	// global.bmp before the tint, so a picture of the same map can be tinted alike.
	std::shared_ptr<const ConquestImage> base;
	std::shared_ptr<const ConquestRegions> regions;
};

// Game thread only.
ConquestSnapshot ConquestRead();
void ConquestPress(int id);
void ConquestSetDifficulty(int index);
// Game thread, from the victory of a battle (DeclareTeamVictory): false unless the game is a conquest's; then the conquest's key and
// the territory played, both empty when they cannot be read.
bool ConquestBattleOf(const char *game, std::string &key, std::string &territory);
// Game thread, when the game shows that the conquest is won: its laurel is the easiest of its territories'.
void ConquestNoteConquestWon();
// Hands a mouse event to the map's own handler at a pixel of its picture, as a click on the game's map would.
void ConquestMapMouse(int event, int x, int y);
