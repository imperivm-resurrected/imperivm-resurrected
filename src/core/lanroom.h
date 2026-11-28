// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>

// A room of the game's own local network as its host's mod announces it: the game's LAN browser only lives in its old screen, so the
// mod of each host says on the network that its room is open, once a second, and the room list shows it beside Steam's.
struct LanRoom {
	std::string name, host;
	// The room's map, as the lines "Scenario: " or "Map type: " of its options.
	std::string options;
	// The mod's version, rules and build, compared as a Steam room's are; the content key of the host.
	std::string version, rulesLevel, build, gameplayKey;
	int players = 0, maxPlayers = 0;
};

// The UDP port the announcements go to, beside the game's own (40444, 40445, 40447).
constexpr uint16_t kLanRoomPort = 40450;

// The packet of a room, and the room of a packet (false when it is not one of the mod's).
std::string LanRoomPacket(const LanRoom &r);
bool LanRoomParse(const char *data, int len, LanRoom &out);
// The lines of a room's options that say its map: what the announcement keeps of them.
std::string LanRoomMapLines(const std::string &options);
