// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/civroom.h"

#include <algorithm>

namespace {
const int kComboRace[9] = {1, 4, 0, 3, 2, 7, 6, 5, 8};
const int kFirstModItem = 10;
}

std::vector<int> CivRoomOrder(const std::vector<RoomCiv> &civs, int items, bool gameAllowed)
{
	std::vector<int> order;
	if (civs.empty() || items < kFirstModItem + static_cast<int>(civs.size())) {
		for (int i = 0; i < items; i++) order.push_back(i);
		return order;
	}
	std::vector<bool> placed(civs.size(), false);
	for (int item = 0; item < 9; item++) {
		bool replaced = false;
		for (size_t i = 0; i < civs.size(); i++) {
			if (!civs[i].allowed || !civs[i].replacesBase || civs[i].base != kComboRace[item]) continue;
			order.push_back(kFirstModItem + static_cast<int>(i));
			placed[i] = true;
			replaced = true;
		}
		if (!replaced && gameAllowed) order.push_back(item);
	}
	for (size_t i = 0; i < civs.size(); i++) {
		if (!placed[i] && civs[i].allowed) order.push_back(kFirstModItem + static_cast<int>(i));
	}
	if (order.empty()) return CivRoomOrder(civs, items);
	return order;
}

int CivRoomReplacement(const std::vector<RoomCiv> &civs, int item, int items)
{
	if (item < 0 || item >= 9) return -1;
	const std::vector<int> order = CivRoomOrder(civs, items);
	if (std::find(order.begin(), order.end(), item) != order.end()) return -1;
	for (size_t i = 0; i < civs.size(); i++) {
		if (civs[i].allowed && civs[i].replacesBase && civs[i].base == kComboRace[item]) return kFirstModItem + static_cast<int>(i);
	}
	return -1;
}

int CivRoomBaseOfItem(int item)
{
	return item >= 0 && item < 9 ? kComboRace[item] : -1;
}

int CivRoomItemOfBase(int race)
{
	for (int item = 0; item < 9; item++) {
		if (kComboRace[item] == race) return item;
	}
	return -1;
}

std::vector<std::vector<int>> CivRandomGroups(const std::vector<RandomCiv> &civs, bool ai, bool gameAllowed)
{
	const auto drawn = [ai](const RandomCiv &c) { return c.allowed && c.random && (!ai || c.ai); };
	std::vector<std::vector<int>> groups;
	for (int base = 0; base < 8; base++) {
		std::vector<int> share;
		for (const RandomCiv &c : civs) {
			if (drawn(c) && c.replacesBase && c.base == base) share.push_back(c.index);
		}
		if (!share.empty()) groups.push_back(share);
		else if (gameAllowed) groups.push_back({base});
	}
	for (const RandomCiv &c : civs) {
		if (drawn(c) && !c.replacesBase) groups.push_back({c.index});
	}
	return groups;
}
