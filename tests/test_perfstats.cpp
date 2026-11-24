// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/perfstats.h"

#include <cmath>

using namespace perf;

static bool Near(double a, double b, double tolerance = 0.01)
{
	return std::fabs(a - b) <= tolerance;
}

// Frames `ms` apart from `t`, `n` of them; returns the time of the last.
static double Feed(FrameTracker &f, double t, double ms, int n, bool continuous)
{
	for (int i = 0; i < n; i++) {
		t += ms;
		f.AddFrame(t, continuous);
	}
	return t;
}

static FrameSummary All(const FrameTracker &f)
{
	std::vector<FrameRecord> frames;
	std::vector<float> scratch;
	f.CopySince(-1e18, frames);
	return Summarize(frames, -1e18, f.LastFrameMs(), scratch);
}

static void Percentiles()
{
	std::vector<float> v;
	for (int i = 1; i <= 100; i++) v.push_back(static_cast<float>(101 - i));
	CHECK(Percentile(v, 50) == 50.0f);
	CHECK(Percentile(v, 99) == 99.0f);
	CHECK(Percentile(v, 99.9) == 100.0f);
	CHECK(Percentile(v, 0) == 1.0f);
	CHECK(Percentile(v, 100) == 100.0f);
	std::vector<float> one = {7};
	CHECK(Percentile(one, 99) == 7.0f);
	std::vector<float> none;
	CHECK(Percentile(none, 50) == 0.0f);
	// 1000 frames: the 99th percentile is the 990th, the 99.9th the 999th.
	std::vector<float> k;
	for (int i = 1; i <= 1000; i++) k.push_back(static_cast<float>(i));
	CHECK(Percentile(k, 99) == 990.0f);
	CHECK(Percentile(k, 99.9) == 999.0f);
}

static void Steady()
{
	FrameTracker f;
	Feed(f, 0, 1000.0 / 60, 600, true);
	const FrameSummary s = All(f);
	CHECK(s.hitches == 0);
	CHECK(s.frames == 599);
	CHECK(Near(s.avgFps, 60, 0.1));
	CHECK(Near(s.low1Fps, 60, 0.1));
	CHECK(Near(f.MedianMs(), 16.667, 0.01));
	CHECK(Near(f.HitchThresholdMs(), 41.667, 0.01));
	CHECK(f.State(f.LastFrameMs() + 10, true) == ScreenState::kActive);
}

static void OneSpike()
{
	FrameTracker f;
	double t = Feed(f, 0, 1000.0 / 60, 120, true);
	t = Feed(f, t, 200, 1, true);
	t = Feed(f, t, 1000.0 / 60, 120, true);
	const FrameSummary s = All(f);
	CHECK(s.hitches == 1);
	CHECK(f.TotalHitches() == 1);
	CHECK(Near(s.maxMs, 200, 0.01));
	// 240 frames: the 99th percentile is still a normal one, the 99.9th is the spike.
	CHECK(Near(s.p99Ms, 16.667, 0.01));
	CHECK(Near(s.p999Ms, 200, 0.01));
	CHECK(Near(s.low01Fps, 5, 0.01));
}

static void PeriodicTurnFrames()
{
	// Every network turn (150 ms at 60 FPS, every 9th frame) takes a little longer: the game's pace, not a hitch.
	FrameTracker f;
	double t = 0;
	for (int i = 0; i < 400; i++) t = Feed(f, t, i % 9 == 8 ? 35.0 : 16.0, 1, true);
	CHECK(All(f).hitches == 0);
	// Twice the median and 25 ms more is the line: 45 ms every turn are hitches, each one.
	FrameTracker g;
	t = 0;
	int spikes = 0;
	for (int i = 0; i < 400; i++) {
		const bool spike = i % 9 == 8 && i > 20;
		spikes += spike ? 1 : 0;
		t = Feed(g, t, spike ? 45.0 : 16.0, 1, true);
	}
	CHECK(All(g).hitches == spikes);
}

static void SlowerMatch()
{
	// The match drops from 60 to 15 FPS and stays there: not a stream of hitches.
	FrameTracker f;
	double t = Feed(f, 0, 1000.0 / 60, 120, true);
	t = Feed(f, t, 66.0, 200, true);
	const FrameSummary s = All(f);
	CHECK(s.hitches <= 1);
	CHECK(Near(f.MedianMs(), 66.0, 0.01));
}

static void IdleMenu()
{
	// A menu draws on demand: a frame every few seconds, sometimes a short burst while the mouse moves.
	FrameTracker f;
	double t = 0;
	for (int i = 0; i < 30; i++) {
		t = Feed(f, t, 2500, 1, false);
		t = Feed(f, t, 30, 3, false);
		t = Feed(f, t, 400, 2, false);
	}
	const FrameSummary s = All(f);
	CHECK(s.hitches == 0);
	CHECK(s.frames == 0);
	CHECK(f.State(t + 1500, false) == ScreenState::kIdle);
	CHECK(f.State(t + 10, false) == ScreenState::kIdle);
}

static void MouseMenu()
{
	// The mouse moves over a menu: frames at an irregular pace, with pauses. Some frames may count, none is a hitch.
	FrameTracker f;
	double t = 0;
	const double pattern[] = {16, 18, 16, 33, 16, 17, 16, 16, 16, 120, 16, 16, 40, 16, 16, 16, 16, 16, 16, 16, 230, 17, 16};
	for (int round = 0; round < 40; round++) {
		for (double ms : pattern) t = Feed(f, t, ms, 1, false);
	}
	CHECK(All(f).hitches == 0);
}

static void AnimatedMenu()
{
	// A menu that animates draws continuously: it counts, and becomes idle when it stops.
	FrameTracker f;
	// The first frame only starts the clock: seven intervals are not enough, the eighth is.
	double t = Feed(f, 0, 16.0, 8, false);
	CHECK(f.State(t + 1, false) == ScreenState::kIdle);
	t = Feed(f, t, 16.0, 1, false);
	CHECK(f.State(t + 1, false) == ScreenState::kActive);
	t = Feed(f, t, 16.0, 100, false);
	CHECK(f.State(t + 1, false) == ScreenState::kActive);
	CHECK(f.State(t + 1200, false) == ScreenState::kIdle);
	t = Feed(f, t, 3000, 1, false);
	CHECK(f.State(t + 1, false) == ScreenState::kIdle);
	const FrameSummary s = All(f);
	CHECK(s.hitches == 0);
	CHECK(s.frames == 101);
}

static void WarmUp()
{
	// From a menu into a match: no hitch is judged until the median has enough frames.
	FrameTracker f;
	double t = Feed(f, 0, 3000, 3, false);
	t = Feed(f, t, 16.0, 3, true);
	t = Feed(f, t, 90.0, 1, true);
	t = Feed(f, t, 16.0, 5, true);
	CHECK(All(f).hitches == 0);
	t = Feed(f, t, 16.0, 20, true);
	t = Feed(f, t, 90.0, 1, true);
	t = Feed(f, t, 16.0, 2, true);
	CHECK(All(f).hitches == 1);
}

static void LongFreezes()
{
	FrameTracker f;
	double t = Feed(f, 0, 16.0, 100, true);
	// An autosave holds the match for a second and a half: a hitch.
	t = Feed(f, t, 1500, 1, true);
	t = Feed(f, t, 16.0, 30, true);
	CHECK(All(f).hitches == 1);
	CHECK(f.State(t + 2500, true) == ScreenState::kStalled);
	// Minimised for a minute: not a frame.
	t = Feed(f, t, 60000, 1, true);
	t = Feed(f, t, 16.0, 30, true);
	const FrameSummary s = All(f);
	CHECK(s.hitches == 1);
	CHECK(s.maxMs == 1500.0f);
}

static void Coalesce()
{
	FrameTracker f;
	double t = 0;
	for (int i = 0; i < 100; i++) {
		t += 16.0;
		CHECK(f.AddFrame(t, true));
		// The same image sent in two more pieces.
		CHECK(!f.AddFrame(t + 0.3, true));
		CHECK(!f.AddFrame(t + 0.9, true));
	}
	const FrameSummary s = All(f);
	CHECK(s.frames == 99);
	CHECK(Near(s.avgMs, 16.0, 0.01));
	CHECK(s.hitches == 0);
}

static void Window()
{
	FrameTracker f;
	double t = Feed(f, 0, 20.0, 50, true);
	t = Feed(f, t, 10.0, 100, true);
	std::vector<FrameRecord> frames;
	std::vector<float> scratch;
	f.CopySince(t - 1000, frames);
	CHECK(frames.size() == 100);
	const FrameSummary s = Summarize(frames, t - 1000, t, scratch);
	CHECK(s.frames == 100);
	CHECK(Near(s.avgFps, 100, 0.01));
	CHECK(Near(s.nowFps, 100, 0.01));
	CHECK(Near(s.activeShare, 1, 0.001));
	// The ring keeps the newest.
	FrameTracker tiny(16);
	Feed(tiny, 0, 10.0, 40, true);
	CHECK(tiny.Size() == 16);
	CHECK(Near(tiny.At(15).t, 400, 0.001));
	CHECK(Near(tiny.At(0).t, 250, 0.001));
}

static void Columns()
{
	std::vector<FrameRecord> frames;
	for (int i = 1; i <= 100; i++) frames.push_back({i * 10.0, 10.0f, kFrameCounted});
	// A 300 ms hitch that ends at 1300 and an idle frame.
	frames.push_back({1300.0, 300.0f, kFrameCounted | kFrameHitch});
	frames.push_back({1900.0, 600.0f, 0});
	std::vector<PerfEvent> events = {{1500.0, kEventDesync}};
	GraphColumn cols[20];
	BuildColumns(frames, events, 0, 2000, 20, cols);
	CHECK(cols[0].maxMs == 10.0f);
	CHECK(cols[0].count == 10);
	CHECK((cols[5].flags & kColumnFrames) != 0);
	CHECK((cols[5].flags & kColumnHitch) == 0);
	// The hitch fills the columns it lasted through, 1000 to 1300.
	for (int c = 10; c <= 12; c++) CHECK((cols[c].flags & kColumnHitch) && cols[c].maxMs == 300.0f);
	CHECK((cols[13].flags & kColumnHitch) == 0);
	CHECK((cols[14].flags & kColumnEvent) && cols[14].event == kEventDesync);
	CHECK((cols[17].flags & kColumnIdle) && !(cols[17].flags & kColumnFrames));
}

static void Cpu()
{
	CHECK(CpuPercent(50, 100, 1) == 50.0f);
	CHECK(CpuPercent(100, 100, 4) == 25.0f);
	CHECK(CpuPercent(900, 100, 4) == 100.0f);
	CHECK(CpuPercent(10, 0, 4) == 0.0f);
	CpuMeter m(10000000);
	CHECK(!m.Update(0, 0, 0, 4));
	CHECK(!m.Update(5000000, 8000000, 4000000, 4));
	CHECK(m.process < 0);
	// One second (in 100 ns): the process used 2 s of CPU on 4 cores, its game thread 0.75 s.
	CHECK(m.Update(10000000, 20000000, 7500000, 4));
	CHECK(Near(m.process, 50, 0.001));
	CHECK(Near(m.thread, 75, 0.001));
}

static void Csv()
{
	FrameTracker f;
	double t = Feed(f, 0, 16.0, 20, true);
	t = Feed(f, t, 100.0, 1, true);
	t = Feed(f, t, 16.0, 2, true);
	std::vector<FrameRecord> frames;
	std::vector<float> scratch;
	f.CopySince(0, frames);
	const FrameSummary s = Summarize(frames, 0, t, scratch);
	const std::vector<PerfEvent> events = {{50.0, kEventLoad}};
	const std::string csv = CaptureCsv(frames, events, 0, t, s, "Imperivm Resurrected 1.2.3\nmatch");
	CHECK(csv.rfind("# Imperivm Resurrected 1.2.3\n# match\n", 0) == 0);
	CHECK(csv.find("time_s,frame_ms,counted,hitch,event\n") != std::string::npos);
	CHECK(csv.find("0.0500,,,,load\n") != std::string::npos);
	CHECK(csv.find(",100.000,1,1,\n") != std::string::npos);
	CHECK(csv.find("1 hitches") != std::string::npos);
}

int main()
{
	Percentiles();
	Steady();
	OneSpike();
	PeriodicTurnFrames();
	SlowerMatch();
	IdleMenu();
	MouseMenu();
	AnimatedMenu();
	WarmUp();
	LongFreezes();
	Coalesce();
	Window();
	Columns();
	Cpu();
	Csv();
	return test::Summary("perfstats");
}
