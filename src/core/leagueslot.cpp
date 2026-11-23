// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueslot.h"

namespace {
// The same race or colour is sent this many times at most.
const int kSameTries = 2;
}

LeagueSlotWanted LeagueSlotWants(const LeagueSlotTries &t, int raceItem, int color, uint32_t rgb, const LeagueSlotHave &have)
{
	LeagueSlotWanted w;
	if (t.sets >= kLeagueSlotMaxSets) return w;
	w.race = raceItem >= 0 && raceItem < have.raceItems && have.raceItem != raceItem && (t.raceSent != raceItem || t.raceTries < kSameTries);
	w.own = color == kLeagueOwnColor && rgb && have.own != rgb && t.ownTried != rgb;
	w.preset = color >= 0 && color < 12 && !have.colorTaken && (have.color != color || have.own != 0) && (t.colorSent != color || t.colorTries < kSameTries);
	return w;
}

void LeagueSlotRaceSent(LeagueSlotTries &t, int raceItem)
{
	if (t.raceSent != raceItem) {
		t.raceSent = raceItem;
		t.raceTries = 0;
	}
	t.raceTries++;
}

void LeagueSlotColorSent(LeagueSlotTries &t, int color)
{
	if (t.colorSent != color) {
		t.colorSent = color;
		t.colorTries = 0;
	}
	t.colorTries++;
}
