// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include "check.h"

#include <vector>

#include "ui/screenscale.h"
#include "ui/scrollpane.h"

using namespace ui;

static bool Near(float a, float b) { return a > b - 0.001f && a < b + 0.001f; }

static void ScreensFollowTheHeight()
{
	ScreenView v = ScreenViewFor(1920, 1080);
	CHECK(Near(v.s, 1.0f));
	CHECK_EQ(v.w, 1920);
	CHECK_EQ(v.h, 1080);
	// 1440p and 4K show the 1080p layout larger, not more of it.
	v = ScreenViewFor(2560, 1440);
	CHECK(Near(v.s, 1.3333f));
	CHECK_EQ(v.w, 1920);
	CHECK_EQ(v.h, 1080);
	v = ScreenViewFor(3840, 2160);
	CHECK(Near(v.s, 2.0f));
	CHECK_EQ(v.w, 1920);
	CHECK_EQ(v.h, 1080);
	v = ScreenViewFor(7680, 4320);
	CHECK(Near(v.s, 4.0f));
	CHECK_EQ(v.h, 1080);
	// An ultrawide gets the 1080p height and more width.
	v = ScreenViewFor(3440, 1440);
	CHECK_EQ(v.h, 1080);
	CHECK_EQ(v.w, 2580);
}

static void SmallScreensKeepTheTextReadable()
{
	// Below 0.8 the captions would not read: a short screen gets fewer units instead.
	ScreenView v = ScreenViewFor(1280, 720);
	CHECK(Near(v.s, 0.8f));
	CHECK_EQ(v.w, 1600);
	CHECK_EQ(v.h, 900);
	v = ScreenViewFor(1366, 768);
	CHECK(Near(v.s, 0.8f));
	CHECK_EQ(v.w, 1708);
	CHECK_EQ(v.h, 960);
	v = ScreenViewFor(1024, 768);
	CHECK(Near(v.s, 0.8f));
	CHECK_EQ(v.w, 1280);
	CHECK_EQ(v.h, 960);
	// Never narrower than 1200 units: a narrow screen goes under the readable scale rather than lose its columns.
	v = ScreenViewFor(800, 600);
	CHECK(Near(v.s, 0.6667f));
	CHECK_EQ(v.w, 1200);
	CHECK_EQ(v.h, 900);
	CHECK(Near(ScreenViewFor(640, 480).s, kMinScale));
}

static void WindowsGrowWithTheScreen()
{
	// The options window, 1100x680 with 16 pixels around it.
	CHECK(Near(FitScale(1920, 1080, 1100, 680, 16), 1064 / 680.0f));
	CHECK(Near(FitScale(1280, 720, 1100, 680, 16), 704 / 680.0f));
	CHECK(Near(FitScale(1024, 768, 1100, 680, 16), 1008 / 1100.0f));
	// A small window stops at 1.6 on a 1080p screen, and keeps its share of a larger one.
	CHECK(Near(FitScale(1920, 1080, 400, 300), 1.6f));
	CHECK(Near(FitScale(2560, 1440, 400, 300), 1.6f * 1.3333f));
	CHECK(Near(FitScale(3840, 2160, 400, 300), 3.2f));
	CHECK(Near(FitScale(3840, 2160, 1100, 680, 16), 2144 / 680.0f));
	// One that can't fit stops at the floor.
	CHECK(Near(FitScale(800, 600, 2000, 1500), kMinScale));
	CHECK(Near(MaxWindowScale(1280, 720), kWindowGrowth));
	CHECK(Near(ResolutionFactor(3440, 1440), 1.3333f));
}

static void HeadersTighten()
{
	ScreenLayout l = ScreenLayoutFor(1920, 1080);
	CHECK_EQ(l.margin, 64);
	CHECK(l.roomy);
	CHECK_EQ(l.top, 216);
	l = ScreenLayoutFor(1600, 900);
	CHECK_EQ(l.margin, 53);
	CHECK(!l.roomy);
	CHECK_EQ(l.top, 177);
	l = ScreenLayoutFor(1280, 960);
	CHECK_EQ(l.margin, 42);
	CHECK_EQ(l.top, 166);
	l = ScreenLayoutFor(1920, 1080, false);
	CHECK_EQ(l.infoH, 0);
	CHECK_EQ(l.top, 182);
}

static void RowsShortenBeforeScrolling()
{
	// 11 rows of 56 and 124 of cards fit 740 as they are.
	CHECK_EQ(FitRowHeight(740, 124, 11, 56, 46), 56);
	// In 728 they shorten to 54.
	CHECK_EQ(FitRowHeight(728, 124, 11, 56, 46), 54);
	// Never under the minimum: the rest scrolls.
	CHECK_EQ(FitRowHeight(500, 124, 11, 56, 46), 46);
	CHECK_EQ(FitRowHeight(100, 124, 11, 56, 46), 46);
	CHECK_EQ(FitRowHeight(100, 0, 0, 56, 46), 56);
	CHECK_EQ(PaneMax(900, 600), 300);
	CHECK_EQ(PaneMax(500, 600), 0);
	CHECK_EQ(PaneMax(500, -10), 500);
	CHECK_EQ(PaneWheelStep(240), 40);
	CHECK_EQ(PaneWheelStep(640), 80);
	CHECK_EQ(PaneWheelStep(2000), 120);
}

static void TheWheelScrollsThePaneUnderIt()
{
	std::vector<PaneArea> panes = {{{0, 0, 1000, 800}, kPaneIdBase, 300, 640}, {{100, 100, 400, 400}, kPaneIdBase + 1, 50, 300}};
	PaneScrolls s;
	// The inner pane first, down one notch.
	CHECK(s.Wheel(panes, {200, 200}, -WHEEL_DELTA));
	CHECK_EQ(s.Of(kPaneIdBase + 1), 40);
	CHECK(s.Wheel(panes, {200, 200}, -WHEEL_DELTA));
	CHECK_EQ(s.Of(kPaneIdBase + 1), 50);
	// Outside it, the outer one.
	CHECK(s.Wheel(panes, {700, 700}, -WHEEL_DELTA));
	CHECK_EQ(s.Of(kPaneIdBase), 80);
	CHECK(s.Wheel(panes, {700, 700}, 3 * WHEEL_DELTA));
	CHECK_EQ(s.Of(kPaneIdBase), 0);
	// A touchpad's small steps add up to a notch.
	CHECK(s.Wheel(panes, {700, 700}, -60));
	CHECK_EQ(s.Of(kPaneIdBase), 0);
	CHECK(s.Wheel(panes, {700, 700}, -60));
	CHECK_EQ(s.Of(kPaneIdBase), 80);
	// Nothing to scroll there.
	CHECK(!s.Wheel(panes, {2000, 2000}, -WHEEL_DELTA));
	panes[0].max = 0;
	CHECK(!s.Wheel(panes, {700, 700}, -WHEEL_DELTA));
	s.Reset();
	CHECK_EQ(s.Of(kPaneIdBase + 1), 0);
}

static void TheBarAndRevealStayInRange()
{
	const std::vector<PaneArea> panes = {{{0, 0, 100, 100}, kPaneIdBase, 300, 200}};
	PaneScrolls s;
	CHECK(s.Apply(panes, {kPaneIdBase, 250}));
	CHECK_EQ(s.Of(kPaneIdBase), 250);
	CHECK(s.Apply(panes, {kPaneIdBase, 900}));
	CHECK_EQ(s.Of(kPaneIdBase), 300);
	CHECK(!s.Apply(panes, {900, 10}));
	CHECK(!s.Apply(panes, {}));
	s.Set(kPaneIdBase, 0, 300);
	// Down to show a row under the view, back up to one over it, and no move for one in view.
	s.Reveal(panes, kPaneIdBase, 380, 40);
	CHECK_EQ(s.Of(kPaneIdBase), 220);
	s.Reveal(panes, kPaneIdBase, 300, 40);
	CHECK_EQ(s.Of(kPaneIdBase), 220);
	s.Reveal(panes, kPaneIdBase, 100, 40);
	CHECK_EQ(s.Of(kPaneIdBase), 100);
}

static void HitsOutsideThePaneAreDropped()
{
	std::vector<Hit> hits = {{{0, 0, 50, 50}, 1, 0}, {{10, 90, 60, 130}, 2, 0}, {{10, 150, 60, 190}, 3, 0}, {{10, 300, 60, 340}, 4, 0}};
	// The first hit is from before the pane and stays as it was.
	KeepHitsInside(hits, 1, {0, 100, 200, 200});
	CHECK_EQ(static_cast<int>(hits.size()), 3);
	CHECK_EQ(hits[0].action, 1);
	CHECK_EQ(static_cast<int>(hits[0].r.top), 0);
	// Cut to the pane where it overlaps its edge.
	CHECK_EQ(hits[1].action, 2);
	CHECK_EQ(static_cast<int>(hits[1].r.top), 100);
	CHECK_EQ(hits[2].action, 3);
}

int main()
{
	ScreensFollowTheHeight();
	SmallScreensKeepTheTextReadable();
	WindowsGrowWithTheScreen();
	HeadersTighten();
	RowsShortenBeforeScrolling();
	TheWheelScrollsThePaneUnderIt();
	TheBarAndRevealStayInRange();
	HitsOutsideThePaneAreDropped();
	return test::Summary("layout");
}
