// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <array>
#include <cstdint>

constexpr int kTeamColorPalette = 12, kTeamColorSlots = 8;

// "Team colours" of a room: each team takes the palette's colours nearest its own hue (team 1 red, 2 blue, 3 green, 4 yellow), one
// round at a time so that every team gets its nearest first; the players without a team then take the colours furthest from those
// already given. The hues come from the palette itself (0xRRGGBB, [PlayerColors] of const.ini, which mods change). `team[i]` is 1 to 4
// or 0, `used[i]` whether slot i has a player. The palette index of each used slot, all different; -1 for the others.
std::array<int, kTeamColorSlots> TeamColorsFor(const uint32_t (&palette)[kTeamColorPalette], const int (&team)[kTeamColorSlots],
	const bool (&used)[kTeamColorSlots]);
