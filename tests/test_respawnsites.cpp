// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdio>
#include <string>

#include "game/respawnsites.h"

namespace {

void KindsByClass()
{
	CHECK_EQ(RespawnKindOf("StonePile"), kRespawnMushrooms);
	CHECK_EQ(RespawnKindOf("OakStump"), kRespawnWater);
	CHECK_EQ(RespawnKindOf("Tombstone"), kRespawnWater);
	CHECK_EQ(RespawnKindOf("TombStone"), kRespawnWater);
	CHECK_EQ(RespawnKindOf("Ruins1"), kRespawnRuin);
	CHECK_EQ(RespawnKindOf("DRuins1"), kRespawnNone);
	CHECK_EQ(RespawnKindOf("BaseWell"), kRespawnNone);
}

void WaterAndMushroomsFollowTheGame()
{
	int water = 0;
	RespawnSiteState s;
	// Under the fog nothing is learned.
	CHECK(!s.Observe(&water, false, false, 1000, 120000, kRespawnWater));
	CHECK(!s.discovered);
	// Seen full, then used: the game's own respawn event gives the time.
	CHECK(!s.Observe(&water, true, true, 1500, 0, kRespawnWater));
	CHECK(!s.Observe(&water, true, false, 2000, 120000, kRespawnWater));
	CHECK(s.discovered && !s.stocked && s.due == 120000 && !s.approximate);
	// Back while out of sight: still shown as last seen.
	CHECK(!s.Observe(&water, false, true, 120000, 0, kRespawnWater));
	CHECK(!s.stocked && s.due == 120000);
	// Seen back: told once.
	CHECK(s.Observe(&water, true, true, 122000, 0, kRespawnWater));
	CHECK(s.stocked && s.due == 0);
	CHECK(!s.Observe(&water, true, true, 123000, 0, kRespawnWater));
	CHECK(!s.Observe(&water, true, false, 130000, 240000, kRespawnWater));
	CHECK(!s.stocked && s.due == 240000);
}

void RuinsOnlyCountWhatWasSeen()
{
	int ruin = 0, other = 0;
	RespawnSiteState s;
	// What happens under the fog is not learned: neither the loot nor its taking.
	CHECK(!s.Observe(&ruin, false, true, 1000, 0, kRespawnRuin));
	CHECK(!s.discovered);
	// Found empty: nobody saw when it was emptied.
	CHECK(!s.Observe(&ruin, true, false, 2000, 0, kRespawnRuin));
	CHECK(s.discovered && !s.stocked && s.due == 0);
	// Its first loot is no news: the game fills the ruins a moment after the start, and this one was found empty.
	CHECK(!s.Observe(&ruin, true, true, 3000, 0, kRespawnRuin));
	// Seen full, then emptied while watched: the refill is due in about ten minutes.
	CHECK(!s.Observe(&ruin, true, false, 4000, 0, kRespawnRuin));
	CHECK(s.due == 4000 + kRuinRefillMs && s.approximate);
	// The time stays while it stays empty.
	CHECK(!s.Observe(&ruin, true, false, 5000, 0, kRespawnRuin));
	CHECK(s.due == 4000 + kRuinRefillMs);
	CHECK(s.Observe(&ruin, true, true, 610000, 0, kRespawnRuin));
	CHECK(s.due == 0 && !s.approximate);
	// Emptied in the fog and found empty later: no time, it would be a guess.
	CHECK(!s.Observe(&ruin, false, false, 620000, 0, kRespawnRuin));
	CHECK(!s.Observe(&ruin, true, false, 700000, 0, kRespawnRuin));
	CHECK(s.discovered && !s.stocked && s.due == 0);
	// Another object in the same slot starts afresh.
	CHECK(!s.Observe(&other, true, false, 710000, 0, kRespawnRuin));
	CHECK(s.site == &other && s.due == 0);
}

void FirstLootIsNotBack()
{
	int water = 0;
	RespawnSiteState s;
	// Found empty (used before the player came), then full: nobody saw it go, so it is not "back".
	CHECK(!s.Observe(&water, true, false, 1000, 60000, kRespawnWater));
	CHECK(!s.Observe(&water, true, true, 61000, 0, kRespawnWater));
	CHECK(s.stocked && s.everStocked);
	// From then on it is.
	CHECK(!s.Observe(&water, true, false, 70000, 190000, kRespawnWater));
	CHECK(s.Observe(&water, true, true, 191000, 0, kRespawnWater));
}

void TimesAndPixels()
{
	CHECK_EQ(RespawnSecondsUntil(732000, 142000), 590u);
	CHECK_EQ(RespawnSecondsUntil(732000, 731999), 1u);
	CHECK_EQ(RespawnSecondsUntil(732000, 732000), 0u);
	CHECK_EQ(RespawnSecondsUntil(0, 200000), 0u);
	char text[16];
	RespawnTimeText(245, false, text);
	CHECK_EQ(std::string(text), std::string("4:05"));
	RespawnTimeText(598, true, text);
	CHECK_EQ(std::string(text), std::string("~9:58"));
	int pixel = -1;
	CHECK(RespawnProjectAxis(2000, 0, 4000, 140, 3580, pixel));
	CHECK_EQ(pixel, 1860);
	CHECK(RespawnProjectAxis(4600, 1000, 4600, 62, 1367, pixel));
	CHECK_EQ(pixel, 1367);
	CHECK(!RespawnProjectAxis(4601, 1000, 4600, 62, 1367, pixel));
	CHECK(!RespawnProjectAxis(4600, 4600, 4600, 62, 1367, pixel));
}

}

int main()
{
	KindsByClass();
	WaterAndMushroomsFollowTheGame();
	RuinsOnlyCountWhatWasSeen();
	FirstLootIsNotBack();
	TimesAndPixels();
	return test::Summary("respawnsites");
}
