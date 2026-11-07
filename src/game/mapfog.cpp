// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "core/config.h"
#include "core/log.h"
#include "game/mapfog.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"

namespace {
typedef void *(__fastcall *Rebuild_t)(void *self);
typedef unsigned(__fastcall *VisionRun_t)(void *query, void *edx, int *point, void *player);
typedef unsigned char(__fastcall *IsExplored_t)(void *exploration, void *edx, unsigned *point, unsigned player);
typedef int(__cdecl *MemLock_t)(unsigned handle);
typedef void(__cdecl *MemUnlock_t)(unsigned handle);
typedef void *(__fastcall *ToMap_t)(void *base, void *edx, int *out, int *in);
typedef int(__fastcall *PixelPtr_t)(void *bitmap, void *edx, int x, int y);

typedef void(__fastcall *CenterOn_t)(void *view, void *edx, int *pos, int *x);
CenterOn_t g_origCenterOn = nullptr;
Rebuild_t g_origRebuild = nullptr;
bool g_fogFailed = false;
bool FogOn() { return !g_fogFailed && GetConfig().zoomFog; }
int g_calls = 0;
void *g_zoomThis = nullptr;
DWORD g_lastNatural = 0;
bool g_forcing = false;
int g_exploredN = 0, g_visibleN = 0;

struct FogCache {
	std::vector<unsigned short> pixels, next;
	void *zoom = nullptr, *src = nullptr;
	int w = 0, h = 0;
	unsigned short fogColor = 0;
	bool valid = false;
	DWORD usedAt = 0;
};
FogCache g_cache;
bool g_overlayFromCache = false;

template <typename T>
T At(const void *p, size_t off) { return *reinterpret_cast<const T *>(static_cast<const char *>(p) + off); }

void PixelToWorld(void *zoom, int px, int py, int *out)
{
	const int *vbptr = At<const int *>(zoom, 0);
	const int off = vbptr[3];
	void *base = static_cast<char *>(zoom) + off;
	void **vt = *reinterpret_cast<void ***>(base);
	int in[2] = {px, py};
	out[0] = out[1] = 0;
	reinterpret_cast<ToMap_t>(vt[0x5c / 4])(base, nullptr, out, in);
}

int Darken(void *zoom)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *map = *reinterpret_cast<char **>(addr::kMap);
	if (!game || !map) return -1;
	char *player = At<char *>(game, 0x12c8);
	char *vis = At<char *>(game, 0x44d4);
	if (!player || !vis || At<int>(vis, 0xd4) == 0) return -1;
	void *exploration = At<void *>(map, 0xb0);
	const unsigned playerIndex = static_cast<unsigned>((player - game - 0x12cc) / 800);
	if (!exploration || playerIndex > 7) return -1;
	char *dst = At<char *>(zoom, 0x14), *src = At<char *>(zoom, 0x18);
	if (!dst || !src) return -1;
	const int w = At<int>(dst, 8), h = At<int>(dst, 0xc);
	if (w < 16 || h < 16 || w > 8192 || h > 8192 || At<short>(dst, 0x10) != 16 || At<int>(src, 8) != w || At<int>(src, 0xc) != h) return -1;
	alignas(16) unsigned char viewer[0x40] = {};
	memcpy(viewer, player, sizeof(viewer));
	const unsigned selfBit = At<unsigned>(player, 0xc);
	const unsigned receiveMask = At<unsigned>(player, 0x18) | selfBit;
	*reinterpret_cast<unsigned *>(viewer + 0x14) = receiveMask;
	const bool hasAllies = receiveMask != selfBit;
	const unsigned short fogColor = At<unsigned short>(zoom, 0x30);

	const unsigned hd = At<unsigned>(dst, 4), hs = At<unsigned>(src, 4);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(hd);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(hs);
	int asked = 0;
	const int step = 8, gw = (w + step - 1) / step + 1, gh = (h + step - 1) / step + 1;
	auto rowPtr = [](char *bmp, int y) { return reinterpret_cast<unsigned short *>(reinterpret_cast<PixelPtr_t>((*reinterpret_cast<void ***>(bmp))[0x10 / 4])(bmp, nullptr, 0, y)); };

	static std::vector<unsigned char> explored, seen, nextExplored, nextSeen;
	static DWORD gridTick = 0;
	static int gridW = 0, gridH = 0, nextSlice = 0;
	const int slices = 6;
	const DWORD now = GetTickCount();
	int toDo = 0;
	if (gridW != gw || gridH != gh) {
		explored.assign(static_cast<size_t>(gw) * gh, 0);
		seen.assign(static_cast<size_t>(gw) * gh, 0);
		nextExplored = explored;
		nextSeen = seen;
		gridW = gw;
		gridH = gh;
		nextSlice = 0;
		toDo = slices;
	}
	else {
		toDo = std::max(1, std::min(slices - nextSlice, static_cast<int>((now - gridTick) / 16)));
	}
	gridTick = now;
	g_exploredN = g_visibleN = 0;
	alignas(16) unsigned char query[32] = {};
	auto sampleRow = [&](int gy) {
		bool changed = false;
		const int py = std::min(h - 1, gy * step);
		for (int gx = 0; gx < gw; gx++) {
			const int px = std::min(w - 1, gx * step);
			const size_t idx = static_cast<size_t>(gy) * gw + gx;
			nextExplored[idx] = 0;
			nextSeen[idx] = 0;
			int world[2] = {};
			PixelToWorld(zoom, px, py, world);
			asked++;
			const bool wasExplored = reinterpret_cast<IsExplored_t>(addr::kIsExplored)(exploration, nullptr, reinterpret_cast<unsigned *>(world), playerIndex) != 0;
			if (wasExplored || hasAllies) {
				memset(query, 0, sizeof(query));
				const bool isSeen = reinterpret_cast<VisionRun_t>(addr::kVisionRun)(query, nullptr, world, viewer) != 0;
				if (wasExplored || isSeen) {
					nextExplored[idx] = 1;
					g_exploredN++;
					if (isSeen) {
						nextSeen[idx] = 1;
						g_visibleN++;
					}
				}
			}
			changed = changed || nextExplored[idx] != explored[idx];
		}
		return changed;
	};
	static DWORD lastFullPass = 0;
	bool exploredChanged = false;
	for (int k = 0; k < toDo; k++) {
		const int slice = nextSlice + k;
		for (int gy = slice; gy < gh; gy += slices) exploredChanged = sampleRow(gy) || exploredChanged;
	}
	nextSlice = (nextSlice + toDo) % slices;
	if (exploredChanged && toDo < slices && now - lastFullPass >= 250) {
		lastFullPass = now;
		for (int gy = 0; gy < gh; gy++) sampleRow(gy);
		nextSlice = 0;
	}
	static std::vector<int> fogA, seenS, blurA, blurB;
	bool gridsUpdated = false;
	if (nextSlice == 0 || fogA.size() != explored.size()) {
		gridsUpdated = true;
		explored = nextExplored;
		seen = nextSeen;
		const int radius = 2, taps = radius * 2 + 1;
		auto soften = [&](const std::vector<unsigned char> &in, std::vector<int> &out) {
			blurA.resize(in.size());
			blurB.resize(in.size());
			for (size_t i = 0; i < in.size(); i++) blurA[i] = in[i] ? 256 : 0;
			for (int pass = 0; pass < 3; pass++) {
				for (int gy = 0; gy < gh; gy++) {
					for (int gx = 0; gx < gw; gx++) {
						int sum = 0;
						for (int d = -radius; d <= radius; d++) sum += blurA[static_cast<size_t>(gy) * gw + std::max(0, std::min(gw - 1, gx + d))];
						blurB[static_cast<size_t>(gy) * gw + gx] = sum / taps;
					}
				}
				for (int gy = 0; gy < gh; gy++) {
					for (int gx = 0; gx < gw; gx++) {
						int sum = 0;
						for (int d = -radius; d <= radius; d++) sum += blurB[static_cast<size_t>(std::max(0, std::min(gh - 1, gy + d))) * gw + gx];
						blurA[static_cast<size_t>(gy) * gw + gx] = sum / taps;
					}
				}
			}
			out = blurA;
		};
		soften(explored, fogA);
		for (int &a : fogA) a = std::max(0, std::min(256, a * 2 - 256));
		soften(seen, seenS);
	}

	static const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
	auto smooth = [](int a) { return a * a * (768 - 2 * a) >> 16; };
	auto shade = [fogColor](unsigned p, int k, int t, int dither) {
		unsigned out = 0;
		for (int shift = 10; shift >= 0; shift -= 5) {
			const int v = static_cast<int>((fogColor >> shift) & 31) * (256 - t) * 256 + static_cast<int>((p >> shift) & 31) * k * t;
			out |= static_cast<unsigned>(std::min(31, (v + dither) >> 16)) << shift;
		}
		return static_cast<unsigned short>(out);
	};
	const bool sameBitmaps = g_cache.valid && g_cache.zoom == zoom && g_cache.src == src && g_cache.w == w && g_cache.h == h;
	const bool dirty = gridsUpdated || !sameBitmaps || !g_overlayFromCache || g_cache.fogColor != fogColor || now - g_cache.usedAt > 500;
	g_cache.usedAt = now;
	if (dirty) {
		const size_t total = static_cast<size_t>(w) * h;
		g_cache.next.resize(total);
		for (int y = 0; y < h; y++) {
			const unsigned short *srow = rowPtr(src, y);
			unsigned short *out = &g_cache.next[static_cast<size_t>(y) * w];
			if (!srow) {
				std::fill(out, out + w, fogColor);
				continue;
			}
			const int gy = y / step, fy = (y % step) * 256 / step;
			for (int gx = 0; gx < gw - 1; gx++) {
				const size_t idx = static_cast<size_t>(gy) * gw + gx;
				const int *a0 = &fogA[idx], *a1 = a0 + gw;
				const int *s0 = &seenS[idx], *s1 = s0 + gw;
				const int x0 = gx * step, x1 = std::min(w, x0 + step);
				if (x0 >= w) break;
				if (!(a0[0] | a0[1] | a1[0] | a1[1])) {
					std::fill(out + x0, out + x1, fogColor);
					continue;
				}
				const bool clear = (a0[0] & a0[1] & a1[0] & a1[1]) == 256;
				const bool allSeen = (s0[0] & s0[1] & s1[0] & s1[1]) == 256;
				if (clear && allSeen) {
					std::copy(srow + x0, srow + x1, out + x0);
					continue;
				}
				for (int x = x0; x < x1; x++) {
					const int fx = (x - x0) * 256 / step;
					int k = 256, t = 256;
					if (!allSeen) {
						const int top = s0[0] * (256 - fx) + s0[1] * fx, bot = s1[0] * (256 - fx) + s1[1] * fx;
						k = 256 - ((256 - smooth((top * (256 - fy) + bot * fy) >> 16)) * 115 >> 8);
					}
					if (!clear) {
						const int top = a0[0] * (256 - fx) + a0[1] * fx, bot = a1[0] * (256 - fx) + a1[1] * fx;
						t = smooth((top * (256 - fy) + bot * fy) >> 16);
					}
					out[x] = shade(srow[x], k, t, kBayer[y & 3][x & 3] * 4096 + 2048);
				}
			}
		}
		const bool fromCache = g_overlayFromCache && sameBitmaps && g_cache.pixels.size() == total;
		for (int y = 0; y < h; y++) {
			unsigned short *drow = rowPtr(dst, y);
			const unsigned short *srow = rowPtr(src, y);
			if (!drow || !srow) continue;
			const unsigned short *was = fromCache ? &g_cache.pixels[static_cast<size_t>(y) * w] : nullptr;
			const unsigned short *fresh = &g_cache.next[static_cast<size_t>(y) * w];
			for (int x = 0; x < w; x++) {
				const bool background = was ? drow[x] == was[x] : drow[x] == srow[x] || drow[x] == fogColor;
				if (background) drow[x] = fresh[x];
			}
		}
		g_cache.pixels.swap(g_cache.next);
		g_cache.zoom = zoom;
		g_cache.src = src;
		g_cache.w = w;
		g_cache.h = h;
		g_cache.fogColor = fogColor;
		g_cache.valid = true;
	}
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(hs);
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(hd);
	return asked;
}

int SafeDarken(void *zoom)
{
	__try {
		return Darken(zoom);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_fogFailed = true;
		return -2;
	}
}

typedef void(__fastcall *FogOverlay_t)(void *self, void *edx, int x0, int y0, int x1, int y1);
FogOverlay_t g_origFogOverlay = nullptr;
const int kFogOverlayColumns = 0x800;

unsigned short *RowPtr(char *bmp, int y)
{
	return reinterpret_cast<unsigned short *>(reinterpret_cast<PixelPtr_t>((*reinterpret_cast<void ***>(bmp))[0x10 / 4])(bmp, nullptr, 0, y));
}

int FogWideColumns(void *zoom, int x0, int y0, int x1, int y1)
{
	char *dst = At<char *>(zoom, 0x14), *src = At<char *>(zoom, 0x18);
	if (!dst || !src) return -1;
	const int w = At<int>(dst, 8), h = At<int>(dst, 0xc);
	if (At<short>(dst, 0x10) != 16 || At<int>(src, 8) != w || At<int>(src, 0xc) != h) return -1;
	x1 = std::min(x1, w - 1);
	y0 = std::max(y0, 0);
	y1 = std::min(y1, h - 1);
	if (x0 > x1 || y0 > y1) return 0;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *map = *reinterpret_cast<char **>(addr::kMap);
	char *player = game ? At<char *>(game, 0x12c8) : nullptr;
	char *vis = game ? At<char *>(game, 0x44d4) : nullptr;
	void *exploration = map ? At<void *>(map, 0xb0) : nullptr;
	const unsigned playerIndex = player ? static_cast<unsigned>((player - game - 0x12cc) / 800) : 8;
	const bool fogOn = vis && At<int>(vis, 0xd4) != 0 && exploration && playerIndex <= 7;
	const unsigned short fogColor = At<unsigned short>(zoom, 0x30);
	const int step = 8, gw = (x1 - x0) / step + 1;
	std::vector<unsigned char> fogged(static_cast<size_t>(gw), 0);
	const unsigned hd = At<unsigned>(dst, 4), hs = At<unsigned>(src, 4);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(hd);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(hs);
	for (int by = y0; by <= y1; by += step) {
		const int py = std::min(y1, by + step / 2);
		for (int gx = 0; gx < gw; gx++) {
			fogged[gx] = 0;
			if (!fogOn) continue;
			int world[2] = {};
			PixelToWorld(zoom, std::min(x1, x0 + gx * step + step / 2), py, world);
			fogged[gx] = reinterpret_cast<IsExplored_t>(addr::kIsExplored)(exploration, nullptr, reinterpret_cast<unsigned *>(world), playerIndex) == 0;
		}
		for (int y = by; y <= y1 && y < by + step; y++) {
			unsigned short *drow = RowPtr(dst, y);
			const unsigned short *srow = RowPtr(src, y);
			if (!drow || !srow) continue;
			for (int x = x0; x <= x1; x++) drow[x] = fogged[(x - x0) / step] ? fogColor : srow[x];
		}
	}
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(hs);
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(hd);
	return gw;
}

int SafeFogWideColumns(void *zoom, int x0, int y0, int x1, int y1)
{
	__try {
		return FogWideColumns(zoom, x0, y0, x1, y1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -2;
	}
}

bool CopyCachedFog(void *zoom, int x0, int y0, int x1, int y1)
{
	if (!FogOn() || !g_cache.valid || zoom != g_cache.zoom) return false;
	char *dst = At<char *>(zoom, 0x14), *src = At<char *>(zoom, 0x18);
	if (!dst || src != g_cache.src || At<int>(dst, 8) != g_cache.w || At<int>(dst, 0xc) != g_cache.h || At<short>(dst, 0x10) != 16) return false;
	if (x0 < 0 || y0 < 0 || x1 >= g_cache.w || y1 >= g_cache.h || x0 > x1 || y0 > y1) return false;
	const unsigned hd = At<unsigned>(dst, 4);
	reinterpret_cast<MemLock_t>(addr::kMemLock)(hd);
	for (int y = y0; y <= y1; y++) {
		unsigned short *drow = RowPtr(dst, y);
		if (drow) memcpy(drow + x0, &g_cache.pixels[static_cast<size_t>(y) * g_cache.w + x0], static_cast<size_t>(x1 - x0 + 1) * 2);
	}
	reinterpret_cast<MemUnlock_t>(addr::kMemUnlock)(hd);
	return true;
}

bool SafeCopyCachedFog(void *zoom, int x0, int y0, int x1, int y1)
{
	__try {
		return CopyCachedFog(zoom, x0, y0, x1, y1);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_cache.valid = false;
		return false;
	}
}

void __fastcall HookFogOverlay(void *self, void *edx, int x0, int y0, int x1, int y1)
{
	if (SafeCopyCachedFog(self, x0, y0, x1, y1)) {
		g_overlayFromCache = true;
		return;
	}
	if (x1 < kFogOverlayColumns) {
		g_origFogOverlay(self, edx, x0, y0, x1, y1);
		return;
	}
	if (x0 < kFogOverlayColumns) g_origFogOverlay(self, edx, x0, y0, kFogOverlayColumns - 1, y1);
	const int from = std::max(x0, kFogOverlayColumns);
	const int r = SafeFogWideColumns(self, from, y0, x1, y1);
	static bool logged = false, failed = false;
	if (!logged) {
		logged = true;
		LogI("game", "zoom map: %d px wide; the game draws the fog of 2048 columns, the mod draws columns %d to %d", x1 + 1, from, x1);
	}
	if (r < 0 && !failed) {
		failed = true;
		LogE("game", "zoom map: ERROR drawing the fog of the columns past 2048 (%d)", r);
	}
}

void __fastcall NoZoomNotify(void *) {}
void *g_fakeVtable[64];
void *g_fakeZoom[2] = {g_fakeVtable, nullptr};

void __fastcall HookCenterOn(void *view, void *edx, int *pos, int *x)
{
	void **global = reinterpret_cast<void **>(addr::kZoomMapGlobal);
	if (*global) {
		g_origCenterOn(view, edx, pos, x);
		return;
	}
	static bool filled = false;
	if (!filled) {
		for (void *&v : g_fakeVtable) v = reinterpret_cast<void *>(&NoZoomNotify);
		filled = true;
	}
	static int logged = 0;
	if (logged++ < 3) LogD("game", "camera: centering before the zoom map exists (returning to a game in progress); crash avoided");
	*global = g_fakeZoom;
	__try {
		g_origCenterOn(view, edx, pos, x);
	}
	__finally {
		*global = nullptr;
	}
}

void *__fastcall HookRebuild(void *self)
{
	if (!g_forcing) {
		g_zoomThis = self;
		g_lastNatural = GetTickCount();
	}
	g_overlayFromCache = false;
	void *r = g_origRebuild(self);
	if (!FogOn() || r != nullptr) return r;
	const DWORD t0 = GetTickCount();
	const int asked = SafeDarken(self);
	if (asked < 0) g_cache.valid = false;
	if (asked == -2) LogE("game", "map fog: ERROR, disabled");
	else if (asked > 0 && g_calls++ < 20) LogD("game", "map fog: %d points queried, %d explored, %d in view, %lu ms", asked, g_exploredN, g_visibleN, GetTickCount() - t0);
	return r;
}

void *ForceRebuild(void *self)
{
	__try {
		return HookRebuild(self);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_zoomThis = nullptr;
		return nullptr;
	}
}

}

std::atomic<int> g_images{0};

void InvalidateZoomWindow(void *zoom)
{
	char *dlg = *reinterpret_cast<char **>(addr::kZoomDialog);
	if (!dlg) return;
	const int *vb = *reinterpret_cast<const int **>(dlg);
	char *win = dlg + vb[2];
	reinterpret_cast<void(__fastcall *)(void *, void *, int, int)>(addr::kInvalidateRect)(win, nullptr, 0, 1);
}

void ZoomFogNoteImage() { g_images++; }

long long ZoomFogGameThreadTick()
{
	const int refreshMs = GetConfig().zoomRefreshMs;
	if (!g_zoomThis || !g_origRebuild || refreshMs <= 0 || g_forcing) return 0;
	static LARGE_INTEGER freq = {}, lastForced = {}, statStart = {};
	static int forced = 0, logged = 0, imagesAtStart = 0;
	static unsigned long long spent = 0;
	if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const DWORD tick = GetTickCount();
	if (tick - g_lastNatural > 1200) {
		statStart.QuadPart = 0;
		return 0;
	}
	if (!statStart.QuadPart) {
		statStart = now;
		forced = 0;
		spent = 0;
		imagesAtStart = g_images;
	}
	const long long elapsedUs = (now.QuadPart - statStart.QuadPart) * 1000000 / freq.QuadPart;
	if (elapsedUs >= 2000000 && logged < 12) {
		logged++;
		LogD("game", "zoom map open: %d game images per second, %d forced rebuilds per second, %.1f ms each", static_cast<int>((g_images - imagesAtStart) * 1000000LL / elapsedUs), static_cast<int>(forced * 1000000LL / elapsedUs), forced ? static_cast<double>(spent) / forced / 1000.0 : 0.0);
		statStart = now;
		forced = 0;
		spent = 0;
		imagesAtStart = g_images;
	}
	if ((now.QuadPart - lastForced.QuadPart) * 1000 / freq.QuadPart < refreshMs) return 0;
	lastForced = now;
	g_forcing = true;
	ForceRebuild(g_zoomThis);
	if (g_zoomThis) {
		__try {
			InvalidateZoomWindow(g_zoomThis);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	g_forcing = false;
	LARGE_INTEGER b;
	QueryPerformanceCounter(&b);
	forced++;
	spent += static_cast<unsigned long long>((b.QuadPart - now.QuadPart) * 1000000 / freq.QuadPart);
	return b.QuadPart - now.QuadPart;
}

void InstallZoomFog(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookCreate(reinterpret_cast<void *>(addr::kRebuild), reinterpret_cast<void *>(&HookRebuild), reinterpret_cast<void **>(&g_origRebuild)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kRebuild)) == MH_OK;
	const bool okCenter = HookCreate(reinterpret_cast<void *>(addr::kCenterOn), reinterpret_cast<void *>(&HookCenterOn), reinterpret_cast<void **>(&g_origCenterOn)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kCenterOn)) == MH_OK;
	const bool okWide = HookCreate(reinterpret_cast<void *>(addr::kZoomFogOverlay), reinterpret_cast<void *>(&HookFogOverlay), reinterpret_cast<void **>(&g_origFogOverlay)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kZoomFogOverlay)) == MH_OK;
	LogAt(okCenter ? kLogInfo : kLogError, "game", "camera: CenterOn protection without zoom map %s", okCenter ? "ready" : "NOT available");
	LogAt(okWide ? kLogInfo : kLogError, "game", "zoom map: screens wider than 2048 px %s", okWide ? "protected" : "NOT protected");
	LogAt(ok ? kLogInfo : kLogError, "game", "zoom map fog: %s", ok ? (GetConfig().zoomFog ? "ready" : "ready, turned off in the options") : "NOT available");
}

bool ZoomBitmapSize(int *width, int *height)
{
	void *zoom = g_zoomThis;
	if (!zoom) return false;
	__try {
		char *bitmap = At<char *>(zoom, 0x14);
		if (!bitmap) return false;
		*width = At<int>(bitmap, 8);
		*height = At<int>(bitmap, 0xc);
		return *width > 0 && *height > 0 && *width < 16384 && *height < 16384;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ZoomPixelToWorld(int px, int py, int *world)
{
	void *zoom = g_zoomThis;
	if (!zoom) return false;
	__try {
		PixelToWorld(zoom, px, py, world);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
