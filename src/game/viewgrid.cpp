// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "addr/world.h"
#include "core/log.h"
#include "game/viewgrid.h"

// The engine marks what to redraw on a grid of 128 x 96 cells of 16 px (2048 x 1536) and copies the screen when the view
// scrolls. Past 2048 px the rest of the screen is never painted again, and past 1536 px the marks are written beyond the
// grid. A larger view is redrawn whole whenever something changes, as the engine's own _InvalidateAllToggle does.
namespace {
const int kGridWidth = 2048, kGridHeight = 1536;

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
SetViewRect_t g_origSetViewRect = nullptr;
bool g_whole = false;

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
	LogI("render", "view of %dx%d: %s", w, h, whole ? "larger than the engine's redraw grid (2048x1536), it is redrawn whole" : "within the engine's redraw grid");
}

void __fastcall HookSetViewRect(void *self, void *edx, int *rect, void *surface)
{
	if (rect) {
		const int w = rect[2] - rect[0], h = rect[3] - rect[1];
		const bool whole = w >= kGridWidth || h >= kGridHeight;
		if (whole != g_whole) RedrawWhole(whole, w, h);
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
	const bool ok = expected &&
		HookCreate(reinterpret_cast<void *>(addr::kVisibleMgrSetViewRect), reinterpret_cast<void *>(&HookSetViewRect), reinterpret_cast<void **>(&g_origSetViewRect)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kVisibleMgrSetViewRect)) == MH_OK;
	LogAt(ok ? kLogInfo : kLogError, "render", "views larger than 2048x1536: %s", ok ? "redrawn whole" : expected ? "NOT handled (hook failed)" : "NOT handled (the code is not the expected one)");
}
