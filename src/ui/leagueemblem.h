// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

#include "core/league.h"

// The league's shields (RankedUiEmblemas): a 20-pixel drawing for next to a name and the full emblem for the big sizes. Pictures only:
// nothing here draws on the screen.

// 0xRRGGBB. `text` is the colour of the rank's name over the dark panels.
struct LeagueColors {
	uint32_t border, body, light, text;
};
LeagueColors LeagueTierColors(LeagueTier t);

// Premultiplied BGRA, top row first, as AlphaBlend takes it.
struct LeagueImage {
	int w = 0, h = 0;
	std::vector<uint32_t> px;
	uint32_t At(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
};

constexpr int kLeagueBadgeGrid = 20;

// The badge of a rank, pixel by pixel, `scale` screen pixels to each (20 * scale wide): crisp only at whole scales. None is the unranked
// outline.
LeagueImage LeagueBadgePixels(LeagueTier tier, int scale);
// A past season's vexillum, in the colours of the rank reached there.
LeagueImage LeagueVexillumPixels(LeagueTier tier, int scale);
// The phalera of a tournament trophy.
LeagueImage LeagueTrophyPixels(int scale);
// The emblem drawn from its shapes, smooth at any size: for 48 px and up (the screens show it at 160).
LeagueImage LeagueEmblemImage(LeagueTier tier, int size);
