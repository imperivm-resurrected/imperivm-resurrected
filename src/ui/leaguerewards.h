// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>

#include "core/league.h"
#include "core/leagueseasons.h"
#include "ui/uikit.h"

// What a season leaves next to a player, drawn: the avatar frame (3-clasificatoria-pantallas.md §3.7), the title, and the showcase and
// the choices of the profile.

// How far inside a frame's square the avatar goes.
constexpr int kLeagueFrameInset = 6;
// A season's frame over a square of `size`: a border in the rank's colours, a lozenge on each corner and the plate "T · I" on the
// bottom edge, which hangs 7 px below the square. Nothing without a frame.
void PaintLeagueFrame(ui::Painter &p, int x, int y, int size, const LeagueFrame &frame);
// The player's Steam picture (their initials without it) in a square of `size`, inside their frame when they wear one.
void PaintFramedAvatar(ui::Painter &p, int x, int y, int size, uint64_t steamId, const std::string &name, const LeagueFrame &frame);
// "Tribune frame" and the season's title, "Tribune of Trial of Arms", translated; no title without its rank.
std::string LeagueFrameLabel(const LeagueFrame &frame);
std::string LeagueTitleLabel(const LeagueFrame &title);
// The colour a title or a frame's name is written in.
COLORREF LeagueFrameInk(const LeagueFrame &frame);
// The frame (or the title) a season gave the player, with its rank and its season's name; none when that season gave none.
LeagueFrame LeagueOwnCosmetic(const LeagueMe &me, int season);
// The vexillum of a season as a badge: the rank's, or the Founder's in the test season.
LeagueBadge LeagueVexillumBadge(LeagueTier tier, bool founder, int season);

// The profile's showcase: every vexillum and phalera the player won, in squares of `cell` from (x, y) along `w`, the one worn marked.
// A click wears it next to the name (`action`, with its place in LeagueBadgeChoices). Returns how many fit.
int PaintLeagueShowcase(ui::Painter &p, int x, int y, int w, int cell, const LeagueMe &me, int action, int mx, int my);
// The frame and the title the player wears, picked among those won: `frameAction` and `titleAction` with the season (-1: none).
// Returns how tall it came out.
int PaintLeagueCosmeticChoices(ui::Painter &p, int x, int y, int w, const LeagueMe &me, int frameAction, int titleAction, int mx, int my);
