// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "hooks/movemark.h"

// The marks of 1.0.3, whose movement also turned walking units mid-stride.
static const unsigned kTurningHashMark = 0xc000;
static const std::string kTurningRulesTag = "/mfs1";

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

// A room of the turning simulation and one of this must refuse each other: a match of the two desynced.
static void ApartFromTheTurningOne()
{
	const MoveMark m = MoveMarkFor(true);
	CHECK(m.hash != kTurningHashMark);
	CHECK(std::string(m.rules) != kTurningRulesTag);
	const unsigned gameHash = 0x14822800;
	CHECK((gameHash ^ m.hash) != (gameHash ^ kTurningHashMark));
	CHECK((gameHash ^ m.hash) != gameHash);
}

int main()
{
	Off();
	On();
	ApartFromTheTurningOne();
	return test::Summary("movemark");
}
