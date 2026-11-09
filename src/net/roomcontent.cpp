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

std::string RoomHashKey(const std::string &gameplayHash)
{
	return gameplayHash.substr(0, 16);
}

bool RoomHashMatches(const std::string &gameplayHash, const std::string &hostHashKey)
{
	return hostHashKey.empty() || RoomHashKey(gameplayHash) == hostHashKey;
}

RoomRulesStep RoomRulesDecide(const std::string &roomKey, const std::string &ownKey, bool adoptFailed, unsigned differentForMs)
{
	if (roomKey.empty() || ownKey.empty() || roomKey == ownKey) return RoomRulesStep::kInSync;
	if (!adoptFailed) return RoomRulesStep::kTryAdopt;
	return differentForMs < kRoomRulesGraceMs ? RoomRulesStep::kWait : RoomRulesStep::kLeave;
}

bool RoomContentDiffers(RoomContent content)
{
	return content == RoomContent::kOtherGameplay || content == RoomContent::kOtherMap;
}
