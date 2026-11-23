// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/leagueseats.h"

namespace {

const uint64_t kMe = 76561198000000001ull;
const uint64_t kA = 76561198000000002ull;
const uint64_t kB = 76561198000000003ull;
const uint64_t kC = 76561198000000004ull;

void TheAddressGivesTheRoomNumberInBothOrders()
{
	CHECK_EQ(RoomNumberOfAddress(0x7f4d0003u), 3);
	CHECK_EQ(RoomNumberOfAddress(0x03004d7fu), 3);
	CHECK_EQ(RoomNumberOfAddress(0x7f4d00ffu), 255);
	CHECK_EQ(RoomNumberOfAddress(0xc0a80102u), 0);
	CHECK_EQ(RoomNumberOfAddress(0x0100007fu), 0);
	CHECK_EQ(RoomNumberOfAddress(0u), 0);
}

void TheRoomNumberWinsOverTheName()
{
	const std::vector<SeatMember> members = {{kMe, "Gladiator", 1}, {kA, "Gladiator_2", 2}, {kB, "Gladiator", 3}};
	CHECK_EQ(PickSeatSteamId(3, "Gladiator", members, kMe, {}, {}), kB);
	CHECK_EQ(PickSeatSteamId(2, "Gladiator", members, kMe, {}, {}), kA);
}

void NeverThisPlayersIdNorOneTaken()
{
	const std::vector<SeatMember> members = {{kMe, "Marcus", 1}, {kA, "marcus", 2}};
	CHECK_EQ(PickSeatSteamId(0, "Marcus", members, kMe, {}, {}), kA);
	CHECK_EQ(PickSeatSteamId(0, "Marcus", members, kMe, {kA}, {}), 0ull);
	CHECK_EQ(PickSeatSteamId(1, "Marcus", members, kMe, {}, {}), kA);
}

void APrefixCountsOnlyWithOneCandidate()
{
	const std::vector<SeatMember> two = {{kA, "Gladiator_2", 0}, {kB, "Gladiator_3", 0}};
	CHECK_EQ(PickSeatSteamId(0, "Gladiator", two, kMe, {}, {}), 0ull);
	const std::vector<SeatMember> one = {{kA, "Gladiator_2", 0}, {kB, "Brutus", 0}};
	CHECK_EQ(PickSeatSteamId(0, "Gladiator", one, kMe, {}, {}), kA);
	const std::vector<SeatMember> same = {{kA, "Brutus", 0}, {kB, "brutus", 0}};
	CHECK_EQ(PickSeatSteamId(0, "Brutus", same, kMe, {}, {}), 0ull);
	const std::vector<SeatMember> twice = {{kA, "Brutus", 0}, {kA, "Brutus", 4}};
	CHECK_EQ(PickSeatSteamId(0, "Brutus", twice, kMe, {}, {}), kA);
}

void OnlyTheTicketsPlayersWhenItIsKnown()
{
	const std::vector<SeatMember> members = {{kA, "Brutus", 2}, {kC, "Brutus", 0}};
	CHECK_EQ(PickSeatSteamId(2, "Brutus", members, kMe, {}, {kMe, kC}), kC);
	CHECK_EQ(PickSeatSteamId(0, "Nobody", members, kMe, {}, {kMe, kC}), 0ull);
	CHECK_EQ(PickSeatSteamId(0, "", members, kMe, {}, {}), 0ull);
}

void AReportIsTriedAgainOnlyWhenItMayStillGo()
{
	CHECK(LeagueReportRetry(0, true));
	CHECK(LeagueReportRetry(200, true));
	CHECK(LeagueReportRetry(201, true));
	CHECK(!LeagueReportRetry(200, false));
	CHECK(LeagueReportRetry(401, false));
	CHECK(LeagueReportRetry(408, false));
	CHECK(LeagueReportRetry(429, false));
	CHECK(LeagueReportRetry(503, false));
	CHECK(!LeagueReportRetry(400, false));
	CHECK(!LeagueReportRetry(403, false));
	CHECK(!LeagueReportRetry(409, false));
	CHECK(!LeagueReportRetry(426, false));
}

}

int main()
{
	TheAddressGivesTheRoomNumberInBothOrders();
	TheRoomNumberWinsOverTheName();
	NeverThisPlayersIdNorOneTaken();
	APrefixCountsOnlyWithOneCandidate();
	OnlyTheTicketsPlayersWhenItIsKnown();
	AReportIsTriedAgainOnlyWhenItMayStillGo();
	return test::Summary("leagueseats");
}
