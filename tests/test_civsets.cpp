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

void ToggleKeepsOneSet()
{
	const std::vector<std::string> known = {kCivSetGame, "caesar", "greek"};
	const std::vector<std::string> noGreek = {kCivSetGame, "caesar"};
	// From "all", flipping a set leaves out that set only.
	CHECK(CivSetsToggle(known, {}, "greek") == noGreek);
	CHECK(CivSetsToggle(known, {}, kCivSetGame) == std::vector<std::string>({"caesar", "greek"}));
	// Flipping it back makes every set allowed again, which is the empty list.
	CHECK(CivSetsToggle(known, noGreek, "greek").empty());
	CHECK(CivSetsToggle(known, noGreek, "caesar") == std::vector<std::string>({kCivSetGame}));
	// The last set that is on cannot be turned off.
	const std::vector<std::string> onlyGame = {kCivSetGame};
	CHECK(CivSetsToggle(known, onlyGame, kCivSetGame) == onlyGame);
	CHECK(CivSetsToggle(known, onlyGame, "caesar") == noGreek);
	// A set the room does not offer changes nothing.
	CHECK(CivSetsToggle(known, noGreek, "other") == noGreek);
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
	ToggleKeepsOneSet();
	RoomState();
	return test::Summary("civsets");
}
