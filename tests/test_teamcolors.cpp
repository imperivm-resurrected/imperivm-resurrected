// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <set>

#include "core/teamcolors.h"

namespace {

// [PlayerColors] of the game, id1 to id12: red, yellow, green, cyan, magenta, brown, teal, orange, forest, blue, purple, peach.
const uint32_t kVanilla[kTeamColorPalette] = {0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0xff7fff, 0x783c14,
	0x009b9b, 0xff7f00, 0x007f00, 0x0000ff, 0x7f00ff, 0xffb978};
enum { kRed, kYellow, kGreen, kCyan, kMagenta, kBrown, kTeal, kOrange, kForest, kBlue, kPurple, kPeach };

bool Distinct(const std::array<int, kTeamColorSlots> &out, const bool (&used)[kTeamColorSlots])
{
	std::set<int> seen;
	for (int i = 0; i < kTeamColorSlots; i++) {
		if (!used[i]) {
			if (out[i] != -1) return false;
			continue;
		}
		if (out[i] < 0 || out[i] >= kTeamColorPalette || !seen.insert(out[i]).second) return false;
	}
	return true;
}

bool In(int c, std::initializer_list<int> family)
{
	for (int f : family) {
		if (c == f) return true;
	}
	return false;
}

void FourAgainstFour()
{
	const int team[kTeamColorSlots] = {1, 2, 1, 2, 1, 2, 1, 2};
	const bool used[kTeamColorSlots] = {true, true, true, true, true, true, true, true};
	const auto out = TeamColorsFor(kVanilla, team, used);
	CHECK(Distinct(out, used));
	// Warm against cool, each team led by its own colour.
	CHECK_EQ(out[0], static_cast<int>(kRed));
	CHECK_EQ(out[1], static_cast<int>(kBlue));
	for (int i = 0; i < kTeamColorSlots; i += 2) CHECK(In(out[i], {kRed, kOrange, kBrown, kPeach}));
	for (int i = 1; i < kTeamColorSlots; i += 2) CHECK(In(out[i], {kBlue, kCyan, kTeal, kPurple}));
}

void FourTeams()
{
	const int team[kTeamColorSlots] = {1, 1, 2, 2, 3, 3, 4, 4};
	const bool used[kTeamColorSlots] = {true, true, true, true, true, true, true, true};
	const auto out = TeamColorsFor(kVanilla, team, used);
	CHECK(Distinct(out, used));
	CHECK_EQ(out[0], static_cast<int>(kRed));
	CHECK_EQ(out[2], static_cast<int>(kBlue));
	CHECK_EQ(out[4], static_cast<int>(kGreen));
	CHECK_EQ(out[6], static_cast<int>(kYellow));
	CHECK(In(out[5], {kForest, kTeal, kCyan}));
}

void WithoutTeamsAndEmptySlots()
{
	const int team[kTeamColorSlots] = {1, 0, 2, 0, 1, 0, 0, 0};
	const bool used[kTeamColorSlots] = {true, true, true, false, true, true, false, false};
	const auto out = TeamColorsFor(kVanilla, team, used);
	CHECK(Distinct(out, used));
	CHECK_EQ(out[3], -1);
	CHECK_EQ(out[6], -1);
	CHECK_EQ(out[0], static_cast<int>(kRed));
	CHECK_EQ(out[2], static_cast<int>(kBlue));
	// Those without a team stay clear of red and blue.
	CHECK(!In(out[1], {kOrange, kBrown, kPeach, kPurple}));
}

void CommunityModPalette()
{
	// The Community Mod makes id7 a dark red instead of teal.
	uint32_t palette[kTeamColorPalette];
	for (int i = 0; i < kTeamColorPalette; i++) palette[i] = kVanilla[i];
	palette[kTeal] = 0x9a000a;
	const int team[kTeamColorSlots] = {1, 2, 1, 2, 1, 2, 1, 2};
	const bool used[kTeamColorSlots] = {true, true, true, true, true, true, true, true};
	const auto out = TeamColorsFor(palette, team, used);
	CHECK(Distinct(out, used));
	// The dark red goes to the reds, never to the blues.
	for (int i = 1; i < kTeamColorSlots; i += 2) CHECK(out[i] != static_cast<int>(kTeal));
	CHECK(In(out[2], {kTeal, kBrown, kPeach, kOrange}));
}

}

int main()
{
	FourAgainstFour();
	FourTeams();
	WithoutTeamsAndEmptySlots();
	CommunityModPalette();
	return test::Summary("teamcolors");
}
