// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

#include "core/league.h"
#include "ui/uikit.h"

// The mastery of the civilizations on the league's screens: the level's badge, its bar and its tooltip. The league counts the wins and
// keeps the thresholds; these only draw what it sends.

// A civilization by the key the league keeps (a base one's "ImperialRome", a mod civilization's id), as the game names it now; a key it
// doesn't know, as words.
std::string LeagueCivName(const std::string &id);
// The level's name, translated.
std::string LeagueMasteryLabel(int level);
// The metal of a level: Tiro iron, Legionario bronze, Veterano silver, Evocato and Maestro gold.
COLORREF LeagueMasteryInk(int level);
// The level's badge in a square of `size`: a lozenge of its metal; Maestro's smaller, inside the laurel wreath.
void DrawMasteryBadge(ui::Painter &p, int x, int y, int size, int level);
// The bar from the level to the next one, full at Maestro; the last `seasonWins` of the way, those of this season, in the lighter
// shade.
void PaintMasteryBar(ui::Painter &p, int x, int y, int w, int h, const LeagueMastery &m, int seasonWins = 0);
// "3 more wins for Veteran"; at Maestro, the wins so far ("73 ranked wins"), which keep counting.
std::string LeagueMasteryNextText(const LeagueMastery &m);
// The tooltip of a civilization's mastery drawn in a square of `size`, when the mouse is over it; drawn by PaintMasteryTip after
// everything else.
void HoverMastery(ui::Painter &p, int x, int y, int size, const std::string &civ, const LeagueMastery &m, int mx, int my);
void PaintMasteryTip(ui::Painter &p, int screenW, int screenH);
