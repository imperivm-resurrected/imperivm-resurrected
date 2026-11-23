// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The civilization and colour a league page chose (a cup's match page, the panel of a ranked match), put in the player's slot of the room
// that stays under it.

// The colour the player made on the wheel instead of one of the game's twelve.
constexpr int kLeagueOwnColor = 100;
// The changes sent to a slot in all; after them the slot stays as the room has it.
constexpr int kLeagueSlotMaxSets = 12;

// What was sent, and how many times: a room that keeps its own (a scenario fixes the civilizations, the colour is another player's) is not
// asked again and again, which made the player not ready each time and restarted the countdown.
struct LeagueSlotTries {
	int raceSent = -1, raceTries = 0, colorSent = -1, colorTries = 0;
	uint32_t ownTried = 0;
	int sets = 0;
};

// The slot as the room has it.
struct LeagueSlotHave {
	// The room's item of the civilization, and how many the room offers.
	int raceItem = -1, raceItems = 0;
	int color = -1;
	// The slot's own colour (0xRRGGBB), 0 without one.
	uint32_t own = 0;
	// Another player of the room has the preset colour wanted.
	bool colorTaken = false;
};

struct LeagueSlotWanted {
	bool race = false, own = false, preset = false;
	bool Any() const { return race || own || preset; }
};

// What still goes to the slot: `raceItem` is the room's item of the civilization chosen (-1: the room's own), `color` one of the twelve,
// kLeagueOwnColor with `rgb`, or -1 for the room's own.
LeagueSlotWanted LeagueSlotWants(const LeagueSlotTries &t, int raceItem, int color, uint32_t rgb, const LeagueSlotHave &have);
// A race or a preset colour sent to the slot, counted.
void LeagueSlotRaceSent(LeagueSlotTries &t, int raceItem);
void LeagueSlotColorSent(LeagueSlotTries &t, int color);
