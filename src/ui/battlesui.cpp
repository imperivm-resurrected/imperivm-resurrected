// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/battlesui.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "core/log.h"
#include "game/greatbattles.h"
#include "ui/battlesui_view.h"
#include "ui/modui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace battlesui;

namespace {

const DWORD kReadEveryMs = 100;
const int kWheelLines = 3;

struct State {
	View view;
	std::vector<Hit> hits;
	PaintOut geom;
	ListState listDrag, descDrag;
	bool mouseDown = false;
	DWORD lastRead = 0;
	bool active = false;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

void Refresh()
{
	BattlesSnapshot snap = BattlesRead();
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	if (!g_s.active) LogD("ui", "the new adventure screen opens");
	if (snap.tab != v.snap.tab) v.listScroll = 0;
	if (snap.tab != v.snap.tab || snap.selList != v.snap.selList || snap.selIndex != v.snap.selIndex) v.descScroll = 0;
	v.snap = std::move(snap);
	g_s.active = true;
	g_s.lastRead = GetTickCount();
}

struct Command {
	int action = kActNone;
	int arg = 0;
	bool start = false;
};

void Execute(const Command &cmd)
{
	switch (cmd.action) {
	case kActTab: BattlesSetTab(cmd.arg); break;
	case kActRow: {
		const int list = cmd.arg >> 16, index = cmd.arg & 0xffff;
		bool same = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			same = g_s.view.snap.selList == list && g_s.view.snap.selIndex == index;
		}
		if (!same) BattlesSelect(list, index);
		if (cmd.start) BattlesStart();
		break;
	}
	case kActDifficulty: BattlesSetDifficulty(cmd.arg); break;
	case kActStart: BattlesStart(); break;
	case kActIntro: BattlesPlayIntro(); break;
	case kActBack: BattlesBack(); break;
	default: return;
	}
	Refresh();
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

// Up and down walk the rows of the tab as they are drawn, through both groups of the great battles.
void MoveSelection(View &v, int step, Command &cmd)
{
	const BattlesSnapshot &s = v.snap;
	std::vector<std::pair<int, int>> order;
	for (int list : s.tab == kBattlesTabGreat ? std::vector<int>{kBattlesVictories, kBattlesDefeats} : std::vector<int>{kBattlesOthers}) {
		for (size_t i = 0; i < s.lists[list].size(); i++) {
			if (BattlesInTab(s.tab, list, s.lists[list][i].path)) order.push_back({list, static_cast<int>(i)});
		}
	}
	if (order.empty()) return;
	int at = -1;
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i].first == s.selList && order[i].second == s.selIndex) at = static_cast<int>(i);
	}
	const int next = at < 0 ? 0 : std::max(0, std::min(static_cast<int>(order.size()) - 1, at + step));
	if (next == at) return;
	cmd.action = kActRow;
	cmd.arg = RowArg(order[next].first, order[next].second);
	const int top = RowTop(s, order[next].first, order[next].second);
	const int visible = g_s.geom.list.visible;
	if (top >= 0 && visible > 0) {
		if (top - 30 < v.listScroll) v.listScroll = std::max(0, top - (next == 0 || order[next].second == 0 ? 44 : 8));
		else if (top + kListRowH > v.listScroll + visible) v.listScroll = top + kListRowH - visible + 8;
	}
}

// Everything that touches the game runs without the lock: its screen can call back into the mod.
bool Handle(UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (ListMove(g_s.listDrag, g_s.geom.list, at)) v.listScroll = g_s.listDrag.scroll;
		if (ListMove(g_s.descDrag, g_s.geom.desc, at)) v.descScroll = g_s.descDrag.scroll;
		// The game moves its cursor with these.
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseDown = true;
		g_s.listDrag.scroll = v.listScroll;
		g_s.descDrag.scroll = v.descScroll;
		if (ListDown(g_s.listDrag, g_s.geom.list, at)) {
			v.listScroll = g_s.listDrag.scroll;
			return true;
		}
		if (ListDown(g_s.descDrag, g_s.geom.desc, at)) {
			v.descScroll = g_s.descDrag.scroll;
			return true;
		}
		int arg = 0;
		cmd.action = HitAt(g_s.hits, at, arg);
		cmd.arg = arg;
		cmd.start = msg == WM_LBUTTONDBLCLK && cmd.action == kActRow;
		return true;
	}
	case WM_LBUTTONUP:
		g_s.mouseDown = false;
		ListUp(g_s.listDrag);
		ListUp(g_s.descDrag);
		return true;
	case WM_MOUSEWHEEL: {
		// The wheel's point is on the screen: the last position of the mouse in the frame is the one that counts.
		const POINT mouse = {v.mouseX, v.mouseY};
		const int delta = GET_WHEEL_DELTA_WPARAM(wp);
		ListState st;
		st.scroll = v.descScroll;
		if (ListWheel(st, g_s.geom.desc, mouse, delta, kWheelLines)) v.descScroll = st.scroll;
		st.scroll = v.listScroll;
		if (ListWheel(st, g_s.geom.list, mouse, delta, kListRowH)) v.listScroll = st.scroll;
		return true;
	}
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) cmd.action = kActBack;
		else if (wp == VK_RETURN) cmd.action = kActStart;
		else if (wp == VK_UP || wp == VK_DOWN) MoveSelection(v, wp == VK_UP ? -1 : 1, cmd);
		return true;
	case WM_KEYUP:
	case WM_CHAR:
		return true;
	default:
		return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

}

void BattlesUiGameTick()
{
	static bool told = false;
	if (!told) {
		told = true;
		BattlesOnDescription([] {
			if (BattlesScreenWanted() && !BattlesIntroPlaying()) Refresh();
		});
	}
	// The screen comes back as it was after the intro.
	if (BattlesIntroPlaying()) return;
	if (!BattlesScreenWanted()) {
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.active) g_s = State();
		return;
	}
	bool due = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		due = !g_s.active || GetTickCount() - g_s.lastRead >= kReadEveryMs;
	}
	if (due) Refresh();
}

bool BattlesUiNeedsDraw()
{
	if (!BattlesScreenWanted()) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.active;
}

void BattlesUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.active) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	const float s = ScreenScale(w, h);
	const int screenW = ScreenWidth(w, h);
	const uint32_t back = 0xff000000u | GetRValue(pal::panel) << 16 | GetGValue(pal::panel) << 8 | GetBValue(pal::panel);
	std::fill(buf, buf + static_cast<size_t>(w) * h, back);
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(screenW, kScreenH, 0, false, s)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOut out;
	PaintBattlesScreen(r.p, screenW, view, out);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.geom = out;
}

bool BattlesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (!BattlesScreenWanted() || BattlesIntroPlaying() || ModUiIsOpen()) return false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.active) return false;
	}
	if (!BattlesDialogOpen()) return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	Command cmd;
	const bool consumed = Handle(msg, wp, lp, cmd);
	if (cmd.action != kActNone) Execute(cmd);
	return consumed;
}
