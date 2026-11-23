// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "net/roomcontent.h"

namespace {

RoomContentFacts Facts(const std::string &room, const std::string &own, const std::string &mapMod = "", bool haveMap = false, bool simple = false)
{
	RoomContentFacts f;
	f.roomKey = room;
	f.ownKey = own;
	f.mapModId = mapMod;
	f.haveMapMod = haveMap;
	f.mapSimple = simple;
	return f;
}

void SameKeysAndNoMapModAreTheSame()
{
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "685eb718eafca4a3")) == RoomContent::kSame);
}

void DifferentKeysAreOtherGameplay()
{
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "1f00aa0000000000")) == RoomContent::kOtherGameplay);
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "1f00aa0000000000", "numida", false)) == RoomContent::kOtherGameplay);
}

void AMissingKeyIsNeverTheSame()
{
	CHECK(CompareRoomContent(Facts("", "685eb718eafca4a3")) == RoomContent::kChecking);
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "")) == RoomContent::kChecking);
	CHECK(CompareRoomContent(Facts("", "")) == RoomContent::kChecking);
}

void TheMapModMustBeLoaded()
{
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "685eb718eafca4a3", "numida", false)) == RoomContent::kOtherMap);
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "685eb718eafca4a3", "numida", true)) == RoomContent::kSame);
}

void AMapOnlyPackIsNotNeededToJoin()
{
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "685eb718eafca4a3", "numida", false, true)) == RoomContent::kSame);
	CHECK(CompareRoomContent(Facts("685eb718eafca4a3", "1f00aa0000000000", "numida", false, true)) == RoomContent::kOtherGameplay);
	CHECK(CompareRoomContent(Facts("", "685eb718eafca4a3", "numida", false, true)) == RoomContent::kChecking);
}

void AMissingMapModIsKnownBeforeTheKeys()
{
	CHECK(CompareRoomContent(Facts("", "", "numida", false)) == RoomContent::kOtherMap);
	CHECK(CompareRoomContent(Facts("", "685eb718eafca4a3", "numida", true)) == RoomContent::kChecking);
}

void OnlyTheTwoDifferencesDiffer()
{
	CHECK(RoomContentDiffers(RoomContent::kOtherGameplay));
	CHECK(RoomContentDiffers(RoomContent::kOtherMap));
	CHECK(!RoomContentDiffers(RoomContent::kSame));
	CHECK(!RoomContentDiffers(RoomContent::kChecking));
}

void AKeyNotReadyNeverLetsAPlayerIn()
{
	CHECK(RoomContentBlocksEntry(CompareRoomContent(Facts("685eb718eafca4a3", ""))));
	CHECK(RoomContentBlocksEntry(CompareRoomContent(Facts("", "685eb718eafca4a3"))));
	CHECK(RoomContentBlocksEntry(CompareRoomContent(Facts("", ""))));
	CHECK(RoomContentBlocksEntry(RoomContent::kOtherGameplay));
	CHECK(RoomContentBlocksEntry(RoomContent::kOtherMap));
	CHECK(!RoomContentBlocksEntry(CompareRoomContent(Facts("685eb718eafca4a3", "685eb718eafca4a3"))));
}

}

int main()
{
	SameKeysAndNoMapModAreTheSame();
	DifferentKeysAreOtherGameplay();
	AMissingKeyIsNeverTheSame();
	TheMapModMustBeLoaded();
	AMapOnlyPackIsNotNeededToJoin();
	AMissingMapModIsKnownBeforeTheKeys();
	OnlyTheTwoDifferencesDiffer();
	AKeyNotReadyNeverLetsAPlayerIn();
	return test::Summary("roomcontent");
}
