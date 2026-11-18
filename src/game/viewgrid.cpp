// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "addr/world.h"
#include "core/log.h"
#include "game/dirtygrid.h"
#include "game/viewgrid.h"
#include "game/gamehook.h"

// The engine marks what to redraw on a grid of 128 x 96 cells of 16 px (2048 x 1536) and copies the screen when the view
// scrolls. Past 2048 px the rest of the screen was never painted again, and past 1536 px the marks were written beyond the
// grid. A view up to 4096 x 3072 keeps the grid with cells of 32 px: only the two functions that turn pixels into cells change
// (RectToTileMask, which InvalidateRect and CollectDirtyEntries use, and GetDirtyRects, which SetViewRect and the map view's
// Render use), and each reads the cell's size from the manager's own view. A larger view is redrawn whole whenever something
// changes, as the engine's own _InvalidateAllToggle does.
namespace {
const int kGridWidth = dirtygrid::kCols << dirtygrid::kShift16, kGridHeight = dirtygrid::kRows << dirtygrid::kShift16;
const int kWholeWidth = dirtygrid::kCols << dirtygrid::kShift32, kWholeHeight = dirtygrid::kRows << dirtygrid::kShift32;
// CVXVisibleMgr: the grid, the "something changed" flag and the view (left, top, right, bottom, inclusive).
const size_t kMgrGrid = 0x10, kMgrDirty = 0x610, kMgrView = 0x618;
// The first calls with the engine's own cells run both ways and are compared: the 32 px cells run the same code.
const int kChecks = 4000;
const DWORD kStatsMs = 30000;

struct CodeSwitch {
	uintptr_t site;
	uint8_t grid[6], whole[6];
	size_t n;
};

const CodeSwitch kSwitches[] = {
	// InvalidateRect: raise the "something changed" flag without marking the grid's rows.
	{addr::kDirtyGridRowsJump, {0x77, 0x42}, {0xeb, 0x42}, 2},
	// SetViewRect: invalidate the whole view instead of copying the screen and invalidating the uncovered strips.
	{addr::kViewScrollSurfaceJump, {0x0f, 0x84, 0xc5, 0x00, 0x00, 0x00}, {0xe9, 0xc6, 0x00, 0x00, 0x00, 0x90}, 6},
	// CollectDirtyEntries: every object of the view, not only those on marked cells.
	{addr::kCollectDirtyFilterJump, {0x0f, 0x84, 0x86, 0x00, 0x00, 0x00}, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6},
};

typedef void(__fastcall *SetViewRect_t)(void *self, void *edx, int *rect, void *surface);
typedef int(__fastcall *TileMask_t)(void *self, void *edx, const int *rect, uint32_t *mask, int *rows);
typedef unsigned(__fastcall *DirtyRects_t)(void *self, void *edx, int *out, unsigned max);
SetViewRect_t g_origSetViewRect = nullptr;
TileMask_t g_origTileMask = nullptr;
DirtyRects_t g_origDirtyRects = nullptr;
bool g_whole = false;
// Both cell hooks are in: views up to 4096x3072 keep the grid.
bool g_cells = false;
int g_maskChecks = 0, g_rectChecks = 0;
bool g_mismatch = false;

struct Stats {
	DWORD since = 0;
	unsigned calls = 0, rects = 0;
	double share = 0;
};
Stats g_stats;

dirtygrid::Rect ViewOf(void *mgr)
{
	const int *v = reinterpret_cast<const int *>(static_cast<char *>(mgr) + kMgrView);
	dirtygrid::Rect r;
	r.l = v[0];
	r.t = v[1];
	r.r = v[2];
	r.b = v[3];
	return r;
}

uint32_t (*GridOf(void *mgr))[4]
{
	return reinterpret_cast<uint32_t(*)[4]>(static_cast<char *>(mgr) + kMgrGrid);
}

void Mismatch(const char *what, const char *detail)
{
	g_mismatch = true;
	LogE("render", "redraw grid: %s differs from the game's (%s); the 32 px cells may leave parts unpainted", what, detail);
}

int __fastcall HookTileMask(void *self, void *edx, const int *rect, uint32_t *mask, int *rows)
{
	const dirtygrid::Rect view = ViewOf(self);
	const int shift = dirtygrid::CellShift(view);
	if (shift == dirtygrid::kShift32 && rect) {
		dirtygrid::Rect r;
		r.l = rect[0];
		r.t = rect[1];
		r.r = rect[2];
		r.b = rect[3];
		return dirtygrid::TileMask(view, r, shift, mask, rows) ? 1 : 0;
	}
	const int result = g_origTileMask(self, edx, rect, mask, rows);
	if (shift != dirtygrid::kShift16 || !rect || g_mismatch || g_maskChecks >= kChecks) return result;
	g_maskChecks++;
	dirtygrid::Rect r;
	r.l = rect[0];
	r.t = rect[1];
	r.r = rect[2];
	r.b = rect[3];
	uint32_t ours[4] = {};
	int ourRows[2] = {};
	const bool hit = dirtygrid::TileMask(view, r, shift, ours, ourRows);
	if (hit != (result != 0) || (hit && (memcmp(ours, mask, sizeof(ours)) != 0 || ourRows[0] != rows[0] || ourRows[1] != rows[1]))) {
		char detail[160];
		sprintf_s(detail, "rect %d,%d,%d,%d in view %d,%d,%d,%d", r.l, r.t, r.r, r.b, view.l, view.t, view.r, view.b);
		Mismatch("the cells of a rectangle", detail);
	}
	else if (g_maskChecks == kChecks) {
		LogI("render", "redraw grid: %d rectangles turned into cells as the game does", kChecks);
	}
	return result;
}

void CountRects(const dirtygrid::Rect &view, const dirtygrid::Rect *out, unsigned n)
{
	const DWORD now = GetTickCount();
	if (!g_stats.since) g_stats.since = now;
	if (n) {
		const double area = static_cast<double>(view.r - view.l + 1) * (view.b - view.t + 1);
		double painted = 0;
		for (unsigned i = 0; i < n; i++) painted += static_cast<double>(out[i].r - out[i].l + 1) * (out[i].b - out[i].t + 1);
		g_stats.calls++;
		g_stats.rects += n;
		g_stats.share += painted / area;
	}
	if (now - g_stats.since < kStatsMs) return;
	if (g_stats.calls) {
		LogI("render", "redraw grid of 32 px cells: %u repaints in %u s, %.1f rectangles and %.0f %% of the view each", g_stats.calls,
			static_cast<unsigned>((now - g_stats.since) / 1000), static_cast<double>(g_stats.rects) / g_stats.calls, 100.0 * g_stats.share / g_stats.calls);
	}
	g_stats = Stats();
	g_stats.since = now;
}

unsigned __fastcall HookDirtyRects(void *self, void *edx, int *out, unsigned max)
{
	const dirtygrid::Rect view = ViewOf(self);
	const int shift = dirtygrid::CellShift(view);
	int &dirty = *reinterpret_cast<int *>(static_cast<char *>(self) + kMgrDirty);
	const bool redrawAll = *reinterpret_cast<int *>(addr::kRedrawWholeView) != 0;
	if (shift == dirtygrid::kShift32 && out) {
		dirtygrid::Rect *rects = reinterpret_cast<dirtygrid::Rect *>(out);
		const unsigned n = dirtygrid::TakeRects(GridOf(self), dirty, redrawAll, view, shift, rects, max);
		CountRects(view, rects, n);
		return n;
	}
	if (shift != dirtygrid::kShift16 || !out || g_mismatch || g_rectChecks >= kChecks) return g_origDirtyRects(self, edx, out, max);
	g_rectChecks++;
	uint32_t grid[dirtygrid::kRows][4];
	memcpy(grid, GridOf(self), sizeof(grid));
	int ourDirty = dirty;
	const unsigned n = g_origDirtyRects(self, edx, out, max);
	std::vector<dirtygrid::Rect> ours(max ? max : 1);
	const unsigned m = dirtygrid::TakeRects(grid, ourDirty, redrawAll, view, shift, ours.data(), max);
	const bool same = m == n && ourDirty == dirty && memcmp(grid, GridOf(self), sizeof(grid)) == 0 &&
		(n == 0 || memcmp(ours.data(), out, n * sizeof(dirtygrid::Rect)) == 0);
	if (!same) {
		char detail[160];
		sprintf_s(detail, "%u rectangles against %u, flag %d against %d, view %d,%d,%d,%d", m, n, ourDirty, dirty, view.l, view.t, view.r, view.b);
		Mismatch("the rectangles to repaint", detail);
	}
	else if (g_rectChecks == kChecks) {
		LogI("render", "redraw grid: %d sets of rectangles to repaint taken as the game does", kChecks);
	}
	return n;
}

bool WriteCode(uintptr_t site, const uint8_t *from, const uint8_t *to, size_t n)
{
	uint8_t *at = reinterpret_cast<uint8_t *>(site);
	if (memcmp(at, to, n) == 0) return true;
	if (memcmp(at, from, n) != 0) return false;
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, to, n);
	VirtualProtect(at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	return true;
}

void RedrawWhole(bool whole, int w, int h)
{
	for (const CodeSwitch &s : kSwitches) WriteCode(s.site, whole ? s.grid : s.whole, whole ? s.whole : s.grid, s.n);
	*reinterpret_cast<int *>(addr::kRedrawWholeView) = whole ? 1 : 0;
	g_whole = whole;
	LogI("render", "view of %dx%d: %s", w, h, whole ? "larger than 4096x3072, it is redrawn whole" : "within the redraw grid");
}

void __fastcall HookSetViewRect(void *self, void *edx, int *rect, void *surface)
{
	if (rect) {
		const int w = rect[2] - rect[0], h = rect[3] - rect[1];
		const bool whole = g_cells ? w >= kWholeWidth || h >= kWholeHeight : w >= kGridWidth || h >= kGridHeight;
		if (whole != g_whole) RedrawWhole(whole, w, h);
		static int loggedW = 0, loggedH = 0;
		if (!whole && (w != loggedW || h != loggedH)) {
			loggedW = w;
			loggedH = h;
			LogI("render", "view of %dx%d: redraw grid of %d px cells", w + 1, h + 1, w < kGridWidth && h < kGridHeight ? 16 : 32);
		}
	}
	g_origSetViewRect(self, edx, rect, surface);
}
}

void InstallViewGrid(bool knownExe)
{
	if (!knownExe) return;
	// ScrollBitmapRows loops while its row count is not 0: a negative one, from a view that jumps away, would never end.
	const uint8_t untilZero[2] = {0x74, 0x16}, whileAbove[2] = {0x7e, 0x16};
	if (!WriteCode(addr::kScrollRowsCountJump, untilZero, whileAbove, 2)) LogW("render", "scroll copy: the code is not the expected one; left as it is");
	bool expected = true;
	for (const CodeSwitch &s : kSwitches) expected = expected && memcmp(reinterpret_cast<void *>(s.site), s.grid, s.n) == 0;
	const bool ok = expected && HookFunction(addr::kVisibleMgrSetViewRect, &HookSetViewRect, &g_origSetViewRect);
	// Without both cell hooks, views past 2048x1536 keep the whole redraw.
	const bool cells = ok && HookFunction(addr::kVisibleMgrRectToTileMask, &HookTileMask, &g_origTileMask) &&
		HookFunction(addr::kVisibleMgrGetDirtyRects, &HookDirtyRects, &g_origDirtyRects);
	if (ok && !cells) LogE("render", "redraw grid: the 32 px cells could not be hooked; views past 2048x1536 are redrawn whole");
	g_cells = cells;
	LogAt(ok ? kLogInfo : kLogError, "render", "views larger than 2048x1536: %s",
		!ok ? (expected ? "NOT handled (hook failed)" : "NOT handled (the code is not the expected one)")
			: cells ? "redraw grid of 32 px cells up to 4096x3072, redrawn whole past it" : "redrawn whole");
}
