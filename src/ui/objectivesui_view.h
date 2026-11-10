// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <vector>

#include "core/objectivelog.h"
#include "ui/uikit.h"

namespace objectivesui {

struct View {
	std::vector<ObjectiveEntry> entries;
	int selected = -1;
	int listScroll = 0, textScroll = 0;
	// Bring the selected row into the list once.
	bool revealSelected = false;
	int mouseX = -10000, mouseY = -10000;
};

struct PaintOut {
	// In the units of the design, and the scrolls the painting kept to.
	int listH = 0, listViewH = 0, textH = 0, textViewH = 0;
	int listScroll = 0, textScroll = 0;
	// Where the wheel scrolls each part, in pixels of the frame.
	RECT list = {}, text = {};
};

enum Action { kActNone = 0, kActSelect, kActShowOnMap, kActClose };

constexpr int kScreenW = 960, kScreenH = 620;

void PaintObjectivesScreen(ui::Painter &p, const View &v, PaintOut &out);

}
