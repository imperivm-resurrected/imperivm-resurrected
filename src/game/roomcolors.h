// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The colours a room's players picked outside the game's twelve (0xRRGGBB per slot of the room, 0 for the game's own). The room keeps
// setting them while it is open; the match that starts next takes them once (RoomColorsTake) and paints those players with them on
// every computer. Only the pictures change: the game's sync hash does not read the players' colours.
constexpr int kRoomColorSlots = 8;
void RoomColorsSet(const uint32_t rgb[kRoomColorSlots]);
// The colours set in the last two minutes, once; false when there are none.
bool RoomColorsTake(uint32_t rgb[kRoomColorSlots]);
// Too close to tell apart on the map: two players may not pick colours this near.
bool RoomColorsTooClose(uint32_t a, uint32_t b);
