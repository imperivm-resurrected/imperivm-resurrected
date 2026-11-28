// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

#include "core/lanroom.h"

// A room heard on the local network, and the address of its host (IPv4, network order).
struct LanHeard {
	LanRoom room;
	uint32_t ip = 0;
};

// This host's room, told to the local network (the caller paces it, once a second).
void LanBeaconAnnounce(const LanRoom &room);
// The rooms heard in the last seconds. Listening starts with the first call; LanBeaconStop ends it when the list closes.
std::vector<LanHeard> LanBeaconRooms();
void LanBeaconStop();
