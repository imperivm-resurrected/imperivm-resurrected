// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "net/patrons.h"
#include "ui/uikit.h"

#include <string>

struct PatronColors {
	COLORREF laurel, name, light;
};

PatronColors PatronColorsOf(PatronTier tier);
COLORREF PatronBlend(COLORREF from, COLORREF to, int percent);
// The laurel of the honour roll in a square of `size` at (x, y), then the name in the colour of the tier.
// On the top three a slanted light sweeps across both now and then, as on the website.
void DrawPatron(ui::Painter &p, int x, int y, int size, int nameX, int nameY, int nameW, int nameH, const std::string &name,
	PatronTier tier, UINT fmt);
int PatronLaurelSize(PatronTier tier);
// Behind the row of a top-three patron.
void DrawPatronRowGlow(ui::Painter &p, int x, int y, int w, int h, PatronTier tier);
// Click area and tooltip of a laurel. The tooltip is drawn by PaintPatronTip after everything else, so nothing covers it.
void PatronHotspot(ui::Painter &p, int x, int y, int size, const PatronInfo &info, int mx, int my, int action);
void PaintPatronTip(ui::Painter &p, int screenW, int screenH);
void OpenPatronPage();
