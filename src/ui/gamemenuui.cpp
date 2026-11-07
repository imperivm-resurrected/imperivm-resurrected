// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/gamemenuui.h"

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

const int kNativeYes = 0x1002, kNativeNo = 0x1003, kNativeBack = 0x1007;

struct RowSpec {
	int id;
	const char *label;
	bool strong, startsGroup;
};

const RowSpec kRows[] = {
	{kNativeBack, "Back to game", true, false},
	{0x1001, "Load game", false, false},
	{0x1002, "Save game", false, false},
	{0x1003, "Options", false, false},
	{0x1004, "Restart", false, true},
	{0x1005, "Surrender", false, false},
	{0x1006, "Quit", false, false},
};

enum Screen { kScreenNone, kScreenMenu, kScreenConfirm };

struct State {
	Screen screen = kScreenNone;
	View view;
	std::vector<Hit> hits;
	bool mouseDown = false;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;
bool g_swallowUp = false;

// The mod's own screens and the room handle what the game asks behind them.
bool OtherScreenOwnsInput() { return ModUiIsOpen() || MpUiNeedsDraw(); }

Screen CurrentScreen(void *&dlg)
{
	dlg = nullptr;
	if (OtherScreenOwnsInput()) return kScreenNone;
	dlg = ConfirmOpen();
	if (dlg) return kScreenConfirm;
	if (OptionsDialogOpen() || SavesUiNeedsDraw()) return kScreenNone;
	dlg = GameMenuVisible();
	return dlg ? kScreenMenu : kScreenNone;
}

std::vector<Row> ReadRows(void *dlg)
{
	std::vector<Row> rows;
	for (const RowSpec &spec : kRows) {
		bool enabled = true;
		if (!NativeButtonEnabled(dlg, spec.id, enabled)) continue;
		Row row;
		row.id = spec.id;
		row.label = spec.label;
		row.enabled = enabled;
		row.strong = spec.strong;
		row.startsGroup = spec.startsGroup;
		rows.push_back(row);
	}
	return rows;
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

}

void GameMenuUiGameTick()
{
	void *dlg = nullptr;
	const Screen screen = CurrentScreen(dlg);
	std::vector<Row> rows;
	std::string question;
	if (screen == kScreenMenu) rows = ReadRows(dlg);
	else if (screen == kScreenConfirm) question = ConfirmText();
	std::lock_guard<std::mutex> lock(g_lock);
	if (screen != g_s.screen) {
		LogD("ui", "game menu screen: %d -> %d", static_cast<int>(g_s.screen), static_cast<int>(screen));
		g_s.hits.clear();
		g_s.mouseDown = false;
	}
	g_s.screen = screen;
	g_s.view.rows = std::move(rows);
	g_s.view.question = std::move(question);
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
	const bool confirm = screen == kScreenConfirm;
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(confirm ? kConfirmW : kMenuW, confirm ? kConfirmH : MenuHeight(view), 16)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	if (confirm) PaintConfirm(r.p, view);
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
		break;
	}
	case WM_KEYDOWN:
		if (lp & 0x40000000) break;
		if (wp == VK_ESCAPE) clickId = screen == kScreenConfirm ? kNativeNo : kNativeBack;
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
