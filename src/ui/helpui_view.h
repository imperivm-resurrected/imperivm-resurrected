// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/helpdoc.h"
#include "game/battlelist.h"
#include "ui/textedit.h"
#include "ui/uikit.h"

namespace helpui {

// Everything the screen shows, read once when the game's help opens.
struct Content {
	HelpDoc doc;
	HelpIndex index;
	std::map<int, HelpUnitStats> stats;
	std::map<std::string, int> classTopic;
	HelpCounters counters;
};

// Lower-case path -> picture; null when the game has no such file.
using ImageMap = std::map<std::string, std::shared_ptr<const BattleImage>>;

struct View {
	std::shared_ptr<const Content> content;
	std::shared_ptr<const ImageMap> images;
	HelpPlace place;
	bool canBack = false, canForward = false;
	std::string query;
	ui::EditState edit;
	bool searchFocused = false;
	std::shared_ptr<const std::vector<HelpHit>> hits;
	int scroll = 0, navScroll = 0;
	// Bring the row of the place into the side bar once.
	bool revealNav = false;
	// Rows of the side bar opened or closed by hand, as group * 1000 + item.
	std::set<int> toggled;
	int mouseX = -10000, mouseY = -10000;
};

struct PaintOut {
	// In the units of the design, and the scrolls the painting kept to.
	int contentH = 0, viewH = 0, navH = 0, navViewH = 0;
	int scroll = 0, navScroll = 0;
	// Where the wheel scrolls each part, in pixels of the frame.
	RECT content = {}, nav = {};
	std::vector<HelpPlace> targets;
	std::vector<std::string> web;
	// Pictures the page asked for that are not loaded yet.
	std::vector<std::string> wanted;
};

enum Action { kActNone = 0, kActGo, kActBack, kActForward, kActClose, kActSearch, kActClearSearch, kActToggle, kActWeb };

constexpr int kScreenW = 1060, kScreenH = 720;

std::string ImageKey(const std::string &path);
// Turns the transparent colour of a bitmap of the game into transparent pixels.
void KeyOut(BattleImage &img);
void PaintHelpScreen(ui::Painter &p, const View &v, PaintOut &out);

}
