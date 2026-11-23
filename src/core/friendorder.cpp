// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/friendorder.h"

#include <string.h>

FriendPresence FriendPresenceOf(int personaState, bool inGame)
{
	if (inGame) return FriendPresence::Playing;
	switch (personaState) {
	case 1:
	case 5:
	case 6: return FriendPresence::Online;
	case 2: return FriendPresence::Busy;
	case 3:
	case 4: return FriendPresence::Away;
	default: return FriendPresence::Offline;
	}
}

namespace {

int GroupOf(FriendPresence p)
{
	if (p == FriendPresence::Playing) return 0;
	return p == FriendPresence::Offline ? 2 : 1;
}

}

bool FriendListBefore(FriendPresence a, const std::string &nameA, FriendPresence b, const std::string &nameB)
{
	const int ga = GroupOf(a), gb = GroupOf(b);
	if (ga != gb) return ga < gb;
	return _stricmp(nameA.c_str(), nameB.c_str()) < 0;
}
