// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/statshistory.h"

static StatSample At(uint32_t ms, int value = 0)
{
	StatSample s;
	s.ms = ms;
	s.values[kStatScore] = value;
	return s;
}

static void AddAndClear()
{
	StatsHistory h;
	CHECK(h.Empty());
	h.Add(2, At(1000));
	h.Add(2, At(2000));
	CHECK(!h.Empty());
	CHECK_EQ(static_cast<int>(h.Samples(2).size()), 2);
	CHECK_EQ(static_cast<int>(h.Samples(3).size()), 0);
	CHECK_EQ(h.LastMs(), 2000u);
	h.Clear();
	CHECK(h.Empty());
	CHECK_EQ(h.LastMs(), 0u);
}

static void IgnoresBadPlayers()
{
	StatsHistory h;
	h.Add(-1, At(1));
	h.Add(kStatMaxPlayers, At(1));
	CHECK(h.Empty());
	CHECK(h.Samples(-1).empty());
	CHECK(h.Samples(kStatMaxPlayers).empty());
}

static void SameTimeReplaces()
{
	StatsHistory h;
	h.Add(0, At(1000, 5));
	h.Add(0, At(1000, 7));
	CHECK_EQ(static_cast<int>(h.Samples(0).size()), 1);
	CHECK_EQ(h.Samples(0)[0].values[kStatScore], 7);
}

static void StaysBoundedAndKeepsTheEnds()
{
	StatsHistory h;
	const uint32_t first = h.IntervalMs();
	uint32_t ms = 0;
	for (size_t i = 0; i < StatsHistory::kMaxSamples * 5; i++, ms += 1000) {
		h.Add(0, At(ms));
		h.Add(1, At(ms));
		CHECK(h.Samples(0).size() < StatsHistory::kMaxSamples);
		CHECK(h.Samples(1).size() < StatsHistory::kMaxSamples);
	}
	CHECK(h.IntervalMs() > first);
	CHECK_EQ(h.Samples(0).front().ms, 0u);
	CHECK_EQ(h.Samples(0).back().ms, ms - 1000);
	for (size_t i = 1; i < h.Samples(0).size(); i++) CHECK(h.Samples(0)[i - 1].ms < h.Samples(0)[i].ms);
}

static void FindsTheSampleAtATime()
{
	std::vector<StatSample> samples = {At(1000, 1), At(2000, 2), At(3000, 3)};
	CHECK(StatsSampleAt({}, 5) == nullptr);
	CHECK_EQ(StatsSampleAt(samples, 0)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 1000)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 1999)->values[kStatScore], 1);
	CHECK_EQ(StatsSampleAt(samples, 2000)->values[kStatScore], 2);
	CHECK_EQ(StatsSampleAt(samples, 9000)->values[kStatScore], 3);
}

static void AxisIsRoundAndCoversTheMaximum()
{
	CHECK_EQ(StatsNiceAxis(0).top, 4);
	CHECK_EQ(StatsNiceAxis(-5).top, 4);
	CHECK_EQ(StatsNiceAxis(1).step, 1);
	CHECK_EQ(StatsNiceAxis(100).step, 50);
	CHECK_EQ(StatsNiceAxis(100).top, 100);
	CHECK_EQ(StatsNiceAxis(2150).step, 1000);
	CHECK_EQ(StatsNiceAxis(2150).top, 3000);
	for (int maximum : {3, 7, 19, 240, 999, 1001, 54321, 2000000}) {
		const StatsAxis axis = StatsNiceAxis(maximum);
		CHECK(axis.top >= maximum);
		CHECK_EQ(axis.top % axis.step, 0);
		CHECK(axis.top / axis.step <= 8);
	}
}

static void FormatsTexts()
{
	CHECK_EQ(StatsFormatTime(0), std::string("0:00"));
	CHECK_EQ(StatsFormatTime(65000), std::string("1:05"));
	CHECK_EQ(StatsFormatTime(3729000), std::string("1:02:09"));
	CHECK_EQ(StatsFormatShort(950), std::string("950"));
	CHECK_EQ(StatsFormatShort(9999), std::string("9999"));
	CHECK_EQ(StatsFormatShort(12500), std::string("12.5k"));
	CHECK_EQ(StatsFormatShort(3200000), std::string("3.2M"));
	CHECK_EQ(StatsFormatShort(-12500), std::string("-12.5k"));
}

int main()
{
	AddAndClear();
	IgnoresBadPlayers();
	SameTimeReplaces();
	StaysBoundedAndKeepsTheEnds();
	FindsTheSampleAtATime();
	AxisIsRoundAndCoversTheMaximum();
	FormatsTexts();
	return test::Summary("statshistory");
}
