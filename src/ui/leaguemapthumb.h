// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ui {
struct Painter;
}

// The picture of a league map where the screens name it. The game has none of its random map types (data/RandomMap/<id>.rm.xml), so
// each type gets a clean drawing of its kind of land, coasts, islands, an inland sea or passes, with the two starting places.

// BGRA, top row first, `px` by `px`; a type it does not know (or none) is land crossed by rivers.
std::vector<uint32_t> LeagueMapThumbPixels(const std::string &type, int px);
// The drawing in a square of `size` with its frame, at the painter's scale.
void PaintLeagueMapThumb(ui::Painter &p, int x, int y, int size, const std::string &type);
