// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/lanroom.h"

static void ARoomGoesAndComesBack()
{
	LanRoom r;
	r.name = "Partida de Tulia";
	r.host = "Tulia";
	r.options = "Game: Elimination\nMap type: continental\nSize: medium\nResources: normal";
	r.version = "1.0.4";
	r.rulesLevel = "r3:s9";
	r.build = "abc123";
	r.gameplayKey = "c35f0a1b2c3d4e5f";
	r.players = 1;
	r.maxPlayers = 4;
	const std::string packet = LanRoomPacket(r);
	LanRoom back;
	CHECK(LanRoomParse(packet.data(), static_cast<int>(packet.size()), back));
	CHECK_EQ(back.name, r.name);
	CHECK_EQ(back.host, r.host);
	// Only the map travels of the options.
	CHECK_EQ(back.options, std::string("Map type: continental"));
	CHECK_EQ(back.version, r.version);
	CHECK_EQ(back.rulesLevel, r.rulesLevel);
	CHECK_EQ(back.build, r.build);
	CHECK_EQ(back.gameplayKey, r.gameplayKey);
	CHECK_EQ(back.players, 1);
	CHECK_EQ(back.maxPlayers, 4);
}

static void OtherPacketsAreNotRooms()
{
	LanRoom r;
	const std::string game = "\x01\x02 a packet of the game";
	CHECK(!LanRoomParse(game.data(), static_cast<int>(game.size()), r));
	const std::string broken = "IRLAN1\n{\"name\":";
	CHECK(!LanRoomParse(broken.data(), static_cast<int>(broken.size()), r));
	const std::string nameless = "IRLAN1\n{\"max\":4}";
	CHECK(!LanRoomParse(nameless.data(), static_cast<int>(nameless.size()), r));
	CHECK(!LanRoomParse(nullptr, 0, r));
}

static void LongNamesAreCutWhole()
{
	LanRoom r;
	r.name = std::string(198, 'a') + "\xC3\xB1\xC3\xB1";
	r.host = "x";
	r.maxPlayers = 2;
	const std::string packet = LanRoomPacket(r);
	LanRoom back;
	CHECK(LanRoomParse(packet.data(), static_cast<int>(packet.size()), back));
	// 200 bytes at most, and never half a character.
	CHECK_EQ(back.name, std::string(198, 'a') + "\xC3\xB1");
	CHECK(packet.size() < 1400);
}

int main()
{
	ARoomGoesAndComesBack();
	OtherPacketsAreNotRooms();
	LongNamesAreCutWhole();
	return test::Summary("lanroom");
}
