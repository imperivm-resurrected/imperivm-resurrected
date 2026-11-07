// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/gamemenuui.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/gamemenu.h"
#include "game/options.h"
#include "ui/gamemenuui_view.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/savesui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace gamemenuui;

namespace {

const int kNativeYes = 0x1002, kNativeNo = 0x1003, kNativeBack = 0x1007, kNativeQuit = 0x1006, kNativeQuitReplay = 0x100b;
const int kTitleId = 0x1007f, kEndMessageId = 0x1009, kRatingIds[] = {0x100c, 0x100d};
const int kStatsFirstPage = 4, kStatsLastPage = 7, kStatsTimeId = 9, kStatsHeadIds[kStatsColumns] = {1, 2, 3};
const DWORD kReadEveryMs = 120;

struct RowSpec {
	int id;
	const char *label;
	bool strong, startsGroup;
};

const RowSpec kMenuRows[] = {
	{kNativeBack, "Back to game", true, false},
	{0x1001, "Load game", false, false},
	{0x1002, "Save game", false, false},
	{0x1003, "Options", false, false},
	{0x1004, "Restart", false, true},
	{0x1005, "Surrender", false, false},
	{kNativeQuit, "Quit", false, false},
};

const RowSpec kEndRows[] = {
	{kNativeBack, "Continue", true, false},
	{0x1005, "Statistics", false, false},
	{0x1001, "Load game", false, false},
	{0x1002, "Save game", false, false},
	{0x1003, "Options", false, false},
	{0x1004, "Restart", false, true},
	{kNativeQuit, "Quit", false, false},
	{kNativeQuitReplay, "Quit", false, false},
};

enum Screen { kScreenNone, kScreenMenu, kScreenEnd, kScreenStats, kScreenConfirm };

struct State {
	Screen screen = kScreenNone;
	void *dlg = nullptr;
	View view;
	std::vector<Hit> hits;
	bool mouseDown = false;
	DWORD lastRead = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;
bool g_swallowUp = false;

// The mod's own screens and the room handle what the game asks behind them.
bool OtherScreenOwnsInput() { return ModUiIsOpen() || MpUiNeedsDraw(); }

template <size_t N>
std::vector<Row> ReadRows(void *dlg, const RowSpec (&specs)[N], bool onlyVisible)
{
	std::vector<Row> rows;
	for (const RowSpec &spec : specs) {
		const NativeButtonState state = NativeButton(dlg, spec.id);
		if (!state.exists || (onlyVisible && !state.visible)) continue;
		Row row;
		row.id = spec.id;
		row.label = spec.label;
		row.enabled = state.enabled;
		row.strong = spec.strong;
		row.startsGroup = spec.startsGroup;
		rows.push_back(row);
	}
	return rows;
}

// A screen with no buttons would leave the player with nothing to press: the game's own takes over.
Screen CurrentScreen(void *&dlg)
{
	dlg = nullptr;
	if (OtherScreenOwnsInput()) return kScreenNone;
	dlg = ConfirmOpen();
	if (dlg) return kScreenConfirm;
	if (OptionsDialogOpen() || SavesUiNeedsDraw()) return kScreenNone;
	dlg = StatisticsOpen();
	if (dlg) return NativeButton(dlg, kStatsClose).exists ? kScreenStats : kScreenNone;
	dlg = EndMenuVisible();
	if (dlg) return ReadRows(dlg, kEndRows, true).empty() ? kScreenNone : kScreenEnd;
	dlg = GameMenuVisible();
	if (dlg) return ReadRows(dlg, kMenuRows, false).empty() ? kScreenNone : kScreenMenu;
	return kScreenNone;
}

// The game writes the percent as "(45%)" in the cell three places after the value.
int ParsePercent(const std::string &text)
{
	const size_t open = text.find('(');
	return open == std::string::npos ? 0 : std::atoi(text.c_str() + open + 1);
}

void ReadStatistics(void *dlg, View &v)
{
	v.statsTime = NativeText(dlg, kStatsTimeId);
	for (int c = 0; c < kStatsColumns; c++) v.statsColumns[c] = NativeText(dlg, kStatsHeadIds[c]);
	const int count = std::max(0, std::min(8, StatisticsPlayerCount(dlg)));
	for (int row = 0; row < count; row++) {
		const int base = 0x400 + row * 0x10;
		StatsPlayer player;
		player.name = NativeText(dlg, base + 1);
		const unsigned color = StatisticsPlayerColor(dlg, row);
		player.color = RGB(color >> 16 & 255, color >> 8 & 255, color & 255);
		for (int c = 0; c < kStatsColumns; c++) {
			player.value[c] = NativeText(dlg, base + 2 + c);
			player.percent[c] = ParsePercent(NativeText(dlg, base + 5 + c));
		}
		v.statsPlayers.push_back(player);
	}
}

void ReadEndMenu(void *dlg, View &v)
{
	v.title = NativeText(dlg, kTitleId);
	if (v.title.empty()) v.title = "MENU";
	v.titleSpacing = 1;
	v.rows = ReadRows(dlg, kEndRows, true);
	const std::string message = NativeText(dlg, kEndMessageId);
	if (!message.empty()) v.notes.push_back(message);
	for (int id : kRatingIds) {
		const std::string rating = NativeText(dlg, id);
		if (!rating.empty()) v.notes.push_back(rating);
	}
}

int HitAt(const State &st, POINT at, int &arg)
{
	for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

int EscapeButton(Screen screen, void *dlg)
{
	switch (screen) {
	case kScreenConfirm: return kNativeNo;
	case kScreenStats: return kStatsClose;
	case kScreenEnd: return NativeButton(dlg, kNativeBack).visible ? kNativeBack : 0;
	default: return kNativeBack;
	}
}

}

void GameMenuUiGameTick()
{
	void *dlg = nullptr;
	const Screen screen = CurrentScreen(dlg);
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const bool changed = screen != g_s.screen || dlg != g_s.dlg;
		if (!changed && GetTickCount() - g_s.lastRead < kReadEveryMs) return;
		if (changed) {
			LogD("ui", "game menu screen: %d -> %d", static_cast<int>(g_s.screen), static_cast<int>(screen));
			g_s.hits.clear();
			g_s.mouseDown = false;
			g_s.view.statsPage = kStatsFirstPage;
		}
	}
	View fresh;
	if (screen == kScreenMenu) fresh.rows = ReadRows(dlg, kMenuRows, false);
	else if (screen == kScreenEnd) ReadEndMenu(dlg, fresh);
	else if (screen == kScreenConfirm) fresh.question = ConfirmText();
	else if (screen == kScreenStats) ReadStatistics(dlg, fresh);
	std::lock_guard<std::mutex> lock(g_lock);
	fresh.mouseX = g_s.view.mouseX;
	fresh.mouseY = g_s.view.mouseY;
	fresh.statsPage = g_s.view.statsPage;
	g_s.screen = screen;
	g_s.dlg = dlg;
	g_s.view = std::move(fresh);
	g_s.lastRead = GetTickCount();
}

bool GameMenuUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.screen != kScreenNone;
}

void GameMenuUiDraw(uint32_t *buf, int w, int h)
{
	Screen screen = kScreenNone;
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		screen = g_s.screen;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	if (screen == kScreenNone) return;
	const int designW = screen == kScreenConfirm ? kConfirmW : screen == kScreenStats ? kStatsW : kMenuW;
	const int designH = screen == kScreenConfirm ? kConfirmH : screen == kScreenStats ? kStatsH : MenuHeight(view);
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(designW, designH, 16)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	if (screen == kScreenConfirm) PaintConfirm(r.p, view);
	else if (screen == kScreenStats) PaintStatistics(r.p, view);
	else PaintMenu(r.p, view);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.screen == screen) g_s.hits = std::move(r.hits);
}

bool GameMenuUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	// The click that closes a screen must not reach the map when the button comes up.
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		return true;
	}
	void *dlg = nullptr;
	const Screen screen = CurrentScreen(dlg);
	if (screen == kScreenNone) return false;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	int clickId = 0;
	bool consumed = true;
	switch (msg) {
	case WM_MOUSEMOVE: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.mouseX = at.x;
		g_s.view.mouseY = at.y;
		consumed = false;
		break;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.screen != screen) break;
		g_s.mouseDown = true;
		g_swallowUp = true;
		int arg = 0;
		const int action = HitAt(g_s, at, arg);
		if (action == kActButton) clickId = arg;
		else if (action == kActYes) clickId = kNativeYes;
		else if (action == kActNo) clickId = kNativeNo;
		if (screen == kScreenStats && clickId >= kStatsFirstPage && clickId <= kStatsLastPage) {
			g_s.view.statsPage = clickId;
			g_s.lastRead = 0;
		}
		break;
	}
	case WM_KEYDOWN:
		if (lp & 0x40000000) break;
		if (wp == VK_ESCAPE) clickId = EscapeButton(screen, dlg);
		else if (wp == VK_RETURN && screen == kScreenConfirm) clickId = kNativeYes;
		break;
	case WM_KEYUP:
	case WM_CHAR:
		break;
	default:
		consumed = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
		break;
	}
	// The game's dialog can close itself and call back into the mod: no lock here.
	if (clickId) NativeButtonClick(dlg, clickId);
	return consumed;
}
