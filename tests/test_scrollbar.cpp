// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include "check.h"

#include <vector>

#include "ui/listbox.h"
#include "ui/uikit.h"

using namespace ui;

static void ThumbSize()
{
	CHECK_EQ(ScrollThumb({0, 300, 100}, 200, 20).len, 50);
	CHECK_EQ(ScrollThumb({0, 10000, 10}, 200, 20).len, 20);
	// Rows: 8 of 20 shown in a track of 400.
	CHECK_EQ(ScrollThumb({0, 12, 8}, 400, 24).len, 160);
	// Nothing to scroll: the thumb fills the track.
	CHECK_EQ(ScrollThumb({0, 0, 100}, 200, 20).len, 200);
	CHECK_EQ(ScrollThumb({0, 50, 10}, 0, 20).len, 0);
	// A minimum longer than the track stays inside it.
	CHECK_EQ(ScrollThumb({0, 50, 10}, 12, 20).len, 12);
	// A page of 0 counts as one.
	CHECK_EQ(ScrollThumb({0, 99, 0}, 100, 1).len, 1);
}

static void ThumbPosition()
{
	CHECK_EQ(ScrollThumb({0, 300, 100}, 200, 20).top, 0);
	CHECK_EQ(ScrollThumb({300, 300, 100}, 200, 20).top, 150);
	CHECK_EQ(ScrollThumb({150, 300, 100}, 200, 20).top, 75);
	CHECK_EQ(ScrollThumb({-40, 300, 100}, 200, 20).top, 0);
	CHECK_EQ(ScrollThumb({900, 300, 100}, 200, 20).top, 150);
}

static void DragMapping()
{
	CHECK_EQ(ScrollAtThumb(300, 200, 50, 0), 0);
	CHECK_EQ(ScrollAtThumb(300, 200, 50, 150), 300);
	CHECK_EQ(ScrollAtThumb(300, 200, 50, 75), 150);
	CHECK_EQ(ScrollAtThumb(300, 200, 50, -30), 0);
	CHECK_EQ(ScrollAtThumb(300, 200, 50, 999), 300);
	// Rounded to the nearest: 1 pixel of 150 is 2 of 300.
	CHECK_EQ(ScrollAtThumb(300, 200, 50, 1), 2);
	CHECK_EQ(ScrollAtThumb(7, 200, 60, 10), 1);
	CHECK_EQ(ScrollAtThumb(7, 200, 60, 9), 0);
	CHECK_EQ(ScrollAtThumb(0, 200, 50, 40), 0);
	CHECK_EQ(ScrollAtThumb(300, 50, 50, 10), 0);
	// Where the thumb is painted is where a drag that does not move leaves it, when the track has a pixel for every step.
	for (int pos = 0; pos <= 37; pos++) {
		const ScrollSpan span = {pos, 37, 9};
		const ThumbSpan t = ScrollThumb(span, 300, 20);
		CHECK_EQ(ScrollAtThumb(span.max, 300, t.len, t.top), pos);
	}
}

static void Paging()
{
	CHECK_EQ(ScrollPaged({0, 300, 100}, 1), 100);
	CHECK_EQ(ScrollPaged({250, 300, 100}, 1), 300);
	CHECK_EQ(ScrollPaged({250, 300, 100}, -1), 150);
	CHECK_EQ(ScrollPaged({50, 300, 100}, -1), 0);
	CHECK_EQ(ScrollPaged({3, 10, 0}, 1), 4);
	CHECK_EQ(ScrollClamp(-5, 10), 0);
	CHECK_EQ(ScrollClamp(15, 10), 10);
	CHECK_EQ(ScrollClamp(4, -1), 0);
}

// A bar as PaintScrollbar leaves it: a track 200 high at y 50 and the thumb painted for `span`.
static Hit Bar(int id, const ScrollSpan &span)
{
	Hit h = {{100, 50, 114, 250}, kHitScrollbar, id};
	const ThumbSpan t = ScrollThumb(span, 200, 20);
	h.span = span;
	h.thumbTop = 50 + t.top;
	h.thumbLen = t.len;
	return h;
}

static void DragTheThumb()
{
	std::vector<Hit> hits = {{{0, 0, 800, 600}, 3, 0}, Bar(7, {0, 300, 100})};
	ScrollDrag drag;
	ScrollTo to;
	// Taken 10 pixels below the thumb's top: nothing scrolls yet.
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {106, 60}, to));
	CHECK_EQ(drag.id, 7);
	CHECK_EQ(drag.grab, 10);
	CHECK_EQ(to.id, -1);
	CHECK(ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {106, 135}, to));
	CHECK_EQ(to.id, 7);
	CHECK_EQ(to.pos, 150);
	// Far from the bar, and out of the window, the drag goes on and stops at the ends.
	CHECK(ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {-300, 1000}, to));
	CHECK_EQ(to.pos, 300);
	CHECK(ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {900, -500}, to));
	CHECK_EQ(to.pos, 0);
	// The next frame painted the content longer: the same mouse follows the new bar.
	hits[1] = Bar(7, {0, 600, 100});
	CHECK(ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {106, 135}, to));
	CHECK_EQ(to.pos, ScrollAtThumb(600, 200, hits[1].thumbLen, 75));
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONUP, 0, {106, 135}, to));
	CHECK_EQ(drag.id, -1);
	CHECK(!ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {106, 200}, to));
	CHECK_EQ(to.id, -1);
	CHECK(!ScrollbarMouse(drag, hits, WM_LBUTTONUP, 0, {106, 200}, to));
}

static void DragEnds()
{
	std::vector<Hit> hits = {Bar(2, {0, 300, 100})};
	ScrollDrag drag;
	ScrollTo to;
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {106, 50}, to));
	// The button came up where the window did not hear it.
	CHECK(!ScrollbarMouse(drag, hits, WM_MOUSEMOVE, 0, {106, 120}, to));
	CHECK_EQ(drag.id, -1);
	CHECK_EQ(to.id, -1);
	// The bar went away under the drag.
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {106, 50}, to));
	hits.clear();
	CHECK(!ScrollbarMouse(drag, hits, WM_MOUSEMOVE, MK_LBUTTON, {106, 120}, to));
	CHECK_EQ(drag.id, -1);
}

static void PageTheTrack()
{
	std::vector<Hit> hits = {Bar(4, {120, 300, 100})};
	const int thumbTop = hits[0].thumbTop;
	ScrollDrag drag;
	ScrollTo to;
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {104, thumbTop - 5}, to));
	CHECK_EQ(to.id, 4);
	CHECK_EQ(to.pos, 20);
	CHECK_EQ(drag.id, -1);
	CHECK(ScrollbarMouse(drag, hits, WM_LBUTTONDBLCLK, MK_LBUTTON, {104, 249}, to));
	CHECK_EQ(to.pos, 220);
	CHECK_EQ(drag.id, -1);
}

static void OnlyTheTopHit()
{
	// A dialog painted over the bar takes the press.
	std::vector<Hit> hits = {Bar(1, {0, 300, 100}), {{0, 0, 800, 600}, 5, 0}};
	ScrollDrag drag;
	ScrollTo to;
	CHECK(!ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {106, 60}, to));
	CHECK_EQ(drag.id, -1);
	CHECK(!ScrollbarMouse(drag, hits, WM_LBUTTONDOWN, MK_LBUTTON, {50, 60}, to));
	CHECK(!ScrollbarMouse(drag, hits, WM_RBUTTONDOWN, MK_RBUTTON, {106, 60}, to));
}

static void ListsShareTheBar()
{
	// The lists that keep their own geometry: 40 rows, 10 in view, a track 200 high at y 50.
	ListGeom g;
	g.area = {0, 40, 120, 260};
	g.track = {100, 50, 114, 250};
	g.total = 40;
	g.visible = 10;
	g.maxScroll = 30;
	const ThumbSpan t = ScrollThumb({6, 30, 10}, 200, 20);
	g.thumb = {100, 50 + t.top, 114, 50 + t.top + t.len};
	ListState st;
	st.scroll = 6;
	CHECK(ListDown(st, g, {104, 249}));
	CHECK(!st.drag);
	CHECK_EQ(st.scroll, 16);
	st.scroll = 6;
	CHECK(ListDown(st, g, {104, g.thumb.top + 3}));
	CHECK(st.drag);
	CHECK_EQ(st.grab, 3);
	CHECK(ListMove(st, g, {104, 250 + 3}));
	CHECK_EQ(st.scroll, 30);
	CHECK(ListMove(st, g, {104, g.thumb.top + 3}));
	CHECK_EQ(st.scroll, 6);
	CHECK(ListUp(st));
	CHECK(!ListMove(st, g, {104, 60}));
	CHECK(!ListDown(st, g, {20, 60}));
}

int main()
{
	ThumbSize();
	ThumbPosition();
	DragMapping();
	Paging();
	DragTheThumb();
	DragEnds();
	PageTheTrack();
	OnlyTheTopHit();
	ListsShareTheBar();
	return test::Summary("scrollbar");
}
