// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>
#include <string>

#include "game/gameroom.h"
#include "ui/mpui_internal.h"

namespace mpui_impl {
namespace {
// GameRoomTick calls it on the game thread without the room lock; the countdown waits while someone is joining or downloading.
bool StartGateBlocked()
{
	const GameRoomSnapshot snap = GameRoomGet();
	std::lock_guard<std::mutex> lock(g_lock);
	return !StartBlocker(g_s, snap).toast.empty();
}
}

void SetTeamTo(int slot, int team)
{
	GameRoomSetTeam(slot & 7, team);
}

int CountdownLeftMs()
{
	return GameRoomCountdownLeftMs();
}

void RenameRoom(const std::string &name)
{
	GameRoomRename(name);
}

void StartRoomCountdown()
{
	GameRoomSetStartGate(&StartGateBlocked);
	GameRoomSetAutoStart(true);
}
}
