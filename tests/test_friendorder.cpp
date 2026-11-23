// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/friendorder.h"

namespace {

void Presence()
{
	CHECK(FriendPresenceOf(0, false) == FriendPresence::Offline);
	CHECK(FriendPresenceOf(1, false) == FriendPresence::Online);
	CHECK(FriendPresenceOf(2, false) == FriendPresence::Busy);
	CHECK(FriendPresenceOf(3, false) == FriendPresence::Away);
	CHECK(FriendPresenceOf(4, false) == FriendPresence::Away);
	CHECK(FriendPresenceOf(5, false) == FriendPresence::Online);
	CHECK(FriendPresenceOf(6, false) == FriendPresence::Online);
	CHECK(FriendPresenceOf(7, false) == FriendPresence::Offline);
	CHECK(FriendPresenceOf(42, false) == FriendPresence::Offline);
	// Playing the game wins over whatever state they set.
	CHECK(FriendPresenceOf(3, true) == FriendPresence::Playing);
	CHECK(FriendPresenceOf(0, true) == FriendPresence::Playing);
}

struct Friend {
	std::string name;
	FriendPresence presence;
};

void Order()
{
	std::vector<Friend> list = {
		{"zeta", FriendPresence::Offline},
		{"Bruto", FriendPresence::Away},
		{"alba", FriendPresence::Offline},
		{"Cayo", FriendPresence::Playing},
		{"augusto", FriendPresence::Online},
		{"Druso", FriendPresence::Busy},
		{"Anco", FriendPresence::Playing},
	};
	std::sort(list.begin(), list.end(), [](const Friend &a, const Friend &b) { return FriendListBefore(a.presence, a.name, b.presence, b.name); });
	const char *const want[] = {"Anco", "Cayo", "augusto", "Bruto", "Druso", "alba", "zeta"};
	CHECK(list.size() == sizeof(want) / sizeof(want[0]));
	for (size_t i = 0; i < list.size(); i++) CHECK_EQ(list[i].name, std::string(want[i]));
	CHECK(!FriendListBefore(FriendPresence::Online, "a", FriendPresence::Away, "A"));
	CHECK(!FriendListBefore(FriendPresence::Away, "A", FriendPresence::Online, "a"));
}

}

int main()
{
	Presence();
	Order();
	return test::Summary("friendorder");
}
