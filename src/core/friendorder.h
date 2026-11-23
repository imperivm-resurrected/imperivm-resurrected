// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// What Steam says of a friend: playing this game, online (or looking to play or trade), away (or snoozing), busy, or offline (or invisible).
enum class FriendPresence { Playing, Online, Away, Busy, Offline };

// Steam's EPersonaState (0 offline, 1 online, 2 busy, 3 away, 4 snooze, 5 looking to trade, 6 looking to play, 7 invisible) and whether the
// friend is playing this game, which wins over any of them.
FriendPresence FriendPresenceOf(int personaState, bool inGame);

// A friends list as Steam's own: those playing the game first, then those online in any way, then the offline ones; by name inside each,
// without regard to case.
bool FriendListBefore(FriendPresence a, const std::string &nameA, FriendPresence b, const std::string &nameB);
