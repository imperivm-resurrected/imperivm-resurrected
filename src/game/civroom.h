// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <vector>

struct RoomCiv {
	int base = 0;
	bool replacesBase = false;
	bool allowed = true;
};

// The items of the room's civilization lists, in order. Civilizations the room does not allow are left out; so are the game's
// own when gameAllowed is false, and the ones another allowed civilization replaces.
std::vector<int> CivRoomOrder(const std::vector<RoomCiv> &civs, int items, bool gameAllowed = true);
int CivRoomReplacement(const std::vector<RoomCiv> &civs, int item, int items);
int CivRoomBaseOfItem(int item);
int CivRoomItemOfBase(int race);

struct RandomCiv {
	int index = 0;
	int base = 0;
	bool replacesBase = false;
	bool random = true;
	bool ai = false;
	bool allowed = true;
};

// What the random draw picks among: first one of the groups (a game civilization or the ones that replace it, or an independent
// one), then one civilization of the group. Empty when the room allows nothing that can be drawn.
std::vector<std::vector<int>> CivRandomGroups(const std::vector<RandomCiv> &civs, bool ai, bool gameAllowed);
