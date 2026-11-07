// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "game/controls.h"
#include "game/mapfog.h"
#include "game/quickicons.h"
#include "game/zoomview.h"
#include "hooks/displaymode.h"

namespace {
typedef void(__fastcall *ZoomDraw_t)(void *self, void *edx, void *surface, int unused);
typedef void(__fastcall *FillRect_t)(void *surface, void *edx, int *rect, unsigned color);
typedef void(__fastcall *Manager_t)(void *manager);
typedef void(__fastcall *ManagerRect_t)(void *manager, void *edx, int *rect);
typedef void(__fastcall *ManagerApply_t)(void *manager, void *edx, int a, int b);
typedef void(__fastcall *ManagerClear_t)(void *manager, void *edx, int a);
typedef unsigned(__fastcall *Height_t)(void *map, void *edx, unsigned *position);

ZoomDraw_t g_origDraw = nullptr;
bool g_failed = false;

struct Geometry {
	bool valid = false;
	DWORD tick = 0;
	int left = 0, top = 0, right = 0, bottom = 0;
	int winX = 0, winY = 0;
};
Geometry g_geo;

struct Drag {
	bool pending = false, dragging = false;
	int startX = 0, startY = 0, curX = 0, curY = 0;
	WPARAM downWp = 0;
	LPARAM downLp = 0;
};
Drag g_drag;
bool g_swallowLeftUp = false, g_swallowRightUp = false;

const DWORD kOpenWindowMs = 250;
const int kDragThreshold = 5;
const int kMessageLeftDown = 0x111, kMessageLeftDouble = 0x113, kMessageRightDown = 0x121;

template <typename T>
T &At(void *p, int off) { return *reinterpret_cast<T *>(static_cast<char *>(p) + off); }

bool ExtrasEnabled() { return GetConfig().zoomExtras; }

bool ZoomOpen() { return g_geo.valid && GetTickCount() - g_geo.tick < kOpenWindowMs; }

typedef void(__fastcall *ZoomHide_t)(void *self);
ZoomHide_t g_origHide = nullptr;

typedef int(__fastcall *IsKindOf_t)(void *self, void *edx, const char *name);
typedef int(__fastcall *HasFlags_t)(void *window, void *edx, unsigned mask);

bool WindowIsOfKind(char *w, uintptr_t nameAddr)
{
	__try {
		char *obj = w + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(w + 4) + 4);
		const char *cls = *reinterpret_cast<const char *const *>(nameAddr);
		return reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(obj))[1])(obj, nullptr, cls) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SameWindow(void *a, void *b)
{
	if (a == b) return true;
	__try {
		char *x = static_cast<char *>(a), *y = static_cast<char *>(b);
		return x == y + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(y + 4) + 4) ||
			y == x + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(x + 4) + 4);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool OtherUiOpen()
{
	bool menu = false, modalOther = false;
	void *modal = nullptr;
	void *notice = *reinterpret_cast<void **>(addr::kPausedMenu);
	__try {
		char *root = *reinterpret_cast<char **>(addr::kUIRoot);
		if (root) {
			modal = *reinterpret_cast<void **>(root + 0x370);
			int guard = 0;
			for (char *w = *reinterpret_cast<char **>(root + 0x20); w && !menu && guard < 64; w = *reinterpret_cast<char **>(w + 0x18), guard++) {
				if (!(WindowIsOfKind(w, addr::kClassNameCVXUIGameMenu) || WindowIsOfKind(w, addr::kClassNameCVXUIOptions) ||
						WindowIsOfKind(w, addr::kClassNameCVXUIConfirm))) {
					continue;
				}
				menu = reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(w, nullptr, 2) != 0;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		menu = false;
	}
	modalOther = modal && !(notice && SameWindow(modal, notice));
	static int last = -1, logged = 0;
	const int now = menu ? 1 : modalOther ? 2 : 0;
	if (now != last && logged++ < 24) {
		LogD("game", "zoom map extras: %s (menu window %d, other modal window %d, pause notice %d)", now ? "a menu is open, not handled" : "no menu, handled", menu ? 1 : 0,
			modalOther ? 1 : 0, notice ? 1 : 0);
	}
	last = now;
	return menu || modalOther;
}

void ResetDrag() { g_drag = Drag(); }

void FillEdge(void *surface, int l, int t, int r, int b, unsigned color)
{
	void **vtable = *reinterpret_cast<void ***>(surface);
	int rect[4] = {l - At<int>(surface, 0x24), t - At<int>(surface, 0x28), r - At<int>(surface, 0x24), b - At<int>(surface, 0x28)};
	reinterpret_cast<FillRect_t>(vtable[0x3c / 4])(surface, nullptr, rect, color);
}

void DrawDragRectangle(void *surface)
{
	if (!g_drag.dragging) return;
	const int l = std::min(g_drag.startX, g_drag.curX), r = std::max(g_drag.startX, g_drag.curX);
	const int t = std::min(g_drag.startY, g_drag.curY), b = std::max(g_drag.startY, g_drag.curY);
	const unsigned kEdge = 0x7fff, kShade = 0x2108;
	FillEdge(surface, l - 1, t - 1, r + 1, t - 1, kShade);
	FillEdge(surface, l - 1, b + 1, r + 1, b + 1, kShade);
	FillEdge(surface, l - 1, t - 1, l - 1, b + 1, kShade);
	FillEdge(surface, r + 1, t - 1, r + 1, b + 1, kShade);
	FillEdge(surface, l, t, r, t, kEdge);
	FillEdge(surface, l, b, r, b, kEdge);
	FillEdge(surface, l, t, l, b, kEdge);
	FillEdge(surface, r, t, r, b, kEdge);
}

void CaptureGeometry(void *self, void *surface)
{
	const bool wasClosed = !ZoomOpen();
	char *base = static_cast<char *>(self);
	Geometry g;
	g.left = *reinterpret_cast<int *>(base - 0x98);
	g.top = *reinterpret_cast<int *>(base - 0x94);
	g.right = *reinterpret_cast<int *>(base - 0x90);
	g.bottom = *reinterpret_cast<int *>(base - 0x8c);
	const int *virtualBase = *reinterpret_cast<int **>(base - 0x9c);
	g.winX = *reinterpret_cast<short *>(base + virtualBase[2] - 0x8c);
	g.winY = *reinterpret_cast<short *>(base + virtualBase[2] - 0x8a);
	g.valid = true;
	g.tick = GetTickCount();
	g_geo = g;
	static int logged = 0;
	if (wasClosed && logged++ < 6) {
		LogD("game", "zoom map open: bitmap %d,%d to %d,%d, window %d,%d, surface offset %d,%d clip %d,%d to %d,%d", g.left, g.top, g.right, g.bottom, g.winX, g.winY,
			At<int>(surface, 0x24), At<int>(surface, 0x28), At<int>(surface, 4), At<int>(surface, 8), At<int>(surface, 0xc), At<int>(surface, 0x10));
	}
}

void PreviewBox(int x0, int y0, int x1, int y1, bool force);

void __fastcall HookZoomDraw(void *self, void *edx, void *surface, int unused)
{
	g_origDraw(self, edx, surface, unused);
	if (g_failed || !ExtrasEnabled()) return;
	__try {
		CaptureGeometry(self, surface);
		QuickIconsDraw(surface, g_geo.winX, g_geo.winY);
		if (g_drag.dragging) PreviewBox(g_drag.startX, g_drag.startY, g_drag.curX, g_drag.curY, false);
		DrawDragRectangle(surface);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_failed = true;
		LogE("game", "zoom map: ERROR drawing the extras, disabled");
	}
}

bool ToWorld(int x, int y, int *world)
{
	int width = 0, height = 0;
	if (!ZoomBitmapSize(&width, &height)) return false;
	const int spanX = std::max(1, g_geo.right - g_geo.left), spanY = std::max(1, g_geo.bottom - g_geo.top);
	const int px = std::max(0, std::min(width - 1, (x - g_geo.left) * (width - 1) / spanX));
	const int py = std::max(0, std::min(height - 1, (y - g_geo.top) * (height - 1) / spanY));
	return ZoomPixelToWorld(px, py, world);
}

void *Manager() { return *reinterpret_cast<void **>(addr::kSelectionManager); }

bool ProjectedRect(int x0, int y0, int x1, int y1, int *rect)
{
	int a[2] = {}, b[2] = {};
	if (!ToWorld(x0, y0, a) || !ToWorld(x1, y1, b)) return false;
	void *map = *reinterpret_cast<void **>(addr::kMap);
	if (!map) return false;
	int px[2], py[2];
	const int *points[2] = {a, b};
	for (int i = 0; i < 2; i++) {
		unsigned position[2] = {static_cast<unsigned>(points[i][0]), static_cast<unsigned>(points[i][1])};
		const unsigned height = reinterpret_cast<Height_t>(addr::kGetHeightAt)(map, nullptr, position);
		px[i] = points[i][0];
		py[i] = ((points[i][1] * 0xb5) >> 8) - static_cast<int>(height);
	}
	rect[0] = std::min(px[0], px[1]);
	rect[1] = std::min(py[0], py[1]);
	rect[2] = std::max(px[0], px[1]);
	rect[3] = std::max(py[0], py[1]);
	return true;
}

bool g_boxOpen = false;
DWORD g_lastPreview = 0;

void BeginBox(bool add)
{
	void *manager = Manager();
	if (!manager) return;
	if (!add) reinterpret_cast<ManagerClear_t>(addr::kClearSelection)(manager, nullptr, 1);
	reinterpret_cast<Manager_t>(addr::kBeginBoxSelect)(manager);
	g_boxOpen = true;
	g_lastPreview = 0;
}

void PreviewBox(int x0, int y0, int x1, int y1, bool force)
{
	void *manager = Manager();
	const DWORD now = GetTickCount();
	if (!manager || !g_boxOpen || (!force && now - g_lastPreview < 30)) return;
	g_lastPreview = now;
	int rect[4];
	if (!ProjectedRect(x0, y0, x1, y1, rect)) return;
	reinterpret_cast<ManagerRect_t>(addr::kBoxSelect)(manager, nullptr, rect);
	reinterpret_cast<ManagerApply_t>(addr::kApplySelection)(manager, nullptr, 1, 0);
}

void EndBox()
{
	void *manager = Manager();
	if (!manager || !g_boxOpen) return;
	g_boxOpen = false;
	reinterpret_cast<Manager_t>(addr::kEndBoxSelect)(manager);
	reinterpret_cast<Manager_t>(addr::kCommitPendingSelection)(manager);
	reinterpret_cast<ManagerApply_t>(addr::kApplySelection)(manager, nullptr, 1, 0);
}

void __fastcall HookZoomHide(void *self)
{
	if (!g_failed) {
		g_geo.valid = false;
		__try {
			if (g_boxOpen) EndBox();
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			g_boxOpen = false;
		}
		ResetDrag();
		g_swallowLeftUp = g_swallowRightUp = false;
	}
	g_origHide(self);
}

bool InsideBitmap(int x, int y) { return x >= g_geo.left && x <= g_geo.right && y >= g_geo.top && y <= g_geo.bottom; }
}

bool ZoomViewHandleMouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_failed) return false;
	if (!ZoomOpen() || !ExtrasEnabled()) {
		if (g_boxOpen) EndBox();
		if (g_drag.pending || g_drag.dragging) ResetDrag();
		g_swallowLeftUp = g_swallowRightUp = false;
		return false;
	}
	if (OtherUiOpen()) {
		if (g_boxOpen) EndBox();
		ResetDrag();
		g_swallowLeftUp = g_swallowRightUp = false;
		return false;
	}
	const int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
	const int viewX = x - g_geo.winX, viewY = y - g_geo.winY;
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		ResetDrag();
		if (QuickIconsHit(viewX, viewY)) {
			QuickIconsMouse(msg == WM_LBUTTONDOWN ? kMessageLeftDown : kMessageLeftDouble, viewX, viewY);
			g_swallowLeftUp = true;
			return true;
		}
		if (msg == WM_LBUTTONDOWN && ControlsZoomBoxVk() && (GetKeyState(static_cast<int>(ControlsZoomBoxVk())) & 0x8000) && InsideBitmap(x, y)) {
			g_drag.pending = true;
			g_drag.startX = g_drag.curX = x;
			g_drag.startY = g_drag.curY = y;
			g_drag.downWp = wp;
			g_drag.downLp = lp;
			return true;
		}
		return false;
	case WM_RBUTTONDOWN:
		if (QuickIconsHit(viewX, viewY)) {
			QuickIconsMouse(kMessageRightDown, viewX, viewY);
			g_swallowRightUp = true;
			return true;
		}
		return false;
	case WM_MOUSEMOVE:
		if (g_drag.pending) {
			g_drag.curX = x;
			g_drag.curY = y;
			if (!g_drag.dragging && (std::abs(x - g_drag.startX) >= kDragThreshold || std::abs(y - g_drag.startY) >= kDragThreshold)) {
				g_drag.dragging = true;
				BeginBox((GetKeyState(static_cast<int>(ControlsZoomBoxAddVk())) & 0x8000) != 0);
			}
			if (g_drag.dragging) PreviewBox(g_drag.startX, g_drag.startY, x, y, false);
		}
		return false;
	case WM_LBUTTONUP:
		if (g_swallowLeftUp) {
			g_swallowLeftUp = false;
			return true;
		}
		if (g_drag.pending) {
			const Drag drag = g_drag;
			ResetDrag();
			if (drag.dragging) {
				PreviewBox(drag.startX, drag.startY, x, y, true);
				EndBox();
				return true;
			}
			DisplayForwardMessage(hwnd, WM_LBUTTONDOWN, drag.downWp, drag.downLp);
		}
		return false;
	case WM_RBUTTONUP:
		if (g_swallowRightUp) {
			g_swallowRightUp = false;
			return true;
		}
		return false;
	default:
		return false;
	}
}

void ZoomViewGameTick()
{
	if (g_failed || ZoomOpen()) return;
	if (!g_boxOpen && !g_drag.pending && !g_drag.dragging && !g_swallowLeftUp && !g_swallowRightUp) return;
	if (g_boxOpen) EndBox();
	ResetDrag();
	g_swallowLeftUp = g_swallowRightUp = false;
}

void InstallZoomView(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookCreate(reinterpret_cast<void *>(addr::kZoomMapOnDraw), reinterpret_cast<void *>(&HookZoomDraw), reinterpret_cast<void **>(&g_origDraw)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kZoomMapOnDraw)) == MH_OK;
	const bool hideOk = HookCreate(reinterpret_cast<void *>(addr::kZoomMapHide), reinterpret_cast<void *>(&HookZoomHide), reinterpret_cast<void **>(&g_origHide)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kZoomMapHide)) == MH_OK;
	if (!hideOk) LogW("game", "zoom map: could not hook Hide; a box selection in progress is ended when the map stops being drawn");
	if (!ok) g_failed = true;
	LogAt(ok ? kLogInfo : kLogError, "game", "zoom map extras (group icons and box selection): %s", ok ? (ExtrasEnabled() ? "ready" : "ready, turned off in the options") : "NOT available");
}
