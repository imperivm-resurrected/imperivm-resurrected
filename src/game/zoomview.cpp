// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "game/controls.h"
#include "game/diplomacyrules.h"
#include "game/gamelayout.h"
#include "game/mapfog.h"
#include "game/quickicons.h"
#include "game/respawnsites.h"
#include "game/zoomview.h"
#include "game/gamehook.h"
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
typedef void(__fastcall *DrawLinks_t)(void *self, void *edx, void *surface);
typedef int *(__fastcall *ObjectPos_t)(void *object, void *edx, int *out);
DrawLinks_t g_origDrawLinks = nullptr;
// The routes are the mod's once they could be read; an error gives the game's dotted lines back.
bool g_routesFailed = false;

struct Geometry {
	bool valid = false;
	DWORD tick = 0;
	int left = 0, top = 0, right = 0, bottom = 0;
	int winX = 0, winY = 0;
};
Geometry g_geo;
ZoomClickFn g_clickHook = nullptr;

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

std::vector<ZoomRoute> g_routes;

// A map object's position in the world: its slot 0x3c, as the game's own route drawing asks it, called only when its table and the
// function are the game's.
bool ObjectPosition(char *object, int &x, int &y)
{
	const uintptr_t vtable = *reinterpret_cast<uintptr_t *>(object);
	if (vtable < addr::kGameRdataBegin || vtable >= addr::kGameRdataEnd) return false;
	const uintptr_t fn = reinterpret_cast<uintptr_t *>(vtable)[0x3c / 4];
	if (fn < addr::kGameTextBegin || fn >= addr::kGameTextEnd) return false;
	int out[4] = {};
	const int *p = reinterpret_cast<ObjectPos_t>(fn)(object, nullptr, out);
	if (!p) return false;
	x = p[0];
	y = p[1];
	return true;
}

// A world point on the picture of the big map, as the respawn timers are placed.
bool WorldToPicture(void *map, int wx, int wy, int w, int h, float &x, float &y)
{
	unsigned position[] = {static_cast<unsigned>(wx), static_cast<unsigned>(wy)};
	const unsigned height = reinterpret_cast<Height_t>(addr::kGetHeightAt)(map, nullptr, position);
	const int projectedY = ((wy * 0xb5) >> 8) - static_cast<int>(height);
	int pixel[2] = {};
	if (!ZoomProjectedToPixel(wx, projectedY, pixel)) return false;
	int px = 0, py = 0;
	if (!RespawnProjectAxis(pixel[0], 0, w - 1, g_geo.left, g_geo.right, px) || !RespawnProjectAxis(pixel[1], 0, h - 1, g_geo.top, g_geo.bottom, py)) return false;
	x = static_cast<float>(px);
	y = static_cast<float>(py);
	return true;
}

// The settlements that supply another, as CVXZoomMap::DrawLinkArrows (006124f0) finds them: the sender's owner shares its control with
// this player, the sender's supply (+0x5c) names an entry (+0x38) whose settlement (+0xc) is another one, still standing (+0x8c, its
// building), whose owner the sender has a ceasefire with.
void CollectRoutes()
{
	g_routes.clear();
	if (g_routesFailed || OtherUiOpen()) return;
	char *map = *reinterpret_cast<char **>(addr::kMap);
	char *game = *reinterpret_cast<char **>(addr::kGame);
	int w = 0, h = 0;
	if (!map || !game || !ZoomBitmapSize(&w, &h)) return;
	char *local = layout::LocalPlayer(game);
	if (!local) return;
	const int viewer = Field<int>(local, layout::kPlayerId);
	if (viewer < 0 || viewer >= layout::kPlayerCount) return;
	char **table = reinterpret_cast<char **>(addr::kHandleTable);
	auto handle = [table](char *object, size_t offset) { return table[*reinterpret_cast<unsigned short *>(object + offset)]; };
	char **it = *reinterpret_cast<char ***>(map + 0x4c), **end = *reinterpret_cast<char ***>(map + 0x50);
	for (int n = 0; it && it != end && n < 1024; ++it, ++n) {
		char *from = *it;
		if (!from) continue;
		char *owner = *reinterpret_cast<char **>(from + 0x90);
		if (!owner || !(Field<unsigned>(owner, layout::kPlayerDiplomacy + viewer * 4) & diplomacy::kControl)) continue;
		char *building = handle(from, 0x8c), *supply = handle(from, 0x5c);
		if (!building || !supply) continue;
		char *entry = handle(supply, 0x38);
		char *to = entry ? handle(entry, 0xc) : nullptr;
		if (!to || to == from) continue;
		char *toBuilding = handle(to, 0x8c), *toOwner = *reinterpret_cast<char **>(to + 0x90);
		if (!toBuilding || !toOwner) continue;
		const int toId = Field<int>(toOwner, layout::kPlayerId);
		if (toId < 0 || toId >= layout::kPlayerCount || !(Field<unsigned>(owner, layout::kPlayerDiplomacy + toId * 4) & diplomacy::kCeasefire)) continue;
		int ax = 0, ay = 0, bx = 0, by = 0;
		if (!ObjectPosition(building, ax, ay) || !ObjectPosition(toBuilding, bx, by)) continue;
		ZoomRoute r;
		if (!WorldToPicture(map, ax, ay, w, h, r.ax, r.ay) || !WorldToPicture(map, bx, by, w, h, r.bx, r.by)) continue;
		r.rgb = Rgb555ToRgb(Field<unsigned>(owner, layout::kPlayerColor) & 0x7fff);
		g_routes.push_back(r);
	}
}

// The game's dotted line gives way to the mod's arc while it can read the routes.
void __fastcall HookDrawLinks(void *self, void *edx, void *surface)
{
	if (g_failed || g_routesFailed) g_origDrawLinks(self, edx, surface);
}

// The times of the respawn timers over their sites on the last picture of the big map, in its screen coordinates; the overlay writes
// them, and nothing else marks the site.
std::vector<ZoomMapTimer> g_timers;

void DrawRespawnTimers()
{
	g_timers.clear();
	const std::vector<RespawnMark> &marks = RespawnMarks();
	if (marks.empty() || OtherUiOpen()) return;
	void *map = *reinterpret_cast<void **>(addr::kMap);
	int w = 0, h = 0;
	if (!map || !ZoomBitmapSize(&w, &h)) return;
	const int radius = std::clamp((g_geo.bottom - g_geo.top) / 64, 11, 22);
	const unsigned clock = RespawnClock();
	for (const RespawnMark &mark : marks) {
		unsigned position[] = {static_cast<unsigned>(mark.x), static_cast<unsigned>(mark.y)};
		const unsigned height = reinterpret_cast<Height_t>(addr::kGetHeightAt)(map, nullptr, position);
		// The isometric projection the big map's bitmap is drawn with: y squashed to 0xb5/256, minus the terrain's height.
		const int projectedY = ((mark.y * 0xb5) >> 8) - static_cast<int>(height);
		int pixel[2] = {};
		if (!ZoomProjectedToPixel(mark.x, projectedY, pixel)) continue;
		int x = 0, y = 0;
		if (!RespawnProjectAxis(pixel[0], 0, w - 1, g_geo.left, g_geo.right, x) || !RespawnProjectAxis(pixel[1], 0, h - 1, g_geo.top, g_geo.bottom, y)) continue;
		const unsigned left = mark.stocked ? 0 : RespawnSecondsUntil(mark.due, clock);
		if (!left) continue;
		ZoomMapTimer t;
		t.x = x;
		t.y = y - radius - 4 >= g_geo.top + 30 ? y - radius - 4 : y + radius + 32;
		t.seconds = left;
		t.approximate = mark.approximate;
		g_timers.push_back(t);
	}
}

void __fastcall HookZoomDraw(void *self, void *edx, void *surface, int unused)
{
	g_origDraw(self, edx, surface, unused);
	if (g_failed) return;
	__try {
		// The chat's map ping reads the geometry too, so it is kept with the extras turned off.
		CaptureGeometry(self, surface);
		DrawRespawnTimers();
		__try {
			CollectRoutes();
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			g_routes.clear();
			g_routesFailed = true;
			LogE("game", "zoom map: ERROR reading the supply routes; the game draws them again");
		}
		if (!ExtrasEnabled()) return;
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
			// The box key held for a click without a drag: if marking positions uses the same key, the click marks.
			if (g_clickHook && g_clickHook(drag.startX, drag.startY)) return true;
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

bool ZoomViewOpen() { return !g_failed && ZoomOpen(); }

std::vector<ZoomMapTimer> ZoomViewTimers()
{
	if (g_failed || !ZoomOpen()) return {};
	return g_timers;
}

std::vector<ZoomRoute> ZoomViewRoutes()
{
	if (g_failed || g_routesFailed || !ZoomOpen()) return {};
	return g_routes;
}

namespace {
// A disc of radius r at (cx, cy) blended into the picture with `alpha`, its edge smoothed over a pixel.
void BlendDisc(uint32_t *buf, int w, int h, float cx, float cy, float r, uint32_t rgb, float alpha)
{
	const int x0 = std::max(0, static_cast<int>(std::floor(cx - r - 1))), x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + r + 1)));
	const int y0 = std::max(0, static_cast<int>(std::floor(cy - r - 1))), y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + r + 1)));
	const int sr = (rgb >> 16) & 0xff, sg = (rgb >> 8) & 0xff, sb = rgb & 0xff;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			const float cover = std::max(0.0f, std::min(1.0f, r + 0.5f - std::sqrt(dx * dx + dy * dy))) * alpha;
			if (cover <= 0.0f) continue;
			uint32_t &d = buf[static_cast<size_t>(y) * w + x];
			const int dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
			const int a = static_cast<int>(cover * 256);
			d = 0xff000000u | static_cast<uint32_t>(dr + ((sr - dr) * a >> 8)) << 16 | static_cast<uint32_t>(dg + ((sg - dg) * a >> 8)) << 8 |
				static_cast<uint32_t>(db + ((sb - db) * a >> 8));
		}
	}
}

// A triangle blended into the picture with `alpha`, its edges smoothed over a pixel: each pixel covered as far as it is inside all three.
void BlendTriangle(uint32_t *buf, int w, int h, float ax, float ay, float bx, float by, float cx, float cy, uint32_t rgb, float alpha)
{
	const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	if (std::fabs(area) < 0.01f) return;
	const float sign = area > 0 ? 1.0f : -1.0f;
	auto inside = [sign](float x0, float y0, float x1, float y1, float px, float py) {
		const float l = std::max(0.001f, std::hypot(x1 - x0, y1 - y0));
		return sign * ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) / l;
	};
	const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}) - 1))), x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}) + 1)));
	const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}) - 1))), y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}) + 1)));
	const int sr = (rgb >> 16) & 0xff, sg = (rgb >> 8) & 0xff, sb = rgb & 0xff;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			const float pxc = x + 0.5f, pyc = y + 0.5f;
			const float d = std::min({inside(ax, ay, bx, by, pxc, pyc), inside(bx, by, cx, cy, pxc, pyc), inside(cx, cy, ax, ay, pxc, pyc)});
			const float cover = std::max(0.0f, std::min(1.0f, d + 0.5f)) * alpha;
			if (cover <= 0.0f) continue;
			uint32_t &dst = buf[static_cast<size_t>(y) * w + x];
			const int dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
			const int a = static_cast<int>(cover * 256);
			dst = 0xff000000u | static_cast<uint32_t>(dr + ((sr - dr) * a >> 8)) << 16 | static_cast<uint32_t>(dg + ((sg - dg) * a >> 8)) << 8 |
				static_cast<uint32_t>(db + ((sb - db) * a >> 8));
		}
	}
}

uint32_t Lighter(uint32_t rgb, int toWhite)
{
	const int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
	return static_cast<uint32_t>(r + (255 - r) * toWhite / 100) << 16 | static_cast<uint32_t>(g + (255 - g) * toWhite / 100) << 8 |
		static_cast<uint32_t>(b + (255 - b) * toWhite / 100);
}
}

void ZoomViewDrawRoutes(uint32_t *buf, int w, int h, const std::vector<ZoomRoute> &routes, unsigned nowMs)
{
	if (!buf || w <= 0 || h <= 0) return;
	// Clear of the settlement's ring the game draws, and the dots that flow: their gap and their speed, in the picture's pixels.
	// The head: a slim arrow, as long as it is wide twice over, with a dark rim so it reads on any ground.
	const float kRing = std::max(9.0f, h / 70.0f), kGap = 13.0f, kSpeed = 13.0f, kHeadLong = 10.0f, kHeadHalf = 4.0f;
	for (const ZoomRoute &r : routes) {
		const float dx = r.bx - r.ax, dy = r.by - r.ay, len = std::sqrt(dx * dx + dy * dy);
		if (len < 2 * kRing + 6) continue;
		// Straight, from one settlement to the other.
		const float mx = (r.ax + r.bx) / 2, my = (r.ay + r.by) / 2;
		auto at = [&](float t, float &x, float &y) {
			const float u = 1 - t;
			x = u * u * r.ax + 2 * u * t * mx + t * t * r.bx;
			y = u * u * r.ay + 2 * u * t * my + t * t * r.by;
		};
		// The arc's points a pixel or two apart, with the length walked to each.
		const int steps = std::max(16, static_cast<int>(len / 2));
		std::vector<float> px(steps + 1), py(steps + 1), walked(steps + 1);
		for (int i = 0; i <= steps; ++i) {
			at(static_cast<float>(i) / steps, px[i], py[i]);
			walked[i] = i ? walked[i - 1] + std::hypot(px[i] - px[i - 1], py[i] - py[i - 1]) : 0.0f;
		}
		const float total = walked[steps], start = kRing, tip = total - kRing, stop = tip - kHeadLong * 0.7f;
		if (stop <= start) continue;
		auto point = [&](float d, float &x, float &y) {
			const auto k = std::lower_bound(walked.begin(), walked.end(), d) - walked.begin();
			const int i = static_cast<int>(std::max<std::ptrdiff_t>(1, std::min<std::ptrdiff_t>(k, steps)));
			const float seg = std::max(0.001f, walked[i] - walked[i - 1]), f = (d - walked[i - 1]) / seg;
			x = px[i - 1] + (px[i] - px[i - 1]) * f;
			y = py[i - 1] + (py[i] - py[i - 1]) * f;
		};
		// The track, faint, in the owner's colour.
		for (float d = start; d <= stop; d += 1.5f) {
			float x = 0, y = 0;
			point(d, x, y);
			BlendDisc(buf, w, h, x, y, 1.6f, r.rgb, 0.45f);
		}
		// The dots that flow to the settlement supplied.
		const uint32_t light = Lighter(r.rgb, 55);
		const float phase = std::fmod(nowMs / 1000.0f * kSpeed, kGap);
		for (float d = start + phase; d <= stop - 4; d += kGap) {
			float x = 0, y = 0;
			point(d, x, y);
			BlendDisc(buf, w, h, x, y, 2.4f, 0x000000, 0.35f);
			BlendDisc(buf, w, h, x, y, 1.9f, light, 0.95f);
		}
		// The head, its point on the edge of the settlement supplied.
		float ex = 0, ey = 0, qx = 0, qy = 0;
		point(tip, ex, ey);
		point(tip - kHeadLong, qx, qy);
		const float ux = (ex - qx) / kHeadLong, uy = (ey - qy) / kHeadLong;
		auto head = [&](float grow, uint32_t rgb, float alpha) {
			const float l = kHeadLong + grow * 1.6f, hw = kHeadHalf + grow;
			const float tx = ex + ux * grow, ty = ey + uy * grow, bx = tx - ux * l, by = ty - uy * l;
			BlendTriangle(buf, w, h, tx, ty, bx - uy * hw, by + ux * hw, bx + uy * hw, by - ux * hw, rgb, alpha);
		};
		head(1.2f, 0x000000, 0.4f);
		head(0.0f, light, 1.0f);
	}
}

void ZoomViewDrawTimers(uint32_t *buf, int w, int h, const std::vector<ZoomMapTimer> &timers, const wchar_t *face)
{
	if (!buf || w <= 0 || h <= 0 || timers.empty() || !face) return;
	// One small GDI bitmap for every label, kept between frames; only the render thread draws them.
	static HDC dc = nullptr;
	static HBITMAP bmp = nullptr;
	static uint32_t *bits = nullptr;
	static HFONT font = nullptr;
	static int fontPx = 0;
	static std::wstring fontFace;
	constexpr int kCanvasW = 192, kCanvasH = 72;
	if (!dc) {
		dc = CreateCompatibleDC(nullptr);
		if (!dc) return;
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = kCanvasW;
		bi.bmiHeader.biHeight = -kCanvasH;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void **>(&bits), nullptr, 0);
		if (!bmp || !bits) {
			DeleteDC(dc);
			dc = nullptr;
			bits = nullptr;
			return;
		}
		SelectObject(dc, bmp);
		SetBkMode(dc, TRANSPARENT);
	}
	// As the names over the chat's marks (chatui_view.cpp, PaintMarkName): 15 units of its scale, bold, in a black rim of a pixel.
	const int px = static_cast<int>(15 * std::max(0.85f, std::min(1.3f, h / 880.0f)) + 0.5f);
	// The face is part of the key: a font made before the brand's were registered is another one, and would stay.
	if (!font || px != fontPx || fontFace != face) {
		SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
		if (font) DeleteObject(font);
		// Grey antialiasing: the image is scaled to the screen later, and ClearType's coloured edges would show.
		font = CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
		fontPx = px;
		fontFace = face;
		if (!font) return;
	}
	SelectObject(dc, font);
	SetTextColor(dc, RGB(255, 255, 255));
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	// No box: white figures in a black rim of a pixel, as the names over the map's marks, read on grass, sand and snow alike.
	const int rim = 1, drop = 0, pad = rim + 1;
	constexpr int kInkR = 0xff, kInkG = 0xff, kInkB = 0xff, kRimR = 0, kRimG = 0, kRimB = 0, kRimAlpha = 255, kDropAlpha = 0;
	std::vector<uint8_t> ink, edge;
	for (const ZoomMapTimer &t : timers) {
		char text[16];
		RespawnTimeText(t.seconds, t.approximate, text);
		const int length = static_cast<int>(strlen(text));
		SIZE size = {};
		if (!GetTextExtentPoint32A(dc, text, length, &size)) continue;
		// Figures have no descent: the label ends at the baseline, which sits on the timer's point.
		const int bw = std::min(kCanvasW, static_cast<int>(size.cx) + 2 * pad), bh = std::min(kCanvasH, static_cast<int>(tm.tmAscent) + 2 * pad + drop);
		const int left = t.x - bw / 2, top = t.y - bh;
		if (left + bw <= 0 || top + bh <= 0 || left >= w || top >= h) continue;
		GdiFlush();
		for (int y = 0; y < bh; ++y) memset(bits + static_cast<size_t>(y) * kCanvasW, 0, static_cast<size_t>(bw) * 4);
		TextOutA(dc, pad, pad, text, length);
		GdiFlush();
		const size_t n = static_cast<size_t>(bw) * bh;
		ink.assign(n, 0);
		edge.assign(n, 0);
		for (int y = 0; y < bh; ++y) {
			for (int x = 0; x < bw; ++x) ink[static_cast<size_t>(y) * bw + x] = static_cast<uint8_t>(bits[y * kCanvasW + x] & 0xff);
		}
		for (int y = 0; y < bh; ++y) {
			for (int x = 0; x < bw; ++x) {
				int best = 0;
				for (int dy = -rim; dy <= rim && best < 255; ++dy) {
					const int sy = y + dy;
					if (sy < 0 || sy >= bh) continue;
					for (int dx = -rim; dx <= rim; ++dx) {
						const int sx = x + dx;
						if (sx < 0 || sx >= bw || (rim > 1 && std::abs(dx) == rim && std::abs(dy) == rim)) continue;
						best = std::max<int>(best, ink[static_cast<size_t>(sy) * bw + sx]);
					}
				}
				edge[static_cast<size_t>(y) * bw + x] = static_cast<uint8_t>(best);
			}
		}
		auto mix = [](uint32_t d, int r, int g, int b, int a) {
			const int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
			return (static_cast<uint32_t>(dr + (r - dr) * a / 255) << 16) | (static_cast<uint32_t>(dg + (g - dg) * a / 255) << 8) |
				static_cast<uint32_t>(db + (b - db) * a / 255);
		};
		for (int y = 0; y < bh; ++y) {
			const int ty = top + y;
			if (ty < 0 || ty >= h) continue;
			for (int x = 0; x < bw; ++x) {
				const int tx = left + x;
				if (tx < 0 || tx >= w) continue;
				const size_t i = static_cast<size_t>(y) * bw + x;
				const int shadow = y >= drop ? edge[i - static_cast<size_t>(drop) * bw] * kDropAlpha / 255 : 0;
				const int rimA = edge[i] * kRimAlpha / 255, inkA = ink[i];
				if (!shadow && !rimA) continue;
				uint32_t &dst = buf[static_cast<size_t>(ty) * w + tx];
				uint32_t c = dst;
				if (shadow) c = mix(c, kRimR, kRimG, kRimB, shadow);
				if (rimA) c = mix(c, kRimR, kRimG, kRimB, rimA);
				if (inkA) c = mix(c, kInkR, kInkG, kInkB, inkA);
				dst = 0xff000000u | c;
			}
		}
	}
}

bool ZoomViewContains(int x, int y) { return ZoomViewOpen() && InsideBitmap(x, y); }

bool ZoomViewBitmap(int *rect)
{
	if (!ZoomViewOpen()) return false;
	rect[0] = g_geo.left;
	rect[1] = g_geo.top;
	rect[2] = g_geo.right + 1;
	rect[3] = g_geo.bottom + 1;
	return true;
}

bool ZoomViewBoxUses(unsigned vk) { return !g_failed && vk && ExtrasEnabled() && ControlsZoomBoxVk() == vk; }

void ZoomViewSetClickHook(ZoomClickFn fn) { g_clickHook = fn; }

bool ZoomViewScreenPoint(int x, int y, int *point)
{
	if (g_failed || !ZoomOpen() || !InsideBitmap(x, y)) return false;
	int rect[4];
	__try {
		if (!ProjectedRect(x, y, x, y, rect)) return false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	point[0] = rect[0];
	point[1] = rect[1];
	return true;
}

bool ZoomViewPointOnScreen(int px, int py, int *x, int *y)
{
	if (g_failed || !ZoomOpen()) return false;
	int width = 0, height = 0;
	if (!ZoomBitmapSize(&width, &height) || width < 2 || height < 2) return false;
	// The picture is the map seen from above: the world goes straight across it, so two corners give the scale.
	int a[2] = {}, b[2] = {};
	if (!ZoomPixelToWorld(0, 0, a) || !ZoomPixelToWorld(width - 1, height - 1, b) || b[0] == a[0] || b[1] == a[1]) return false;
	// The point's y is the world's, shortened by the view (181/256) and raised by the ground's height there; one guess of the height
	// is close enough for a label.
	int worldY = py * 256 / 0xb5;
	void *map = *reinterpret_cast<void **>(addr::kMap);
	if (map && px >= 0 && worldY >= 0) {
		__try {
			unsigned position[2] = {static_cast<unsigned>(px), static_cast<unsigned>(worldY)};
			const unsigned ground = reinterpret_cast<Height_t>(addr::kGetHeightAt)(map, nullptr, position);
			worldY = (py + static_cast<int>(ground)) * 256 / 0xb5;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	const int spanX = g_geo.right - g_geo.left, spanY = g_geo.bottom - g_geo.top;
	*x = g_geo.left + (px - a[0]) * spanX / (b[0] - a[0]);
	*y = g_geo.top + (worldY - a[1]) * spanY / (b[1] - a[1]);
	return *x >= g_geo.left && *x <= g_geo.right && *y >= g_geo.top && *y <= g_geo.bottom;
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
	const bool ok = HookFunction(addr::kZoomMapOnDraw, &HookZoomDraw, &g_origDraw);
	if (!HookFunction(addr::kZoomMapDrawLinks, &HookDrawLinks, &g_origDrawLinks)) {
		g_routesFailed = true;
		LogW("game", "zoom map: could not hook the supply routes' drawing; the game's dotted lines stay");
	}
	const bool hideOk = HookFunction(addr::kZoomMapHide, &HookZoomHide, &g_origHide);
	if (!hideOk) LogW("game", "zoom map: could not hook Hide; a box selection in progress is ended when the map stops being drawn");
	if (!ok) g_failed = true;
	LogAt(ok ? kLogInfo : kLogError, "game", "zoom map extras (group icons and box selection): %s", ok ? (ExtrasEnabled() ? "ready" : "ready, turned off in the options") : "NOT available");
}
