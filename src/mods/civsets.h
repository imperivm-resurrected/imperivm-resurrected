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

// The sets of the room being played or hosted. Guests take them from the host.
std::vector<std::string> CivRoomSets();
void CivRoomSetsSet(const std::vector<std::string> &sets);
