// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "ui/widgets.h"

namespace ui {

// The screens that fill the whole screen are laid out in pixels of a 1920x1080 one: the scale follows the height.
struct ScreenView {
	float s = 1.0f;
	int w = 1920, h = 1080;
};
ScreenView ScreenViewFor(int screenW, int screenH);

// The engraved map of the brand under the full screens, cover-scaled to the canvas; false when the file is missing.
bool PaintBackdrop(Canvas &canvas);

// Starts a frame on the whole screen, in the units of `view`, with the engraved map (or the page colour without it) under everything.
bool BeginFullScreen(PanelRender &r, const ScreenView &view);

// A card of the full screens: the panel colour over the backdrop, a little see-through, with a hairline.
void PaintGlassCard(Painter &p, int x, int y, int w, int h);

// The full screens' type is larger than the shared set: UseScreenFonts puts it in the painter, UseSharedFonts gives the shared set back
// for the dialogs and menus over the screen, whose boxes are sized for it.
void UseScreenFonts(Painter &p);
void UseSharedFonts(Painter &p);

// Where the header of a full screen goes, in the units of ScreenView: the mockup's sizes on a 1920x1080 screen, tighter on a shorter one.
struct ScreenLayout {
	int w = 1920, h = 1080, margin = 64;
	bool roomy = true;
	int titleY = 0, titleH = 0, titleSize = 84, infoY = 0, infoH = 26;
	// Where the content starts under the header.
	int top = 0;
};
ScreenLayout ScreenLayoutFor(int w, int h);

constexpr int kHeaderButtonH = 44;

struct GhostButton {
	std::string label;
	int action = 0;
	bool arrow = false;
	bool enabled = true;
};

int GhostButtonWidth(Painter &p, const std::string &label, bool arrow);
// A button with only its outline, for the actions that leave a screen rather than act in it; `arrow` points back.
void PaintGhostButton(Painter &p, int x, int y, int w, int h, const std::string &label, int action, bool arrow, bool enabled, int mx, int my);
// The outlined buttons at the top right of a full screen, in the order given; returns where the leftmost one starts.
int PaintHeaderActions(Painter &p, const ScreenLayout &l, const std::vector<GhostButton> &buttons, int mx, int my);
// The big title at the top left, with the tabs on its baseline, all before `right`: a long title gives way to the tabs and ends in an
// ellipsis. It needs the screen's fonts (UseScreenFonts).
void PaintScreenTitle(Painter &p, const ScreenLayout &l, const std::string &title, const std::vector<TabSpec> &tabs, int right, int mx, int my);
// The caption line under the title.
void PaintScreenInfo(Painter &p, const ScreenLayout &l, const std::string &text, COLORREF color = pal::caption);

}
