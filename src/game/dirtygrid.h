// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The engine's redraw grid (CVXVisibleMgr+0x10): 96 rows of 128 bits, a bit for each cell of the view. The engine's cells are 16 px,
// which covers 2048x1536; with 32 px cells the same grid covers 4096x3072, so a 1440p or 4K view repaints only what changed instead
// of all of it on every frame. These are RectToTileMask (00625840) and GetDirtyRects (00625a00) with the cell's size as a value.
namespace dirtygrid {

constexpr int kCols = 128, kRows = 96;
constexpr int kShift16 = 4, kShift32 = 5;

// Inclusive, in the view's pixels, as the engine keeps them.
struct Rect {
	int l = 0, t = 0, r = 0, b = 0;
};

// CBitRow128::SetRange (00625430): the bits from a to b, both included, and no other.
void SetRange(uint32_t row[4], int a, int b);
// CBitRow128::GetNextRun (00623f90): the first set bit and the last of the run it starts.
bool NextRun(const uint32_t row[4], int &a, int &b);
// The cell's size for a view, as a shift: kShift16 up to 2048x1536, kShift32 up to 4096x3072, 0 for a larger one.
int CellShift(const Rect &view);
// The columns a rectangle touches, as a mask, and its first and last row; false when it misses the view.
bool TileMask(const Rect &view, const Rect &rect, int shift, uint32_t mask[4], int rows[2]);
// Takes up to `max` rectangles out of the grid, each a run of cells of a row and the rows below it with the same run, and clears
// them. `dirty` is the engine's "something changed" flag: 0 gives nothing, and it goes back to 0 once the grid is empty. With
// `redrawAll` (DAT_00a782e0) the answer is the whole view.
unsigned TakeRects(uint32_t grid[kRows][4], int &dirty, bool redrawAll, const Rect &view, int shift, Rect *out, unsigned max);

}
