// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/framepace.h"

static void Interval()
{
	CHECK(FrameInterval(600, 60) == 10);
	CHECK(FrameInterval(10000000, 144) == 69444);
	CHECK(FrameInterval(10000000, 0) == 0);
	CHECK(FrameInterval(0, 60) == 0);
}

static void Pace()
{
	int64_t deadline = 0;
	CHECK(FramePaceWait(deadline, 100, 10) == 0);
	CHECK(deadline == 110);
	CHECK(FramePaceWait(deadline, 103, 10) == 7);
	CHECK(deadline == 120);
	CHECK(FramePaceWait(deadline, 120, 10) == 0);
	// A little late: the next one keeps the beat, so the average stays at the limit.
	CHECK(FramePaceWait(deadline, 135, 10) == 0);
	CHECK(deadline == 140);
	CHECK(FramePaceWait(deadline, 141, 10) == 0);
	CHECK(deadline == 150);
	// Late by a whole interval: start again from now.
	CHECK(FramePaceWait(deadline, 170, 10) == 0);
	CHECK(deadline == 180);
	CHECK(FramePaceWait(deadline, 171, 10) == 9);
	// No limit.
	CHECK(FramePaceWait(deadline, 172, 0) == 0);
	CHECK(deadline == 0);
}

static void Average()
{
	// Asked as soon as each image is shown, 60 per second come out of any faster loop.
	int64_t deadline = 0, now = 0;
	int shown = 0;
	const int64_t interval = FrameInterval(1000000, 60);
	while (now < 1000000) {
		now += FramePaceWait(deadline, now, interval);
		shown++;
		now += 1000;
	}
	CHECK(shown >= 59 && shown <= 61);
}

int main()
{
	Interval();
	Pace();
	Average();
	return test::Summary("framepace");
}
