// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/conquestui.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/conquest.h"
#include "ui/conquestui_view.h"
#include "ui/gamemenuui.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/savesui.h"

using namespace conquestui;

namespace {
const DWORD kReadEveryMs = 60;
const int kWheelLines = 3;
// The difficulties are easy, normal and hard: each visit of the map starts at normal.
const int kNormal = 1;
// A paint slower than this is logged, at most once every kSlowLogMs.
const DWORD kSlowLogMs = 5000;
const double kSlowPaintMs = 8, kSlowReadMs = 4;

struct State {
	View view;
	Painted painted;
	bool mouseDown = false;
	DWORD lastRead = 0;
	int wheelRest = 0;
	// Normal was picked for this visit of the map; the player's own choice stands until it closes.
	bool mediumSet = false;
	// A press on the map: its release goes to the map too, at the same pixel. The time and place of the last one tell a double click
	// when the window gets two presses instead of WM_LBUTTONDBLCLK.
	bool mapDown = false;
	POINT mapAt = {};
	DWORD mapDownAt = 0;
	POINT mapDownScreen = {-1000, -1000};
};

std::mutex g_lock;
State g_s;
bool g_swallowUp = false;

bool OtherScreenOwnsInput() { return ModUiIsOpen() || MpUiNeedsDraw() || SavesUiNeedsDraw(); }

bool Inside(const RECT &r, POINT at) { return PtInRect(&r, at) != 0; }

void SetScroll(int line) { g_s.view.scroll = std::max(0, std::min(g_s.painted.maxScroll, line)); }

void ScrollBy(int lines) { SetScroll(g_s.view.scroll + lines); }

// A drag of the description's bar, or a click on its track.
bool BarMouse(UINT msg, WPARAM wp, POINT at)
{
	ui::ScrollTo to;
	const bool took = ui::ScrollbarMouse(g_s.view.bar, g_s.painted.hits, msg, wp, at, to);
	if (to.id == kBarDescription) SetScroll(to.pos);
	return took;
}

// While the "you have won" box is up, only its OK reaches the game, which has its own button in the same place.
bool HandleVictory(UINT msg, POINT at, const RECT &ok)
{
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		RECT inner = ok;
		InflateRect(&inner, -1, -1);
		return !Inside(inner, at);
	}
	case WM_LBUTTONUP: return false;
	default: return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

bool SecondClick(POINT at)
{
	return GetTickCount() - g_s.mapDownAt <= GetDoubleClickTime() && std::abs(at.x - g_s.mapDownScreen.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
		std::abs(at.y - g_s.mapDownScreen.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
}

// A press on the map goes to the map's own handler: the game picks the territory, or plays it on a double click.
int MapPress(UINT msg, POINT at, POINT pixel)
{
	const bool twice = msg == WM_LBUTTONDBLCLK || SecondClick(at);
	g_s.mapDown = true;
	g_s.mapAt = pixel;
	g_s.mapDownAt = twice ? 0 : GetTickCount();
	g_s.mapDownScreen = at;
	return twice ? kConquestMouseDouble : kConquestMouseDown;
}

double MsSince(const LARGE_INTEGER &from)
{
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	return (now.QuadPart - from.QuadPart) * 1000.0 / freq.QuadPart;
}

// Game thread: the game's state into the screen's view.
void Refresh()
{
	LARGE_INTEGER from;
	QueryPerformanceCounter(&from);
	ConquestSnapshot fresh = ConquestRead();
	const double readMs = MsSince(from);
	static DWORD readLoggedAt = 0;
	if (readMs >= kSlowReadMs && GetTickCount() - readLoggedAt >= kSlowLogMs) {
		readLoggedAt = GetTickCount();
		LogD("perf", "conquest screen: reading the game's state took %.1f ms", readMs);
	}
	bool setNormal = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (fresh.open && !g_s.mediumSet && fresh.difficulties.size() >= 3) {
			g_s.mediumSet = true;
			setNormal = fresh.difficulty != kNormal;
		}
	}
	if (setNormal) {
		ConquestSetDifficulty(kNormal);
		fresh = ConquestRead();
	}
	std::lock_guard<std::mutex> lock(g_lock);
	if (fresh.open != g_s.view.game.open) {
		LogI("ui", "conquest map %s", fresh.open ? "open: the mod's screen covers the game's dialog" : "closed");
		g_s.painted = Painted();
		g_s.mouseDown = false;
		g_s.mapDown = false;
		g_s.view.scroll = 0;
	}
	if (!fresh.open) g_s.mediumSet = false;
	if (fresh.selected != g_s.view.game.selected) g_s.view.scroll = 0;
	g_s.view.game = std::move(fresh);
	g_s.lastRead = GetTickCount();
}
}

// The game thread runs this only when the game draws a frame, which its menus do a few times a second: what the player does on the
// screen is read again at once (ConquestUiHandleMessage), not on the next of those frames.
void ConquestUiGameTick()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.view.game.open && GetTickCount() - g_s.lastRead < kReadEveryMs) return;
	}
	Refresh();
}

bool ConquestUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.view.game.open || g_s.view.victory;
}

void ConquestUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	int pressAction = kActNone, pressArg = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.view.game.open && !g_s.view.victory) return;
		view = g_s.view;
		if (g_s.mouseDown) pressAction = ui::HitActionAt(g_s.painted.hits, {view.mouseX, view.mouseY}, pressArg, kActNone);
	}
	view.questionDrawn = view.game.confirm && GameMenuUiNeedsDraw();
	LARGE_INTEGER from;
	QueryPerformanceCounter(&from);
	Painted painted;
	const PaintCost cost = PaintConquest(buf, w, h, view, pressAction, pressArg, painted);
	const double ms = MsSince(from);
	static DWORD loggedAt = 0;
	if (ms >= kSlowPaintMs && GetTickCount() - loggedAt >= kSlowLogMs) {
		loggedAt = GetTickCount();
		LogD("perf", "conquest screen: a paint took %.1f ms at %dx%d (%s; the map built in %.1f ms)", ms, w, h, cost.full ? "whole screen" : "map only", cost.mapMs);
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.painted = std::move(painted);
}

bool ConquestUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		bool mapUp = false;
		POINT pixel = {};
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.mouseDown = false;
			g_s.view.bar = ui::ScrollDrag();
			mapUp = g_s.mapDown;
			pixel = g_s.mapAt;
			g_s.mapDown = false;
		}
		if (mapUp) ConquestMapMouse(kConquestMouseUp, pixel.x, pixel.y);
		return true;
	}
	// A release another screen took leaves the flag up: it must not eat the release of a later click.
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) g_swallowUp = false;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	const bool mouse = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	const bool other = mouse && OtherScreenOwnsInput();
	int action = kActNone, arg = 0, mapEvent = 0;
	POINT pixel = {};
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const View &v = g_s.view;
		if (!v.game.open && !v.victory) return false;
		// Without the map's picture nothing covers the game's dialog (PaintConquest): it keeps its own input.
		if (!v.victory && !v.game.map) return false;
		if (msg == WM_MOUSEMOVE) {
			g_s.view.mouseX = at.x;
			g_s.view.mouseY = at.y;
			BarMouse(msg, wp, at);
			// The game's controls are still there under the screen: they get no hover, and so show no tooltips.
			return !other && !v.victory && !v.game.confirm;
		}
		if (other) return false;
		if (v.victory) return HandleVictory(msg, at, g_s.painted.ok);
		// The game's question is drawn and answered by the game; the keys stay the game's too (Esc is its Quit).
		if (v.game.confirm || !mouse) return false;
		const Painted &pt = g_s.painted;
		const POINT last = {v.mouseX, v.mouseY};
		switch (msg) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			g_s.mouseDown = true;
			g_swallowUp = true;
			if (BarMouse(msg, wp, at)) break;
			if (MapPixelAt(pt, at, pixel)) {
				mapEvent = MapPress(msg, at, pixel);
				break;
			}
			action = ui::HitActionAt(pt.hits, at, arg, kActNone);
			break;
		case WM_MOUSEWHEEL:
			// The wheel carries screen coordinates: the last position of the mouse is the one inside the game.
			if (Inside(pt.desc, last)) ScrollBy(-ui::WheelSteps(g_s.wheelRest, GET_WHEEL_DELTA_WPARAM(wp)) * kWheelLines);
			break;
		default: break;
		}
	}
	if (!mapEvent && action == kActNone) return true;
	// The game's dialog can open others and call back into the mod: no lock here.
	LARGE_INTEGER from;
	QueryPerformanceCounter(&from);
	if (mapEvent) ConquestMapMouse(mapEvent, pixel.x, pixel.y);
	else if (action == kActNative) ConquestPress(arg);
	else if (action == kActBack) ConquestPress(kConquestQuit);
	else if (action == kActDifficulty) ConquestSetDifficulty(arg);
	const double gameMs = MsSince(from);
	Refresh();
	LogD("perf", "conquest screen: a click took %.1f ms (the game %.1f ms, reading it back %.1f ms)", MsSince(from), gameMs, MsSince(from) - gameMs);
	return true;
}

void ConquestUiVictoryShown(const char *text)
{
	LogI("ui", "conquest won: the victory box is drawn by the mod");
	ConquestNoteConquestWon();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.view.victory = true;
	g_s.view.victoryText = text ? text : "";
	g_s.painted.ok = RECT();
}

void ConquestUiVictoryClosed()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.view.victory = false;
	g_s.view.victoryText.clear();
}
