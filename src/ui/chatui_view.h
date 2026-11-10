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
	// The type of a ping (ChatPingType), or -1.
	int ping = -1;
	int alpha = 255;
};

// The menu of ping types, centred on the point where the button went down; hover is the type under the mouse or -1.
struct PingMenuView {
	int x = 0, y = 0;
	int hover = -1;
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
void PaintPingMenu(uint32_t *buf, int w, int h, const PingMenuView &v);
// How far the mouse has to move from the press before it points at a type, in game pixels.
int PingMenuDeadZone(int w, int h);

}
