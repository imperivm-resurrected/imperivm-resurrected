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

void DefaultFromTheEntry()
{
	const std::vector<std::string> known = {kCivSetGame, "community", "greek"};
	// The game's own rules, or an entry whose package has no civilizations: only the game's set.
	CHECK(CivSetsDefault(known, "") == std::vector<std::string>({kCivSetGame}));
	// An entry of a package with civilizations: only that package's set; the other packages are left for "Edit civilizations".
	CHECK(CivSetsDefault(known, "community") == std::vector<std::string>({"community"}));
	CHECK(CivSetsDefault(known, "greek") == std::vector<std::string>({"greek"}));
	// A set that is not loaded falls back to the game's.
	CHECK(CivSetsDefault(known, "gone") == std::vector<std::string>({kCivSetGame}));
	// With a single set there is nothing to filter: all.
	CHECK(CivSetsDefault({kCivSetGame}, "").empty());
	CHECK(CivSetsDefault({}, "community").empty());
	// Whatever the default is, "Edit civilizations" can add the others one by one.
	const std::vector<std::string> start = CivSetsDefault(known, "community");
	CHECK(CivSetsToggle(known, start, "greek") == std::vector<std::string>({"community", "greek"}));
	CHECK(CivSetsToggle(known, start, "community") == start);
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
	DefaultFromTheEntry();
	RoomState();
	return test::Summary("civsets");
}
