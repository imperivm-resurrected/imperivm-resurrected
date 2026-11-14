// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

#include "core/league.h"
#include "ui/uikit.h"

// The league next to names and on the screens: a player's badge to the right of their name (the patron's laurel stays on the left,
// and the name keeps its colour), the big emblems and the card to link the account.

constexpr int kLeagueBadgeSize = 20, kLeagueBadgeGap = 6;

// The badge in a square of `size`. It is a pixel drawing, so it is drawn at the whole scale nearest the screen's and centred.
void DrawLeagueBadge(ui::Painter &p, int x, int y, int size, const LeagueBadge &badge);
// The emblem of a rank drawn from its shapes, for 48 px and up; None is the unranked outline with its question mark. `alpha` (0-255)
// fades it, for the result's animation.
void DrawLeagueEmblem(ui::Painter &p, int x, int y, int size, LeagueTier tier, int alpha = 255);
// A colour laid over what is there at `alpha` (0-255): glows, flashes and the faint part of a bar.
void PaintTint(ui::Painter &p, int x, int y, int w, int h, COLORREF c, int alpha);
// A ring `width` thick around (cx, cy) that runs clockwise from the top over `fraction` of the turn.
void PaintArc(ui::Painter &p, int cx, int cy, int radius, int width, COLORREF c, float fraction);
// The rays behind a new rank's emblem, `radius` long, at `alpha`.
void PaintRays(ui::Painter &p, int cx, int cy, int radius, COLORREF c, int alpha);
// A tournament's phalera in a square of `size`: the plain one up to 47 px, the champion's one with rivets from 48.
void DrawLeaguePhalera(ui::Painter &p, int x, int y, int size, LeagueMetal metal);
COLORREF LeagueMetalInk(LeagueMetal metal);
// What a badge takes after a name, gap included; 0 without one. Take it off the name's box before drawing the name.
int LeagueBadgeRoom(const LeagueBadge &badge);
// The badge 6 px after a name drawn in the box (nameX, y, nameW, h) with the painter's current font, centred on the box's height,
// with its tooltip under the mouse.
void DrawBadgeAfterName(ui::Painter &p, int nameX, int y, int nameW, int h, const std::string &name, const LeagueBadge &badge, int mx, int my);
// The tooltip of the badge under the mouse, drawn after everything else.
void PaintLeagueBadgeTip(ui::Painter &p, int screenW, int screenH);
// "Centurion II", translated.
std::string LeagueRankLabel(LeagueTier tier, int division);
// A map type or size of the league by its id ("mountain_passes" -> "Montañoso"), translated; an id it doesn't know, as words.
std::string LeagueMapText(const std::string &id);
COLORREF LeagueTierInk(LeagueTier tier);

// The actions of the screen that shows the account card. With `badge`, the linked account also offers the badge next to the name;
// its argument is the position in LeagueBadgeChoices.
struct LeagueLinkActions {
	int start = 0, open = 0, cancel = 0, unlink = 0, badge = 0;
};
// The badge next to the name, picked among the player's own (the choice at `index` of LeagueBadgeChoices).
void LeagueChooseBadgeAt(int index);
// The league account from (x, y), `w` wide: what linking is for and the button, the code and the website's address while it waits,
// or the linked account and its rank. Returns how tall it came out.
int PaintLeagueAccount(ui::Painter &p, int x, int y, int w, const LeagueLinkActions &actions, int mx, int my);
