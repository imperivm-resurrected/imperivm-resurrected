// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "hooks/movemark.h"

// The marks of the earlier simulations of the movement patch: 1.0.3's, whose turn desynced, and the one without any turn.
struct OldMark {
	unsigned hash;
	const char *rules;
};
static const OldMark kOldMarks[] = {{0xc000, "/mfs1"}, {0xa000, "/mfs2"}};

static void Off()
{
	const MoveMark m = MoveMarkFor(false);
	CHECK_EQ(m.hash, 0u);
	CHECK_EQ(std::string(m.rules), std::string());
}

static void On()
{
	const MoveMark m = MoveMarkFor(true);
	CHECK(m.hash != 0);
	CHECK((m.hash & 0xffff0000u) == 0);
	CHECK(std::string(m.rules).rfind("/mfs", 0) == 0);
}

// A room of an earlier simulation and one of this must refuse each other: a match of the two would desync.
static void ApartFromTheOldOnes()
{
	const MoveMark m = MoveMarkFor(true);
	const unsigned gameHash = 0x14822800;
	CHECK((gameHash ^ m.hash) != gameHash);
	for (const OldMark &old : kOldMarks) {
		CHECK(m.hash != old.hash);
		CHECK(std::string(m.rules) != old.rules);
		CHECK((gameHash ^ m.hash) != (gameHash ^ old.hash));
	}
}

int main()
{
	Off();
	On();
	ApartFromTheOldOnes();
	return test::Summary("movemark");
}
