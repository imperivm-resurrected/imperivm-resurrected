// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <vector>

#include "core/league.h"
#include "ui/leagueemblem.h"

namespace {

unsigned Opaque(uint32_t rgb) { return 0xFF000000u | rgb; }

unsigned Alpha(uint32_t px) { return px >> 24; }

std::vector<LeagueTier> AllTiers()
{
	std::vector<LeagueTier> out = {LeagueTier::None};
	for (int i = 1; i <= kLeagueTierCount; i++) out.push_back(static_cast<LeagueTier>(i));
	return out;
}

void Badges()
{
	std::vector<std::vector<uint32_t>> seen;
	for (const LeagueTier tier : AllTiers()) {
		const LeagueImage one = LeagueBadgePixels(tier, 1);
		CHECK_EQ(one.w, 20);
		CHECK_EQ(one.h, 20);
		CHECK_EQ(static_cast<unsigned>(one.px.size()), 400u);
		// The corner stays clear and the middle of the shield is solid.
		CHECK_EQ(Alpha(one.At(0, 0)), 0u);
		CHECK_EQ(Alpha(one.At(10, 10)), 255u);
		// Each rank is told apart by its drawing, not only by its colour.
		for (const std::vector<uint32_t> &other : seen) CHECK(other != one.px);
		seen.push_back(one.px);

		// A whole scale repeats every pixel, so the badge stays crisp.
		const LeagueImage two = LeagueBadgePixels(tier, 2);
		CHECK_EQ(two.w, 40);
		bool same = true;
		for (int y = 0; y < 40; y++) {
			for (int x = 0; x < 40; x++) same = same && two.At(x, y) == one.At(x / 2, y / 2);
		}
		CHECK(same);
	}
	const LeagueImage centurion = LeagueBadgePixels(LeagueTier::Centurion, 1);
	CHECK_EQ(centurion.At(9, 10), Opaque(0xA8692C));
	CHECK_EQ(centurion.At(8, 9), Opaque(0xE3A35E));
	CHECK_EQ(centurion.At(6, 0), Opaque(0xE3474C));
	// The highlight over the left half of the shield is see-through white over the body.
	const LeagueImage recruit = LeagueBadgePixels(LeagueTier::Recruit, 1);
	CHECK(recruit.At(5, 12) != Opaque(0x7A6553));
	CHECK_EQ(recruit.At(11, 12), Opaque(0x7A6553));
	CHECK_EQ(LeagueBadgePixels(LeagueTier::Imperator, 0).w, 20);
}

void OtherBadges()
{
	const LeagueImage vex = LeagueVexillumPixels(LeagueTier::Tribune, 1);
	CHECK_EQ(vex.w, 20);
	CHECK_EQ(vex.At(9, 19), Opaque(0x7A6553));
	CHECK_EQ(vex.At(9, 0), Opaque(0xEEF2F5));
	CHECK_EQ(Alpha(vex.At(0, 19)), 0u);
	const LeagueImage trophy = LeagueTrophyPixels(1);
	CHECK_EQ(Alpha(trophy.At(10, 10)), 255u);
	CHECK_EQ(Alpha(trophy.At(0, 0)), 0u);
	CHECK_EQ(Alpha(trophy.At(19, 19)), 0u);
}

void Emblems()
{
	for (const LeagueTier tier : AllTiers()) {
		const LeagueImage img = LeagueEmblemImage(tier, 160);
		CHECK_EQ(img.w, 160);
		CHECK_EQ(img.h, 160);
		CHECK_EQ(Alpha(img.At(0, 0)), 0u);
		CHECK_EQ(Alpha(img.At(159, 159)), 0u);
		CHECK_EQ(Alpha(img.At(80, 100)), 255u);
	}
	// The boss of the shield, well inside its smallest circle, is the body's colour.
	CHECK_EQ(LeagueEmblemImage(LeagueTier::Centurion, 160).At(80, 98), Opaque(0xA8692C));
	CHECK_EQ(LeagueEmblemImage(LeagueTier::Imperator, 160).At(80, 98), Opaque(0xC4161C));
	// Edges are smooth: somewhere along the shield's side there is a pixel only partly covered.
	const LeagueImage legate = LeagueEmblemImage(LeagueTier::Legate, 160);
	bool partial = false;
	for (int x = 40; x < 60 && !partial; x++) partial = Alpha(legate.At(x, 140)) > 0 && Alpha(legate.At(x, 140)) < 255;
	CHECK(partial);
	const LeagueImage little = LeagueEmblemImage(LeagueTier::Signifer, 48);
	CHECK_EQ(little.w, 48);
	CHECK_EQ(Alpha(little.At(24, 30)), 255u);
}

}

int main()
{
	Badges();
	OtherBadges();
	Emblems();
	return test::Summary("leagueemblem");
}
