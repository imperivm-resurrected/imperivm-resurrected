// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/conquest.h"
#include "ui/uikit.h"

namespace conquestui {

enum Action { kActNone = -1, kActNative = 1, kActDifficulty, kActScroll, kActOk };

struct View {
	ConquestSnapshot game;
	int mouseX = -1000, mouseY = -1000;
	int scroll = 0;
	bool victory = false;
	std::string victoryText;
};

// Where the last paint put things, in screen pixels.
struct Painted {
	std::vector<ui::Hit> hits;
	RECT panel = {}, path = {}, legend = {}, ok = {};
	int maxScroll = 0;
};

// Paints over the game's frame: the map stays the game's picture and keeps its input.
void PaintConquest(uint32_t *buf, int w, int h, const View &v, int pressAction, int pressArg, Painted &out);

}
