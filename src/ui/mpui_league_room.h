// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "core/leagueroomoptions.h"
#include "core/leagueslot.h"
#include "game/gameroom.h"

namespace mpui_impl {
// The host of a ranked or cup room: one step towards `want`, every option of the room but the map (the ranked and cup code choose it).
// Locks the rules, sites and speed in the room (GameRoomLockOptions) and keeps the player's own options aside; the changes sent are
// written into `g` too. True once the room holds everything it can take: an item the room does not have, or an option a content mode
// fixes, is logged once and counted as held. Under g_lock. A guest only keeps their own options aside.
bool LeagueRoomApply(const LeagueRoomOptions &want, GameRoomSnapshot &g, uint64_t room, const char *what);
// The room holds what LeagueRoomApply last asked for in this Steam room; true when it asked nothing here, and for a guest.
bool LeagueRoomHeld(const GameRoomSnapshot &g, uint64_t room);

// The player's slot in a room that stays under a league page (a cup's match page, the panel of a ranked match), kept as the page chose it.
struct LeagueSlotKeep {
	LeagueSlotTries tries;
	DWORD setAt = 0;
	// The player was made not ready to change the slot (the game keeps a ready player's row as it is).
	bool unreadied = false;
};
enum class LeagueSlotStep {
	// Something was sent, or waits its turn.
	Working,
	// The player was made not ready to change the slot: they are to be made ready again once it is done.
	Unreadied,
	// The slot has what the page chose, or all the room will take of it.
	Done,
};
// Under g_lock, on the game thread, with the player's slot in g_s.game (used, the room not busy): one step towards the civilization `race`
// (a race of the game, -1 for the room's own) and the colour `color` (one of the twelve, kLeagueOwnColor with `rgb`, -1 for the room's own).
// `what` and `match` name the match in the log ("tournament", "ranked").
LeagueSlotStep LeagueSlotKeepStep(LeagueSlotKeep &k, int race, int color, uint32_t rgb, DWORD now, const char *what, int64_t match);
}
