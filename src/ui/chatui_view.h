// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game/chatlog.h"
#include "ui/textedit.h"
#include "ui/uikit.h"

namespace chatui {

struct Line {
	std::string clock;
	int scope = kChatAll;
	std::string name, toName, text;
	COLORREF color = RGB(232, 216, 160);
	bool location = false;
	int alpha = 255;
};

struct View {
	bool typing = false;
	// Oldest first. Idle, only the lines still on screen, each with its fade.
	std::vector<Line> lines;
	// Rows scrolled up from the newest one, while typing.
	int scroll = 0;
	int targetScope = kChatAll;
	std::string targetName;
	COLORREF targetColor = RGB(232, 216, 160);
	std::string text;
	ui::EditState edit;
	bool withLocation = false;
	// Bottom-left corner of the input bar and the width there is for it, in game pixels.
	int left = 0, bottom = 0, maxWidth = 0;
	int mouseX = -10000, mouseY = -10000;
};

enum Action { kActNone = 0, kActChip, kActPin, kActField, kActHistory };

struct Painted {
	std::vector<ui::Hit> hits;
	RECT panel = {};
	int maxScroll = 0;
	int pageRows = 1;
};

float Scale(int w, int h);
void PaintChat(uint32_t *buf, int w, int h, const View &v, Painted &out);

}
