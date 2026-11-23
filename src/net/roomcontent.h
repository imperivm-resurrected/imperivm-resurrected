// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

enum class RoomContent { kSame, kChecking, kOtherGameplay, kOtherMap };

struct RoomContentFacts {
	std::string roomKey;
	std::string ownKey;
	std::string mapModId;
	bool haveMapMod = false;
	bool mapSimple = false;
};

RoomContent CompareRoomContent(const RoomContentFacts &facts);
bool RoomContentDiffers(RoomContent content);
// Whether a player may go into the room: only with both keys known and equal. A key not ready yet (empty) is never taken for the game
// without mods: the two computers could be playing different content.
bool RoomContentBlocksEntry(RoomContent content);
