// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/dirtygrid.h"

namespace dirtygrid {

void SetRange(uint32_t row[4], int a, int b)
{
	for (int w = 0; w < 4; w++) {
		const int lo = a > w * 32 ? a - w * 32 : 0, hi = b < w * 32 + 31 ? b - w * 32 : 31;
		if (lo > hi || lo > 31 || hi < 0) {
			row[w] = 0;
			continue;
		}
		const uint32_t upTo = hi == 31 ? 0xffffffffu : (1u << (hi + 1)) - 1;
		row[w] = upTo & ~((1u << lo) - 1);
	}
}

bool NextRun(const uint32_t row[4], int &a, int &b)
{
	int w = 0;
	while (w < 4 && !row[w]) w++;
	if (w == 4) return false;
	int bit = 0;
	while (!(row[w] & (1u << bit))) bit++;
	a = w * 32 + bit;
	b = a;
	for (int next = a + 1; next < kCols && (row[next / 32] & (1u << (next % 32))); next++) b = next;
	return true;
}

int CellShift(const Rect &view)
{
	const int w = view.r - view.l + 1, h = view.b - view.t + 1;
	if (w <= (kCols << kShift16) && h <= (kRows << kShift16)) return kShift16;
	if (w <= (kCols << kShift32) && h <= (kRows << kShift32)) return kShift32;
	return 0;
}

bool TileMask(const Rect &view, const Rect &rect, int shift, uint32_t mask[4], int rows[2])
{
	if (rect.l > view.r || view.l > rect.r || rect.t > view.b || view.t > rect.b || rect.l > rect.r || rect.t > rect.b) return false;
	const int first = view.l < rect.l ? (rect.l - view.l) >> shift : 0;
	// Past the view's right edge the engine marks up to its last column.
	const int last = rect.r <= view.r ? (rect.r - view.l) >> shift : kCols - 1;
	SetRange(mask, first, last);
	rows[0] = rect.t < view.t ? 0 : (rect.t - view.t) >> shift;
	rows[1] = ((rect.b < view.b ? rect.b : view.b) - view.t) >> shift;
	return true;
}

unsigned TakeRects(uint32_t grid[kRows][4], int &dirty, bool redrawAll, const Rect &view, int shift, Rect *out, unsigned max)
{
	if (!dirty) return 0;
	if (redrawAll) {
		out[0] = view;
		dirty = 0;
		return 1;
	}
	if (!max) return 0;
	const int cell = 1 << shift;
	unsigned count = 0;
	int row = 0;
	for (;;) {
		int a = 0, b = 0;
		while (row < kRows && !NextRun(grid[row], a, b)) row++;
		if (row == kRows) {
			if (count < max) dirty = 0;
			return count;
		}
		// The run is cleared with a cell of margin on each side, and the rows below join it while they have exactly the same run
		// inside that margin.
		uint32_t wide[4], run[4];
		SetRange(wide, a == 0 ? 0 : a - 1, b > kCols - 2 ? b : b + 1);
		SetRange(run, a, b);
		const int start = row;
		for (int w = 0; w < 4; w++) grid[row][w] &= ~wide[w];
		row++;
		for (; row < kRows; row++) {
			bool same = true;
			for (int w = 0; w < 4; w++) same = same && (grid[row][w] & wide[w]) == run[w];
			if (!same) break;
			for (int w = 0; w < 4; w++) grid[row][w] &= ~wide[w];
		}
		Rect r;
		r.l = a * cell + view.l;
		r.t = start * cell + view.t;
		r.r = b * cell + cell - 1 + view.l;
		r.b = row * cell - 1 + view.t;
		if (r.l < view.l) r.l = view.l;
		if (r.t < view.t) r.t = view.t;
		if (r.r > view.r) r.r = view.r;
		if (r.b > view.b) r.b = view.b;
		if (r.l <= r.r && r.t <= r.b) out[count++] = r;
		row = start;
		if (count >= max) return count;
	}
}

}
