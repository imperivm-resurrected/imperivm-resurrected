// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The sets of civilizations a room allows: kCivSetGame for the game's own, and the folder of the content pack for each pack that
// ships civilizations. An empty list allows all of them.
inline constexpr char kCivSetGame[] = "@game";

bool CivSetsAllow(const std::vector<std::string> &sets, const std::string &set);
std::string CivSetsJoin(const std::vector<std::string> &sets);
std::vector<std::string> CivSetsSplit(const std::string &text);
// The sets after the host flips `set` (one of `known`, the sets the room's content offers) while `now` are allowed. At least one set
// stays allowed: flipping the last one returns `now`. When every known set ends up allowed the list is empty, which means all.
std::vector<std::string> CivSetsToggle(const std::vector<std::string> &known, const std::vector<std::string> &now, const std::string &set);

// The sets a new room allows by default, from the set of the Rules entry it was made with (packSet, "" when the entry's package has no
// civilizations or the entry is the game's own): only that one, or only the game's. Empty (all) when the content offers a single set.
std::vector<std::string> CivSetsDefault(const std::vector<std::string> &known, const std::string &packSet);

// The sets of the room being played or hosted. Guests take them from the host.
std::vector<std::string> CivRoomSets();
void CivRoomSetsSet(const std::vector<std::string> &sets);
