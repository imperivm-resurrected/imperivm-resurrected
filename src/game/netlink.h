// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

struct NetLink {
	bool (*multiplayerSaveLoading)() = nullptr;
	bool (*rejoinLoading)() = nullptr;
	unsigned (*roomRules)() = nullptr;
	bool (*isLockstepHost)() = nullptr;
	bool (*sendRoomCountdown)(unsigned seq, int ms) = nullptr;
	std::string (*roomName)() = nullptr;
	bool (*renameRoom)(const std::string &name) = nullptr;
};

void NetLinkSet(const NetLink &link);

bool NetMultiplayerSaveLoading();
bool NetRejoinLoading();
unsigned NetRoomRules();
bool NetIsLockstepHost();
// The host tells the other players of a Steam room that the start countdown began (ms > 0) or stopped (ms < 0). False when
// the room has no channel of the mod (a local network room).
bool NetSendRoomCountdown(unsigned seq, int ms);
// The name of the Steam room this PC is in, empty outside one.
std::string NetRoomName();
// The owner of a Steam room renames it for the room list and its players. False when this PC owns no Steam room.
bool NetRenameRoom(const std::string &name);
