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

struct Item {
	// The game's button it clicks; 0 for the group that opens the submenu.
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
