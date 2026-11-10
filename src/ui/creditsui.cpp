// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/creditsui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/creditstext.h"
#include "core/log.h"
#include "core/modcredits.h"
#include "game/credits.h"
#include "net/patrons.h"
#include "ui/creditsui_view.h"
#include "ui/modui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace creditsui;

namespace {

const DWORD kRollEveryMs = 1000, kCloseTimeoutMs = 2000;
const int kWheelStep = 60, kKeyStep = 40;
// A stall of the drawing slows the text down for a moment instead of making it jump.
const double kMaxStep = 0.1;

struct State {
	void *dlg = nullptr;
	View view;
	std::vector<Hit> hits;
	PaintOut out;
	float scale = 1.0f;
	int speed = 100;
	LARGE_INTEGER last = {};
	bool mouseDown = false, dragging = false;
	int dragY = 0;
	double dragScroll = 0;
	DWORD closedAt = 0, rollAt = 0;
	size_t patrons = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;
unsigned g_stamp = 0;

std::vector<Patron> RollPatrons(bool &read)
{
	int state = 0;
	std::vector<Patron> out;
	for (const PatronRollEntry &e : PatronsRoll(state)) {
		if (!e.name.empty()) out.push_back({e.name, e.tier});
	}
	read = state == 1;
	return out;
}

// On the game thread: the game's text is read once, when its screen opens.
std::shared_ptr<Content> Build(std::vector<Patron> patrons)
{
	auto c = std::make_shared<Content>();
	c->mod = ResurrectedCredits();
	std::string text;
	if (CreditsGameText(text)) c->game = ParseCredits(text);
	c->patrons = std::move(patrons);
	c->stamp = ++g_stamp;
	return c;
}

int HitAt(const std::vector<Hit> &hits, POINT at, int &arg)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

// Changes of the screen happen here, under the lock; closing the game's screen is left for afterwards.
bool Act(State &st, int action, int arg)
{
	View &v = st.view;
	switch (action) {
	case kActJump:
		if (arg >= 0 && arg < static_cast<int>(st.out.entries.size())) v.scroll = st.out.entries[arg];
		return false;
	case kActPause: v.paused = !v.paused; return false;
	case kActClose: return true;
	default: return false;
	}
}

bool Handle(UINT msg, WPARAM wp, LPARAM lp, bool &close)
{
	std::lock_guard<std::mutex> lock(g_lock);
	State &st = g_s;
	View &v = st.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (st.dragging) v.scroll = st.dragScroll - (at.y - st.dragY) / static_cast<double>(st.scale);
		// The game moves its cursor with these.
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		st.mouseDown = true;
		int arg = 0;
		const int action = HitAt(st.hits, at, arg);
		if (action != kActNone) close = Act(st, action, arg);
		else if (PtInRect(&st.out.column, at)) {
			st.dragging = true;
			st.dragY = at.y;
			st.dragScroll = v.scroll;
		}
		return true;
	}
	case WM_LBUTTONUP:
		st.mouseDown = false;
		st.dragging = false;
		return true;
	case WM_MOUSEWHEEL:
		v.scroll -= GET_WHEEL_DELTA_WPARAM(wp) * kWheelStep / static_cast<double>(WHEEL_DELTA);
		return true;
	case WM_KEYDOWN:
		switch (wp) {
		case VK_ESCAPE: close = true; break;
		case VK_SPACE: v.paused = !v.paused; break;
		case VK_TAB: v.fast = !v.fast; break;
		case VK_UP: v.scroll -= kKeyStep; break;
		case VK_DOWN: v.scroll += kKeyStep; break;
		case VK_PRIOR: v.scroll -= st.out.viewport * 0.8; break;
		case VK_NEXT: v.scroll += st.out.viewport * 0.8; break;
		case VK_HOME: v.scroll = 0; break;
		case VK_END:
			if (!st.out.entries.empty()) v.scroll = st.out.entries.back();
			break;
		default: break;
		}
		return true;
	case WM_KEYUP:
	case WM_CHAR:
		return true;
	default:
		return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

void Close()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.closedAt = GetTickCount();
		g_s.dragging = false;
	}
	if (CreditsDialogClose()) return;
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.closedAt = 0;
}

// The game's picture stays at the sides, darker, behind the screen.
void Darken(uint32_t *buf, int w, int h)
{
	for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; i++) {
		const uint32_t c = buf[i];
		buf[i] = 0xff000000u | (((c >> 1) & 0x7f7f7f) - ((c >> 3) & 0x1f1f1f));
	}
}

}

void CreditsUiGameTick()
{
	void *dlg = CreditsDialogOpen();
	bool fresh = false, rollDue = false;
	size_t known = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!dlg) {
			if (g_s.dlg) g_s = State();
			return;
		}
		fresh = g_s.dlg != dlg;
		rollDue = !fresh && !g_s.closedAt && GetTickCount() - g_s.rollAt >= kRollEveryMs;
		known = g_s.patrons;
	}
	if (fresh) {
		PatronsRollRefresh();
		bool read = false;
		std::shared_ptr<Content> c = Build(RollPatrons(read));
		const int speed = CreditsDialogSpeed();
		LogI("ui", "credits: %d lines of the game's text in %d index lines, %d patrons", static_cast<int>(c->game.items.size()),
			static_cast<int>(c->game.index.size()), static_cast<int>(c->patrons.size()));
		std::lock_guard<std::mutex> lock(g_lock);
		g_s = State();
		g_s.dlg = dlg;
		g_s.speed = speed;
		g_s.patrons = c->patrons.size();
		g_s.view.content = c;
		g_s.rollAt = GetTickCount();
		return;
	}
	if (!rollDue) return;
	// The honour roll is read in the background: its names join the screen when they arrive.
	PatronsRollRefresh();
	bool read = false;
	std::vector<Patron> patrons = RollPatrons(read);
	std::shared_ptr<const Content> current;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.rollAt = GetTickCount();
		if (!read || patrons.size() == known) return;
		current = g_s.view.content;
	}
	auto c = std::make_shared<Content>(current ? *current : Content());
	c->patrons = std::move(patrons);
	c->stamp = ++g_stamp;
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.dlg != dlg) return;
	g_s.patrons = c->patrons.size();
	g_s.view.content = c;
}

bool CreditsUiNeedsDraw()
{
	void *dlg = CreditsDialogOpen();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!dlg || g_s.dlg != dlg) return false;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void CreditsUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || CreditsDialogOpen() != g_s.dlg) return;
		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		if (g_s.last.QuadPart && !g_s.view.paused && !g_s.dragging && !g_s.closedAt) {
			const double step = std::min(kMaxStep, std::max(0.0, static_cast<double>(now.QuadPart - g_s.last.QuadPart) / freq.QuadPart));
			g_s.view.scroll += g_s.speed * (g_s.view.fast ? 2 : 1) * step;
		}
		g_s.last = now;
		const double period = g_s.out.period;
		if (period > 0 && !g_s.dragging) {
			g_s.view.scroll = std::fmod(g_s.view.scroll, period);
			if (g_s.view.scroll < 0) g_s.view.scroll += period;
		}
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	Darken(buf, w, h);
	const float s = ScreenScale(w, h);
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(PanelWidth(w, h), kScreenH, 0, false, s)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOut out;
	PaintCreditsScreen(r.p, PanelWidth(w, h), view, out);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.out = out;
	g_s.scale = s;
}

bool CreditsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	void *dlg = CreditsDialogOpen();
	if (!dlg || ModUiIsOpen()) return false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg != dlg) return false;
		if (g_s.closedAt) return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	}
	bool close = false;
	const bool consumed = Handle(msg, wp, lp, close);
	if (close) Close();
	return consumed;
}
