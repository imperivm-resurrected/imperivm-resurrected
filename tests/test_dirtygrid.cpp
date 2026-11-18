// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstring>
#include <vector>

#include "game/dirtygrid.h"

using namespace dirtygrid;

namespace {

struct Grid {
	uint32_t rows[kRows][4] = {};
	int dirty = 0;
	bool Empty() const
	{
		for (const auto &r : rows) {
			if (r[0] || r[1] || r[2] || r[3]) return false;
		}
		return true;
	}
};

// What CVXVisibleMgr::InvalidateRect does with the mask.
void Invalidate(Grid &g, const Rect &view, int shift, const Rect &rect)
{
	uint32_t mask[4];
	int rows[2];
	if (!TileMask(view, rect, shift, mask, rows)) return;
	g.dirty = 1;
	for (int r = rows[0]; r <= rows[1]; r++) {
		for (int w = 0; w < 4; w++) g.rows[r][w] |= mask[w];
	}
}

bool Inside(const Rect &r, int x, int y)
{
	return x >= r.l && x <= r.r && y >= r.t && y <= r.b;
}

Rect View(int l, int t, int w, int h)
{
	Rect v;
	v.l = l;
	v.t = t;
	v.r = l + w - 1;
	v.b = t + h - 1;
	return v;
}

Rect R(int l, int t, int r, int b)
{
	Rect x;
	x.l = l;
	x.t = t;
	x.r = r;
	x.b = b;
	return x;
}

}

static void Bits()
{
	uint32_t row[4];
	SetRange(row, 0, 0);
	CHECK(row[0] == 1 && !row[1] && !row[2] && !row[3]);
	SetRange(row, 30, 33);
	CHECK(row[0] == 0xc0000000u && row[1] == 3 && !row[2]);
	SetRange(row, 0, 127);
	CHECK(row[0] == 0xffffffffu && row[3] == 0xffffffffu);
	SetRange(row, 96, 127);
	CHECK(!row[0] && !row[2] && row[3] == 0xffffffffu);
	int a = 0, b = 0;
	SetRange(row, 30, 70);
	CHECK(NextRun(row, a, b));
	CHECK_EQ(a, 30);
	CHECK_EQ(b, 70);
	const uint32_t none[4] = {};
	CHECK(!NextRun(none, a, b));
	const uint32_t last[4] = {0, 0, 0, 0x80000000u};
	CHECK(NextRun(last, a, b));
	CHECK_EQ(a, 127);
	CHECK_EQ(b, 127);
}

static void Shifts()
{
	CHECK_EQ(CellShift(View(0, 0, 1920, 1080)), kShift16);
	CHECK_EQ(CellShift(View(0, 0, 2048, 1536)), kShift16);
	CHECK_EQ(CellShift(View(0, 0, 2560, 1440)), kShift32);
	CHECK_EQ(CellShift(View(0, 0, 3840, 2160)), kShift32);
	CHECK_EQ(CellShift(View(0, 0, 4096, 3072)), kShift32);
	CHECK_EQ(CellShift(View(0, 0, 5120, 2880)), 0);
}

static void Edges()
{
	const Rect view = View(1000, 500, 2560, 1440);
	uint32_t mask[4];
	int rows[2];
	CHECK(TileMask(view, R(view.r, view.b, view.r, view.b), kShift32, mask, rows));
	int a = 0, b = 0;
	CHECK(NextRun(mask, a, b));
	CHECK_EQ(a, 79);
	CHECK_EQ(b, 79);
	CHECK_EQ(rows[0], 44);
	CHECK_EQ(rows[1], 44);
	CHECK(!TileMask(view, R(0, 0, 999, 499), kShift32, mask, rows));
	// Past the right edge the engine marks up to the last column.
	CHECK(TileMask(view, R(view.r - 10, view.t, view.r + 50, view.t), kShift32, mask, rows));
	CHECK(NextRun(mask, a, b));
	CHECK_EQ(b, kCols - 1);
}

// Every marked pixel ends in a returned rectangle inside the view, and the grid is left empty.
static void Covers(const Rect &view, int shift)
{
	Grid g;
	std::vector<Rect> marked;
	unsigned seed = 12345;
	auto next = [&seed](int n) {
		seed = seed * 1103515245u + 12345u;
		return static_cast<int>((seed >> 8) % static_cast<unsigned>(n));
	};
	const int w = view.r - view.l + 1, h = view.b - view.t + 1;
	for (int i = 0; i < 60; i++) {
		const int x = view.l - 40 + next(w + 80), y = view.t - 40 + next(h + 80);
		const Rect r = R(x, y, x + next(300), y + next(200));
		marked.push_back(r);
		Invalidate(g, view, shift, r);
	}
	std::vector<Rect> out(512);
	const unsigned n = TakeRects(g.rows, g.dirty, false, view, shift, out.data(), 512);
	CHECK(n > 0 && n < 512);
	CHECK(g.Empty());
	CHECK_EQ(g.dirty, 0);
	for (unsigned i = 0; i < n; i++) CHECK(out[i].l >= view.l && out[i].r <= view.r && out[i].t >= view.t && out[i].b <= view.b);
	int missed = 0;
	for (const Rect &m : marked) {
		const int l = m.l < view.l ? view.l : m.l, t = m.t < view.t ? view.t : m.t;
		const int r = m.r > view.r ? view.r : m.r, b = m.b > view.b ? view.b : m.b;
		if (l > r || t > b) continue;
		const int xs[3] = {l, (l + r) / 2, r}, ys[3] = {t, (t + b) / 2, b};
		for (int x : xs) {
			for (int y : ys) {
				bool hit = false;
				for (unsigned i = 0; i < n && !hit; i++) hit = Inside(out[i], x, y);
				if (!hit) missed++;
			}
		}
	}
	CHECK_EQ(missed, 0);
}

static void Whole()
{
	const Rect view = View(64, 32, 2560, 1440);
	Grid g;
	Invalidate(g, view, kShift32, view);
	Rect out[4];
	CHECK_EQ(TakeRects(g.rows, g.dirty, false, view, kShift32, out, 4), 1u);
	CHECK(out[0].l == view.l && out[0].t == view.t && out[0].r == view.r && out[0].b == view.b);
	// The engine's "redraw everything": the view itself, and the flag goes down.
	g.dirty = 1;
	CHECK_EQ(TakeRects(g.rows, g.dirty, true, view, kShift32, out, 4), 1u);
	CHECK_EQ(g.dirty, 0);
	// Nothing changed: nothing to paint.
	CHECK_EQ(TakeRects(g.rows, g.dirty, false, view, kShift32, out, 4), 0u);
}

static void Limit()
{
	const Rect view = View(0, 0, 1920, 1080);
	Grid g;
	for (int i = 0; i < 10; i++) Invalidate(g, view, kShift16, R(i * 100, i * 50, i * 100 + 10, i * 50 + 10));
	Rect out[3];
	CHECK_EQ(TakeRects(g.rows, g.dirty, false, view, kShift16, out, 3), 3u);
	// Full before the grid ran out: the flag stays up and the rest waits for the next call.
	CHECK_EQ(g.dirty, 1);
	CHECK(!g.Empty());
	CHECK(out[0].l == 0 && out[0].t == 0 && out[0].r == 15 && out[0].b == 15);
}

int main()
{
	Bits();
	Shifts();
	Edges();
	Covers(View(0, 0, 1920, 1080), kShift16);
	Covers(View(320, 96, 2048, 1536), kShift16);
	Covers(View(1000, 500, 2560, 1440), kShift32);
	Covers(View(0, 0, 3840, 2160), kShift32);
	Whole();
	Limit();
	return test::Summary("dirtygrid");
}
