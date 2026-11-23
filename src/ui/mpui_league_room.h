// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

#include "core/leagueroomoptions.h"
#include "game/gameroom.h"

namespace mpui_impl {
// The host of a ranked or cup room: one step towards `want`, every option of the room but the map (the ranked and cup code choose it).
// Locks the rules, sites and speed in the room (GameRoomLockOptions) and keeps the player's own options aside; the changes sent are
// written into `g` too. True once the room holds everything it can take: an item the room does not have, or an option a content mode
// fixes, is logged once and counted as held. Under g_lock. A guest only keeps their own options aside.
bool LeagueRoomApply(const LeagueRoomOptions &want, GameRoomSnapshot &g, uint64_t room, const char *what);
// The room holds what LeagueRoomApply last asked for in this Steam room; true when it asked nothing here, and for a guest.
bool LeagueRoomHeld(const GameRoomSnapshot &g, uint64_t room);
}
