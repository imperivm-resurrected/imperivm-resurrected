// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <string>

#include "ui/mpui_internal.h"

namespace mpui_impl {
// wired to GameRoomSetTeam once feat/room-countdown lands: until then the game's team button is pressed until it shows the team.
void SetTeamTo(int slot, int team)
{
	const int from = g_s.game.slots[slot & 7].team;
	const int current = from > 0 && from <= kRoomTeams ? from : 0;
	for (int k = (team - current + kRoomTeams + 1) % (kRoomTeams + 1); k > 0; k--) GameRoomCycleTeam(slot & 7);
}

// The start countdown comes with feat/room-countdown; -1 means there is none.
int CountdownLeftMs()
{
	return -1;
}

// Renaming the room comes with feat/room-countdown.
void RenameRoom(const std::string &)
{
}
}
