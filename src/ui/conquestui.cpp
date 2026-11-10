// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/conquestui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/conquest.h"
#include "ui/conquestui_view.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/savesui.h"

using namespace conquestui;

namespace {
const DWORD kReadEveryMs = 60;
const int kWheelLines = 3;

struct State {
	View view;
	Painted painted;
	bool mouseDown = false;
	DWORD lastRead = 0;
};

std::mutex g_lock;
State g_s;
bool g_swallowUp = false;

bool OtherScreenOwnsInput() { return ModUiIsOpen() || MpUiNeedsDraw() || SavesUiNeedsDraw(); }

bool Inside(const RECT &r, POINT at) { return PtInRect(&r, at) != 0; }

int HitAt(const std::vector<ui::Hit> &hits, POINT at, int &arg)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

void ScrollBy(int lines)
{
	g_s.view.scroll = std::max(0, std::min(g_s.painted.maxScroll, g_s.view.scroll + lines));
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
}

void ConquestUiGameTick()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.view.game.open && GetTickCount() - g_s.lastRead < kReadEveryMs) return;
	}
	ConquestSnapshot fresh = ConquestRead();
	std::lock_guard<std::mutex> lock(g_lock);
	if (fresh.open != g_s.view.game.open) {
		LogI("ui", "conquest map %s", fresh.open ? "open: the new panel is drawn over it" : "closed");
		g_s.painted = Painted();
		g_s.mouseDown = false;
		g_s.view.scroll = 0;
	}
	if (fresh.selected != g_s.view.game.selected) g_s.view.scroll = 0;
	g_s.view.game = std::move(fresh);
	g_s.lastRead = GetTickCount();
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
		if (g_s.mouseDown) pressAction = HitAt(g_s.painted.hits, {view.mouseX, view.mouseY}, pressArg);
	}
	Painted painted;
	PaintConquest(buf, w, h, view, pressAction, pressArg, painted);
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.painted = std::move(painted);
}

bool ConquestUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	(void)wp;
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		return true;
	}
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	int action = kActNone, arg = 0;
	const bool other = msg != WM_MOUSEMOVE && OtherScreenOwnsInput();
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const View &v = g_s.view;
		if (!v.game.open && !v.victory) return false;
		if (msg == WM_MOUSEMOVE) {
			g_s.view.mouseX = at.x;
			g_s.view.mouseY = at.y;
			return false;
		}
		if (other) return false;
		if (v.victory) return HandleVictory(msg, at, g_s.painted.ok);
		if (v.game.confirm) return false;
		const Painted &pt = g_s.painted;
		const POINT mouse = {v.mouseX, v.mouseY};
		switch (msg) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			if (!Inside(pt.panel, at) && !Inside(pt.path, at) && !Inside(pt.legend, at)) return false;
			g_s.mouseDown = true;
			g_swallowUp = true;
			action = HitAt(pt.hits, at, arg);
			if (action == kActScroll) {
				ScrollBy(arg);
				action = kActNone;
			}
			break;
		case WM_MOUSEWHEEL:
			// The wheel carries screen coordinates: the last position of the mouse is the one inside the game.
			if (Inside(pt.panel, mouse)) ScrollBy(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * kWheelLines);
			return Inside(pt.panel, mouse) || Inside(pt.path, mouse) || Inside(pt.legend, mouse);
		case WM_LBUTTONUP: return false;
		default:
			if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return Inside(pt.panel, at) || Inside(pt.path, at) || Inside(pt.legend, at);
			return false;
		}
	}
	// The game's dialog can open others and call back into the mod: no lock here.
	if (action == kActNative) ConquestPress(arg);
	else if (action == kActDifficulty) ConquestSetDifficulty(arg);
	return true;
}

void ConquestUiVictoryShown(const char *text)
{
	LogI("ui", "conquest won: the victory box is drawn by the mod");
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
