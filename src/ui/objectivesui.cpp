// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/objectivesui.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/objectivelog.h"
#include "game/objectives.h"
#include "ui/modui.h"
#include "ui/objectivesui_view.h"
#include "ui/widgets.h"

using namespace ui;
using namespace objectivesui;

namespace {

const DWORD kCloseTimeoutMs = 2000, kReadEveryMs = 250;
const int kWheelStep = 66, kKeyStep = 44;

struct State {
	void *dlg = nullptr;
	View view;
	std::vector<Hit> hits;
	PaintOut out;
	bool mouseDown = false, escArmed = false;
	// The game's dialog could not be closed from here: the screen gives way to it.
	bool aside = false;
	DWORD closedAt = 0, readAt = 0;
	uint32_t version = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;
// The click that closes the screen must not reach the map when the button comes up.
bool g_swallowUp = false;

std::string SelectedId(const State &st)
{
	const View &v = st.view;
	return v.selected >= 0 && v.selected < static_cast<int>(v.entries.size()) ? v.entries[v.selected].note.id : std::string();
}

bool CanShowOnMap(const View &v)
{
	return v.selected >= 0 && v.selected < static_cast<int>(v.entries.size()) && !v.entries[v.selected].done && v.entries[v.selected].note.onMap;
}

struct After {
	bool close = false;
	std::string showOnMap;
	// The note the player leaves: read now, it loses its "new" tag.
	std::string seen;
};

void Select(State &st, int index, After &after)
{
	View &v = st.view;
	if (index < 0 || index >= static_cast<int>(v.entries.size()) || index == v.selected) return;
	after.seen = SelectedId(st);
	v.selected = index;
	v.textScroll = 0;
	v.revealSelected = true;
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

void TextScrollBy(State &st, int delta)
{
	st.view.textScroll = std::max(0, std::min(std::max(0, st.out.textH - st.out.textViewH), st.view.textScroll + delta));
}

void ListScrollBy(State &st, int delta)
{
	st.view.listScroll = std::max(0, std::min(std::max(0, st.out.listH - st.out.listViewH), st.view.listScroll + delta));
}

void Click(State &st, int action, int arg, After &after)
{
	switch (action) {
	case kActSelect: Select(st, arg, after); break;
	case kActShowOnMap:
		if (CanShowOnMap(st.view)) after.showOnMap = SelectedId(st);
		break;
	case kActClose: after.close = true; break;
	default: break;
	}
}

bool Key(State &st, WPARAM wp, After &after)
{
	View &v = st.view;
	switch (wp) {
	case VK_ESCAPE: st.escArmed = true; break;
	case VK_UP: Select(st, std::max(0, v.selected - 1), after); break;
	case VK_DOWN: Select(st, std::min(static_cast<int>(v.entries.size()) - 1, v.selected + 1), after); break;
	case VK_PRIOR: TextScrollBy(st, -st.out.textViewH * 4 / 5); break;
	case VK_NEXT:
	case VK_SPACE: TextScrollBy(st, st.out.textViewH * 4 / 5); break;
	case VK_HOME: v.textScroll = 0; break;
	case VK_END: TextScrollBy(st, st.out.textH); break;
	default: break;
	}
	return true;
}

bool Handle(UINT msg, WPARAM wp, LPARAM lp, After &after)
{
	std::lock_guard<std::mutex> lock(g_lock);
	State &st = g_s;
	View &v = st.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		// The game moves its cursor with these.
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		st.mouseDown = true;
		g_swallowUp = true;
		v.mouseX = at.x;
		v.mouseY = at.y;
		int arg = 0;
		const int action = HitAt(st.hits, at, arg);
		if (action != kActNone) Click(st, action, arg, after);
		return true;
	}
	case WM_LBUTTONUP:
		st.mouseDown = false;
		return true;
	case WM_MOUSEWHEEL: {
		// The wheel's point is on the screen: the last position of the mouse in the frame is the one that counts.
		const POINT mouse = {v.mouseX, v.mouseY};
		const int delta = -GET_WHEEL_DELTA_WPARAM(wp) * kWheelStep / WHEEL_DELTA;
		if (PtInRect(&st.out.list, mouse)) ListScrollBy(st, delta);
		else TextScrollBy(st, delta);
		return true;
	}
	case WM_KEYDOWN: return Key(st, wp, after);
	case WM_KEYUP:
		// On the release, as the game does: closing on the press would leave it the release, and it would open its menu.
		if (wp == VK_ESCAPE && st.escArmed) {
			st.escArmed = false;
			after.close = true;
		}
		return true;
	default:
		return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	}
}

void Close(void *dlg)
{
	std::string seen;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.closedAt = GetTickCount();
		seen = SelectedId(g_s);
	}
	ObjectivesMarkSeen(seen);
	if (NotesDialogClose(dlg)) return;
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.closedAt = 0;
	g_s.aside = true;
}

// The notes and the log, read again when either may have changed: the scripts keep running in a game of several players.
void Refresh(void *dlg, bool fresh)
{
	const uint32_t version = ObjectivesLogVersion();
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!fresh && g_s.version == version && GetTickCount() - g_s.readAt < kReadEveryMs) return;
	}
	const std::vector<ObjectiveEntry> entries = ObjectiveEntries(ObjectivesActive(), ObjectivesLog());
	std::lock_guard<std::mutex> lock(g_lock);
	if (fresh) {
		g_s = State();
		g_s.dlg = dlg;
		g_s.view.entries = entries;
		g_s.view.selected = ObjectiveFirstSelection(entries);
		g_s.view.revealSelected = true;
		LogI("ui", "the objectives screen opens with %d notes in progress and %d completed", ObjectiveCountInProgress(entries),
			static_cast<int>(entries.size()) - ObjectiveCountInProgress(entries));
	}
	else if (g_s.dlg == dlg) {
		View &v = g_s.view;
		const std::string id = SelectedId(g_s);
		v.entries = entries;
		v.selected = ObjectiveFollow(entries, id, v.selected);
		if (SelectedId(g_s) != id) {
			v.textScroll = 0;
			v.revealSelected = true;
		}
	}
	g_s.version = version;
	g_s.readAt = GetTickCount();
}

}

void ObjectivesUiGameTick()
{
	void *dlg = NotesDialogOpen();
	bool fresh = false;
	std::string seen;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!dlg && g_s.dlg) {
			seen = SelectedId(g_s);
			g_s = State();
		}
		fresh = dlg && g_s.dlg != dlg;
	}
	if (!seen.empty()) ObjectivesMarkSeen(seen);
	if (dlg) Refresh(dlg, fresh);
}

// On the drawing thread: the game tick says whether the game's dialog is open, which is not read from here.
bool ObjectivesUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg || g_s.aside) return false;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void ObjectivesUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || g_s.aside) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kScreenW, kScreenH, 40)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOut out;
	PaintObjectivesScreen(r.p, view, out);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	if (view.revealSelected && g_s.view.revealSelected && g_s.view.selected == view.selected) {
		g_s.view.revealSelected = false;
		g_s.view.listScroll = out.listScroll;
	}
	g_s.out = out;
}

bool ObjectivesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		return true;
	}
	void *dlg = NotesDialogOpen();
	if (!dlg || ModUiIsOpen()) return false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg != dlg || g_s.aside) return false;
		if (g_s.closedAt) return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP);
	}
	After after;
	const bool consumed = Handle(msg, wp, lp, after);
	if (!after.seen.empty()) ObjectivesMarkSeen(after.seen);
	if (!after.showOnMap.empty()) {
		// The game centres the view behind its dialog; the screen closes so that the player sees the place.
		NotesDialogShowOnMap(dlg, after.showOnMap);
		after.close = true;
	}
	if (after.close) Close(dlg);
	return consumed;
}
