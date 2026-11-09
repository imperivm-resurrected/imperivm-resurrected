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

void TheHashKeyIsTheStartOfTheHash()
{
	const std::string hash = "685eb718eafca4a3f00dbeef0123456789abcdef0123456789abcdef01234567";
	CHECK_EQ(RoomHashKey(hash), "685eb718eafca4a3");
	CHECK_EQ(RoomHashKey(""), "");
	CHECK_EQ(RoomHashKey("abc"), "abc");
}

// The room "key" of a host with civilizations is not the start of its hash, so a joiner that checks the content it
// received before restarting has to compare against the plain hash key.
void TheSyncCheckIgnoresTheCivilizationTable()
{
	const std::string hash = "685eb718eafca4a3f00dbeef0123456789abcdef0123456789abcdef01234567";
	const std::string hostKeyWithCivs = "1f00aa0000000001";
	CHECK(RoomHashKey(hash) != hostKeyWithCivs);
	CHECK(RoomHashMatches(hash, RoomHashKey(hash)));
	CHECK(!RoomHashMatches(hash, "1f00aa0000000000"));
	CHECK(!RoomHashMatches("", "685eb718eafca4a3"));
}

void AnOlderHostPublishesNoHashKey()
{
	CHECK(RoomHashMatches("685eb718eafca4a3f00d", ""));
}

void AGuestFollowsTheRoomsRules()
{
	// Equal keys, or a key not known yet, change nothing.
	CHECK(RoomRulesDecide("aa", "aa", false, 0) == RoomRulesStep::kInSync);
	CHECK(RoomRulesDecide("", "bb", false, 0) == RoomRulesStep::kInSync);
	CHECK(RoomRulesDecide("aa", "", true, 99999) == RoomRulesStep::kInSync);
	// Different keys: try to switch first, whatever the time.
	CHECK(RoomRulesDecide("aa", "bb", false, 0) == RoomRulesStep::kTryAdopt);
	CHECK(RoomRulesDecide("aa", "bb", false, 99999) == RoomRulesStep::kTryAdopt);
	// It was not possible: wait for the host's second update, then leave.
	CHECK(RoomRulesDecide("aa", "bb", true, 0) == RoomRulesStep::kWait);
	CHECK(RoomRulesDecide("aa", "bb", true, kRoomRulesGraceMs - 1) == RoomRulesStep::kWait);
	CHECK(RoomRulesDecide("aa", "bb", true, kRoomRulesGraceMs) == RoomRulesStep::kLeave);
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
	TheHashKeyIsTheStartOfTheHash();
	TheSyncCheckIgnoresTheCivilizationTable();
	AnOlderHostPublishesNoHashKey();
	AGuestFollowsTheRoomsRules();
	return test::Summary("roomcontent");
}
