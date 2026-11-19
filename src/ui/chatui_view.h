// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/league.h"
#include "game/chatlog.h"
#include "game/controls.h"
#include "net/patrons.h"
#include "ui/textedit.h"
#include "ui/uikit.h"

namespace chatui {

struct Line {
	std::string clock;
	int scope = kChatAll;
	std::string name, toName, text;
	COLORREF color = ui::pal::text;
	bool location = false;
	// Where it points, in the map's pixels (-1 without a location).
	int atX = -1, atY = -1;
	// The type of a ping (ChatPingType), or -1.
	int ping = -1;
	// Written by the mod for the reader only, laid out as ChatLocalKind says.
	bool local = false;
	int localKind = kLocalText;
	std::string key, keyHint;
	std::vector<ControlKey> keys;
	bool click = false;
	bool error = false;
	int alpha = 255;
	// The league badge after the sender's name; none when the options hide it.
	LeagueBadge badge;
	// The patron's laurel before the name, as the room's chat shows it.
	PatronTier patron = PatronTier::None;
};

// The menu of ping types, centred on the point where the button went down; hover is the type under the mouse or -1.
struct PingMenuView {
	// The menu's centre, moved in from the edges of the map; the press, where the mark goes.
	int x = 0, y = 0;
	int pressX = 0, pressY = 0;
	int hover = -1;
};

struct View {
	bool typing = false;
	// Oldest first. Idle, only the lines still on screen, each with its fade.
	std::vector<Line> lines;
	// Rows scrolled up from the newest one, while typing.
	int scroll = 0;
	// A line to show at the top instead (the start of the help), or -1.
	int topLine = -1;
	int targetScope = kChatAll;
	std::string targetName;
	COLORREF targetColor = ui::pal::text;
	std::string text;
	ui::EditState edit;
	bool withLocation = false;
	// Bottom-left corner of the input bar and the width there is for it, in game pixels.
	int left = 0, bottom = 0, maxWidth = 0;
	int mouseX = -10000, mouseY = -10000;
	bool dragging = false;
	ui::ScrollDrag bar;
};

// kActGoTo carries the place of a line's mark: x << 16 | y, in the map's pixels.
enum Action { kActNone = 0, kActChip, kActPin, kActField, kActHistory, kActGrip, kActGoTo };

constexpr int kBarHistory = 0;

struct Painted {
	std::vector<ui::Hit> hits;
	RECT panel = {};
	int maxScroll = 0;
	int pageRows = 1;
	// The scroll the panel was drawn with.
	int scroll = 0;
};

float Scale(int w, int h);
void PaintChat(uint32_t *buf, int w, int h, const View &v, Painted &out);
// The width the chat takes at this screen size, in game pixels, to keep it inside the screen.
int ChatWidth(int w, int h, int maxWidth);
void PaintPingMenu(uint32_t *buf, int w, int h, const PingMenuView &v);
// How far the mouse has to move from the press before it points at a type, in game pixels.
int PingMenuDeadZone(int w, int h);
// Where the menu of a press at "at" goes so that it stays inside area (left, top, right, bottom).
POINT PingMenuPlace(POINT at, const int *area, int w, int h);

}
