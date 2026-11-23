// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "net/roomcontent.h"

RoomContent CompareRoomContent(const RoomContentFacts &facts)
{
	const bool keysKnown = !facts.roomKey.empty() && !facts.ownKey.empty();
	if (keysKnown && facts.roomKey != facts.ownKey) return RoomContent::kOtherGameplay;
	if (!facts.mapModId.empty() && !facts.haveMapMod && !facts.mapSimple) return RoomContent::kOtherMap;
	return keysKnown ? RoomContent::kSame : RoomContent::kChecking;
}

bool RoomContentDiffers(RoomContent content)
{
	return content == RoomContent::kOtherGameplay || content == RoomContent::kOtherMap;
}

bool RoomContentBlocksEntry(RoomContent content)
{
	return content != RoomContent::kSame;
}
