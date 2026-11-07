// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

struct NetLink {
	bool (*multiplayerSaveLoading)() = nullptr;
	bool (*rejoinLoading)() = nullptr;
	unsigned (*roomRules)() = nullptr;
	bool (*isLockstepHost)() = nullptr;
	bool (*sendRoomCountdown)(unsigned seq, int ms) = nullptr;
};

void NetLinkSet(const NetLink &link);

bool NetMultiplayerSaveLoading();
bool NetRejoinLoading();
unsigned NetRoomRules();
bool NetIsLockstepHost();
// The host tells the other players of a Steam room that the start countdown began (ms > 0) or stopped (ms < 0). False when
// the room has no channel of the mod (a local network room).
bool NetSendRoomCountdown(unsigned seq, int ms);
