// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "core/creditstext.h"
#include "core/modcredits.h"
#include "net/patrons.h"
#include "ui/uikit.h"

namespace creditsui {

struct Patron {
	std::string name;
	PatronTier tier = PatronTier::None;
};

// Everything the screen shows, with the texts already in the player's language. It is built once when the screen opens and again
// only when the honour roll arrives.
struct Content {
	ModCredits mod;
	std::vector<Patron> patrons;
	CreditsDoc game;
	// Different for every Content made, so that the drawing knows when to lay the text out again.
	unsigned stamp = 0;
};

struct View {
	std::shared_ptr<const Content> content;
	// From the top of the content, in the units of the screen; the credits start again after PaintOut::period.
	double scroll = 0;
	bool paused = false, fast = false;
	int mouseX = -10000, mouseY = -10000;
};

struct PaintOut {
	double period = 0;
	int viewport = 0;
	// Where the text scrolls, in pixels of the frame.
	RECT column = {};
	// The scroll that brings each line of the index to the top.
	std::vector<double> entries;
};

enum Action { kActNone = 0, kActJump, kActPause, kActClose };

// The screen is as tall as the frame and as wide as its shape allows, with the game's picture showing at the sides.
constexpr int kScreenH = 768;

float ScreenScale(int frameW, int frameH);
int PanelWidth(int frameW, int frameH);
// Which line of the index the text is at.
int CurrentEntry(const std::vector<double> &entries, double scroll, double period, int viewport);
void PaintCreditsScreen(ui::Painter &p, int panelW, const View &v, PaintOut &out);

}
