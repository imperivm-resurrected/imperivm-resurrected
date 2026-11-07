// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

#include "ui/uikit.h"

#include "game/greatbattles.h"
#include "ui/listbox.h"

namespace battlesui {

enum Action { kActNone = 0, kActTab, kActRow, kActDifficulty, kActStart, kActIntro, kActBack };

inline int RowArg(int list, int index) { return list << 16 | index; }

struct View {
	BattlesSnapshot snap;
	int mouseX = -10000, mouseY = -10000;
	int listScroll = 0, descScroll = 0;
	// What the player has won, for a later version: each one is drawn only when it has text.
	std::string badge, progress;
};

struct PaintOut {
	ui::ListGeom list, desc;
};

// The screen fills the frame: its height is always 768 and its width follows the frame's shape.
constexpr int kScreenH = 768;
constexpr int kListRowH = 38, kDescLineH = 20;

float ScreenScale(int frameW, int frameH);
int ScreenWidth(int frameW, int frameH);
// Where a row starts inside the list's content, for keeping the chosen one in view; -1 when it is not there.
int RowTop(const BattlesSnapshot &s, int list, int index);
void PaintBattlesScreen(ui::Painter &p, int screenW, const View &v, PaintOut &out);

}
