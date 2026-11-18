// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/uikit.h"

namespace mainmenuui {

enum Action { kActNone, kActMain, kActSub };

// Milliseconds from the performance counter: GetTickCount moves in steps of 15.6 ms, which made the submenu's slide jerk.
inline unsigned MenuClockMs()
{
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	return static_cast<unsigned>(now.QuadPart * 1000 / freq.QuadPart);
}

// How long the submenu's rows take to slide in, the last one after the others.
constexpr unsigned kMenuAnimMs = 400;

// The groups that open a submenu, as the id of their row; and the rows of the multiplayer group, which open its screen on a section.
constexpr int kGroupSingle = 0, kGroupMulti = -1;
constexpr int kMultiRooms = -10, kMultiRanked = -11, kMultiTournaments = -12;
inline bool IsGroup(int id) { return id == kGroupSingle || id == kGroupMulti; }

struct Item {
	// The game's button it clicks; a group (kGroupSingle, kGroupMulti) opens its submenu; a kMulti* row opens the multiplayer screen.
	int id = 0;
	// Already translated: the game's own text, or the mod's through Tr().
	std::string label;
	bool enabled = true;
	// Content packs with an update.
	int badge = 0;
};

struct View {
	std::vector<Item> main, sub;
	std::string groupCaption, tagline;
	// The group whose submenu `sub` is, and both groups' rows.
	int openGroup = kGroupSingle;
	std::vector<Item> subSingle, subMulti;
	bool subOpen = false;
	// When the submenu last opened or closed (GetTickCount), for its animation; 0: no animation.
	unsigned subChangedAt = 0;
	// Keyboard focus, in the main list or (with the submenu open) in the submenu.
	int focusMain = -1, focusSub = -1;
	bool keyboard = false;
	bool interactive = false;
	int mouseX = -10000, mouseY = -10000;
};

// The isotype and the logotype from imperivm-resurrected\brand, read once: false when either is missing or unreadable.
bool LoadBrandArt();
void Paint(uint32_t *buf, int w, int h, const View &v, std::vector<ui::Hit> &hits);

}
