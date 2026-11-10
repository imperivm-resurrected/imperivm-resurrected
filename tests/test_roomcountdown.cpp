// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <vector>

#include "game/roomcountdown.h"

namespace {

RoomCountdownInput Room(bool allReady, std::vector<uint32_t> parts = {1, 2, 3}, bool blocked = false)
{
	RoomCountdownInput in;
	in.allReady = allReady;
	in.blocked = blocked;
	in.parts = parts;
	return in;
}

bool Is(const CountdownStep &s, CountdownEvent e) { return s.event == e; }

void StartsWhenEveryoneIsReady()
{
	RoomCountdown c;
	CHECK(Is(c.HostStep(Room(false), 1000, true), CountdownEvent::None));
	CHECK_EQ(c.LeftMs(1000), -1);
	CHECK(Is(c.HostStep(Room(true), 1100, true), CountdownEvent::Start));
	CHECK_EQ(c.LeftMs(1100), kRoomCountdownMs);
	CHECK_EQ(c.LeftMs(3100), kRoomCountdownMs - 2000);
	CHECK(Is(c.HostStep(Room(true), 3100, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true), 1100 + kRoomCountdownMs, true), CountdownEvent::Fire));
	CHECK_EQ(c.LeftMs(1100 + kRoomCountdownMs), -1);
	CHECK(Is(c.HostStep(Room(true), 20000, true), CountdownEvent::None));
}

void NeedsAutoStart()
{
	RoomCountdown c;
	CHECK(Is(c.HostStep(Room(true), 1000, false), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true), 1100, true), CountdownEvent::Start));
	const CountdownStep off = c.HostStep(Room(true), 1200, false);
	CHECK(Is(off, CountdownEvent::Cancel));
	CHECK(off.reason == CountdownReason::Host);
}

void CancelsWhenSomeoneIsNotReady()
{
	RoomCountdown c;
	c.HostStep(Room(true), 1000, true);
	const uint32_t seq = c.Seq();
	const CountdownStep s = c.HostStep(Room(false), 2000, true);
	CHECK(Is(s, CountdownEvent::Cancel));
	CHECK(s.reason == CountdownReason::NotReady);
	CHECK(c.Seq() != seq);
	CHECK_EQ(c.LeftMs(2000), -1);
	CHECK(Is(c.HostStep(Room(true), 2100, true), CountdownEvent::Start));
}

void CancelsOnChangeAndWaitsForTheRoomToSettle()
{
	RoomCountdown c;
	c.HostStep(Room(true, {1, 2, 3}), 1000, true);
	const CountdownStep s = c.HostStep(Room(true, {1, 9, 3}), 1500, true);
	CHECK(Is(s, CountdownEvent::Cancel));
	CHECK(s.reason == CountdownReason::Changed);
	CHECK_EQ(s.part, 1);
	CHECK(Is(c.HostStep(Room(true, {1, 9, 3}), 1500 + kRoomCountdownSettleMs - 1, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true, {1, 9, 3}), 1500 + kRoomCountdownSettleMs, true), CountdownEvent::Start));
	const CountdownStep grew = c.HostStep(Room(true, {1, 9, 3, 4}), 3000, true);
	CHECK(grew.reason == CountdownReason::Changed);
	CHECK_EQ(grew.part, 3);
}

void CancelsWhenBlocked()
{
	RoomCountdown c;
	CHECK(Is(c.HostStep(Room(true, {1}, true), 1000, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true, {1}), 1100, true), CountdownEvent::Start));
	const CountdownStep s = c.HostStep(Room(true, {1}, true), 1200, true);
	CHECK(Is(s, CountdownEvent::Cancel));
	CHECK(s.reason == CountdownReason::Blocked);
}

void HostCancelHoldsUntilSomeoneUnreadies()
{
	RoomCountdown c;
	c.HostStep(Room(true), 1000, true);
	CHECK(Is(c.HostCancel(), CountdownEvent::Cancel));
	CHECK(Is(c.HostStep(Room(true), 5000, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true, {7, 7}), 9000, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(false, {7, 7}), 9100, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true, {7, 7}), 9200, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true, {7, 7}), 9000 + kRoomCountdownSettleMs, true), CountdownEvent::Start));
	CHECK(Is(c.HostCancel(), CountdownEvent::Cancel));
	CHECK(Is(c.HostCancel(), CountdownEvent::None));
}

void NoRestartAfterFiringUntilSomeoneUnreadies()
{
	RoomCountdown c;
	c.HostStep(Room(true), 1000, true);
	CHECK(Is(c.HostStep(Room(true), 1000 + kRoomCountdownMs, true), CountdownEvent::Fire));
	CHECK(Is(c.HostStep(Room(true), 30000, true), CountdownEvent::None));
	c.HostStep(Room(false), 30100, true);
	CHECK(Is(c.HostStep(Room(true), 30200, true), CountdownEvent::Start));
}

void SurvivesTheTickWrap()
{
	RoomCountdown c;
	const uint32_t now = 0xFFFFF000u;
	CHECK(Is(c.HostStep(Room(true), now, true), CountdownEvent::Start));
	CHECK_EQ(c.LeftMs(now + 1000), kRoomCountdownMs - 1000);
	CHECK(Is(c.HostStep(Room(true), now + 4999, true), CountdownEvent::None));
	CHECK(Is(c.HostStep(Room(true), now + 5000, true), CountdownEvent::Fire));
}

void GuestFollowsTheHost()
{
	RoomCountdown g;
	CHECK(g.RemoteStart(1, 4900, 1000));
	CHECK_EQ(g.LeftMs(1000), 4900);
	CHECK(!g.RemoteStart(1, 4900, 1200));
	CHECK(g.RemoteCancel(2));
	CHECK_EQ(g.LeftMs(1300), -1);
	CHECK(!g.RemoteStart(2, 5000, 1400));
	CHECK(g.RemoteStart(3, 5000, 1500));
	CHECK(!g.RemoteCancel(1));
	CHECK_EQ(g.LeftMs(1500), 5000);
	CHECK_EQ(g.LeftMs(9000), 0);
	CHECK(Is(g.GuestStep(true, 9000), CountdownEvent::None));
	CHECK(Is(g.GuestStep(true, 6500 + kRoomCountdownGraceMs), CountdownEvent::Cancel));
	CHECK_EQ(g.LeftMs(6500 + kRoomCountdownGraceMs), -1);
}

void GuestStopsWhenNotReady()
{
	RoomCountdown g;
	g.RemoteStart(5, 5000, 0);
	const CountdownStep s = g.GuestStep(false, 100);
	CHECK(Is(s, CountdownEvent::Cancel));
	CHECK(s.reason == CountdownReason::NotReady);
	CHECK(!g.RemoteCancel(6));
	CHECK(g.RemoteStart(7, 5000, 200));
	CHECK(g.RemoteStart(8, -40, 300));
	CHECK_EQ(g.LeftMs(300), 0);
}

void ResetForgetsEverything()
{
	RoomCountdown c;
	c.HostStep(Room(true), 1000, true);
	c.Reset();
	CHECK(!c.Counting());
	CHECK_EQ(c.Seq(), 0u);
	CHECK(Is(c.HostStep(Room(true), 1100, true), CountdownEvent::Start));
}

}

int main()
{
	StartsWhenEveryoneIsReady();
	NeedsAutoStart();
	CancelsWhenSomeoneIsNotReady();
	CancelsOnChangeAndWaitsForTheRoomToSettle();
	CancelsWhenBlocked();
	HostCancelHoldsUntilSomeoneUnreadies();
	NoRestartAfterFiringUntilSomeoneUnreadies();
	SurvivesTheTickWrap();
	GuestFollowsTheHost();
	GuestStopsWhenNotReady();
	ResetForgetsEverything();
	return test::Summary("roomcountdown");
}
