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

// The room "key" also covers the civilization table, which a joiner only knows once it has restarted into the host's profile.
// Before restarting it can only check the plain gameplay hash, which the host publishes next to it as "hkey".
std::string RoomHashKey(const std::string &gameplayHash);
bool RoomHashMatches(const std::string &gameplayHash, const std::string &hostHashKey);
bool RoomContentDiffers(RoomContent content);

// What a guest in an open room does about the room's key (see mpui_room.cpp RoomFollowRules): nothing while the keys are equal or one is not
// known yet; try to switch its own rules to the room's when they differ; if that was not possible, wait (the host's mods line and key reach
// it as two updates) and leave the room if the difference lasts.
enum class RoomRulesStep { kInSync, kTryAdopt, kWait, kLeave };
const unsigned kRoomRulesGraceMs = 4000;
RoomRulesStep RoomRulesDecide(const std::string &roomKey, const std::string &ownKey, bool adoptFailed, unsigned differentForMs);
