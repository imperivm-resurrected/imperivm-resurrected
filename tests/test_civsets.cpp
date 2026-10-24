// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/civsets.h"

namespace {

void EmptyAllowsEverything()
{
	CHECK(CivSetsAllow({}, kCivSetGame));
	CHECK(CivSetsAllow({}, "greek-mod"));
}

void ListAllowsItsSets()
{
	const std::vector<std::string> sets = {kCivSetGame, "caesar"};
	CHECK(CivSetsAllow(sets, "caesar"));
	CHECK(!CivSetsAllow(sets, "greek-mod"));
}

void JoinAndSplit()
{
	const std::vector<std::string> sets = {kCivSetGame, "Greek Mod 4.2"};
	CHECK_EQ(CivSetsJoin(sets), std::string("@game|Greek Mod 4.2"));
	CHECK(CivSetsSplit(CivSetsJoin(sets)) == sets);
	CHECK(CivSetsSplit("").empty());
	CHECK(CivSetsSplit("a||b|a|") == std::vector<std::string>({"a", "b"}));
}

void RoomState()
{
	CHECK(CivRoomSets().empty());
	CivRoomSetsSet({"caesar"});
	CHECK(CivRoomSets() == std::vector<std::string>({"caesar"}));
	CivRoomSetsSet({});
	CHECK(CivRoomSets().empty());
}

}

int main()
{
	EmptyAllowsEverything();
	ListAllowsItsSets();
	JoinAndSplit();
	RoomState();
	return test::Summary("civsets");
}
