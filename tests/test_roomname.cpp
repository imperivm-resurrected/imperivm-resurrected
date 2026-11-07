// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "game/roomname.h"

namespace {

void KeepsANormalName()
{
	CHECK_EQ(RoomNameClean("Game of Marcus", kRoomNameMaxBytes), std::string("Game of Marcus"));
	CHECK_EQ(RoomNameClean("Partida de Íñigo · 2v2", kRoomNameMaxBytes), std::string("Partida de Íñigo · 2v2"));
}

void TrimsAndDropsControlCharacters()
{
	CHECK_EQ(RoomNameClean("  Roma  ", kRoomNameMaxBytes), std::string("Roma"));
	CHECK_EQ(RoomNameClean("Roma\tvs\nCartago\r", kRoomNameMaxBytes), std::string("Roma vs Cartago"));
	CHECK_EQ(RoomNameClean(std::string("Ro\x01ma\x7f", 6), kRoomNameMaxBytes), std::string("Roma"));
	CHECK_EQ(RoomNameClean(" \t\n ", kRoomNameMaxBytes), std::string());
	CHECK_EQ(RoomNameClean("", kRoomNameMaxBytes), std::string());
}

void DropsInvalidUtf8()
{
	CHECK_EQ(RoomNameClean("Ro\xffma", kRoomNameMaxBytes), std::string("Roma"));
	CHECK_EQ(RoomNameClean("Ro\xc3", kRoomNameMaxBytes), std::string("Ro"));
	CHECK_EQ(RoomNameClean("\xc0\xafRoma", kRoomNameMaxBytes), std::string("Roma"));
	CHECK_EQ(RoomNameClean("\xed\xa0\x80Roma", kRoomNameMaxBytes), std::string("Roma"));
}

void CutsAtAWholeCharacter()
{
	CHECK_EQ(RoomNameClean(std::string(300, 'a'), kRoomNameMaxBytes).size(), kRoomNameMaxBytes);
	CHECK_EQ(RoomNameClean("abcñ", 4), std::string("abc"));
	CHECK_EQ(RoomNameClean("abcñ", 5), std::string("abcñ"));
	CHECK_EQ(RoomNameClean("ab  cd", 4), std::string("ab"));
	const std::string lan = RoomNameClean(std::string(70, 'x'), kLanRoomNameMaxBytes);
	CHECK_EQ(lan.size(), kLanRoomNameMaxBytes);
}

}

int main()
{
	KeepsANormalName();
	TrimsAndDropsControlCharacters();
	DropsInvalidUtf8();
	CutsAtAWholeCharacter();
	return test::Summary("roomname");
}
