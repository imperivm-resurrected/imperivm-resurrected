// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/leagueslot.h"

namespace {

LeagueSlotHave Slot(int raceItem, int color, uint32_t own = 0, bool taken = false)
{
	LeagueSlotHave h;
	h.raceItem = raceItem;
	h.raceItems = 9;
	h.color = color;
	h.own = own;
	h.colorTaken = taken;
	return h;
}

void NothingWhenTheSlotHasIt()
{
	LeagueSlotTries t;
	CHECK(!LeagueSlotWants(t, 3, 5, 0, Slot(3, 5)).Any());
	// The room's own civilization and colour: nothing to send.
	CHECK(!LeagueSlotWants(t, -1, -1, 0, Slot(3, 5)).Any());
}

void RaceAndColourWanted()
{
	LeagueSlotTries t;
	const LeagueSlotWanted w = LeagueSlotWants(t, 2, 7, 0, Slot(3, 5));
	CHECK(w.race);
	CHECK(w.preset);
	CHECK(!w.own);
	// An item the room does not offer is not sent.
	CHECK(!LeagueSlotWants(t, 9, 5, 0, Slot(3, 5)).race);
}

void ARefusedRaceIsSentTwiceOnly()
{
	// The room keeps its own race (a scenario fixes it): the same item is not sent a third time, which would make the player not ready
	// again and restart the countdown.
	LeagueSlotTries t;
	CHECK(LeagueSlotWants(t, 2, -1, 0, Slot(3, 5)).race);
	LeagueSlotRaceSent(t, 2);
	CHECK(LeagueSlotWants(t, 2, -1, 0, Slot(3, 5)).race);
	LeagueSlotRaceSent(t, 2);
	CHECK(!LeagueSlotWants(t, 2, -1, 0, Slot(3, 5)).race);
	// Another race chosen on the page is sent again.
	CHECK(LeagueSlotWants(t, 4, -1, 0, Slot(3, 5)).race);
	LeagueSlotRaceSent(t, 4);
	CHECK_EQ(t.raceTries, 1);
}

void ARefusedColourIsSentTwiceOnly()
{
	LeagueSlotTries t;
	LeagueSlotColorSent(t, 7);
	LeagueSlotColorSent(t, 7);
	CHECK(!LeagueSlotWants(t, -1, 7, 0, Slot(3, 5)).preset);
	CHECK(LeagueSlotWants(t, -1, 8, 0, Slot(3, 5)).preset);
}

void TakenColourIsLeft()
{
	LeagueSlotTries t;
	CHECK(!LeagueSlotWants(t, -1, 7, 0, Slot(3, 5, 0, true)).preset);
}

void OwnColour()
{
	LeagueSlotTries t;
	CHECK(LeagueSlotWants(t, -1, kLeagueOwnColor, 0x123456, Slot(3, 5)).own);
	CHECK(!LeagueSlotWants(t, -1, kLeagueOwnColor, 0x123456, Slot(3, 5, 0x123456)).own);
	// Tried once: the room said it is too close to another player's.
	t.ownTried = 0x123456;
	CHECK(!LeagueSlotWants(t, -1, kLeagueOwnColor, 0x123456, Slot(3, 5)).own);
	// A preset over an own colour in the slot clears it.
	LeagueSlotTries u;
	CHECK(LeagueSlotWants(u, -1, 5, 0, Slot(3, 5, 0x123456)).preset);
	// Without a colour made, nothing.
	CHECK(!LeagueSlotWants(u, -1, kLeagueOwnColor, 0, Slot(3, 5)).own);
}

void GivesUpAfterTheMaximum()
{
	LeagueSlotTries t;
	t.sets = kLeagueSlotMaxSets;
	CHECK(!LeagueSlotWants(t, 2, 7, 0, Slot(3, 5)).Any());
}

}

int main()
{
	NothingWhenTheSlotHasIt();
	RaceAndColourWanted();
	ARefusedRaceIsSentTwiceOnly();
	ARefusedColourIsSentTwiceOnly();
	TakenColourIsLeft();
	OwnColour();
	GivesUpAfterTheMaximum();
	return test::Summary("leagueslot");
}
