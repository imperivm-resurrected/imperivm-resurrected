// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/diplomacyui.h"

#include <algorithm>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "game/diplomacy.h"
#include "game/diplomacyrules.h"
#include "ui/diplomacyui_view.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/widgets.h"

using namespace ui;
using namespace diplomacyui;

namespace {

const DWORD kReadEveryMs = 120;
const unsigned kBits[4] = {diplomacy::kCeasefire, diplomacy::kVision, diplomacy::kSupport, diplomacy::kControl};

struct State {
	void *dlg = nullptr;
	View view;
	std::vector<Hit> hits;
	bool mouseDown = false;
	DWORD lastRead = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;
bool g_swallowUp = false, g_escArmed = false, g_enterArmed = false;

bool OtherScreenOwnsInput() { return ModUiIsOpen() || MpUiNeedsDraw(); }

int HitAt(const State &st, POINT at, int &arg)
{
	for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		return it->action;
	}
	return kActNone;
}

void *OpenDialog()
{
	DiplomacyDialogState state;
	if (OtherScreenOwnsInput() || !DiplomacyDialogRead(state)) return nullptr;
	return state.dlg;
}

// What a click asks of the game's dialog; done outside the lock, since the dialog can close and call back.
struct Command {
	enum Kind { kNone, kSetMine, kSetAllied, kApply, kCancel } kind = kNone;
	int row = 0;
	unsigned flags = 0;
	bool on = false;
};

// The cards' scroll bar, under the lock: a press on it, the drag that follows and its end.
bool BarMouse(UINT msg, WPARAM wp, POINT at)
{
	ScrollTo to;
	const bool took = ScrollbarMouse(g_s.view.bar, g_s.hits, msg, wp, at, to);
	if (to.id == kBarCards) g_s.view.scroll = std::max(0, std::min(MaxScroll(g_s.view), to.pos));
	return took;
}

Command CommandFor(const View &v, int action, int arg)
{
	Command cmd;
	const int index = arg / kArgStride, sub = arg % kArgStride;
	const bool validCard = index >= 0 && index < static_cast<int>(v.cards.size());
	switch (action) {
	case kActPreset:
		if (!validCard || v.locked || sub >= diplomacy::kPresetCustom) break;
		cmd = {Command::kSetMine, index, diplomacy::PresetFlags(static_cast<diplomacy::Preset>(sub))};
		break;
	case kActToggle:
		if (!validCard || v.locked || sub >= 4) break;
		cmd = {Command::kSetMine, index, v.cards[index].mine ^ kBits[sub]};
		break;
	case kActAccept:
		if (arg < 0 || arg >= static_cast<int>(v.cards.size()) || v.locked) break;
		cmd = {Command::kSetMine, arg, v.cards[arg].mine | diplomacy::Offered(v.cards[arg].mine, v.cards[arg].theirs)};
		break;
	case kActAllied:
		if (!v.locked) cmd = {Command::kSetAllied, 0, 0, !v.alliedVictory};
		break;
	case kActApply:
		if (!v.locked) cmd.kind = Command::kApply;
		break;
	case kActCancel: cmd.kind = Command::kCancel; break;
	default: break;
	}
	return cmd;
}

void Run(void *dlg, const Command &cmd)
{
	switch (cmd.kind) {
	case Command::kSetMine: DiplomacyDialogSetMine(dlg, cmd.row, cmd.flags); break;
	case Command::kSetAllied: DiplomacyDialogSetAlliedVictory(dlg, cmd.on); break;
	case Command::kApply:
		LogI("ui", "diplomacy: applied from the mod's screen");
		DiplomacyDialogApply(dlg);
		break;
	case Command::kCancel: DiplomacyDialogCancel(dlg); break;
	default: return;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.lastRead = 0;
}

}

void DiplomacyUiGameTick()
{
	DiplomacyDialogState state;
	const bool open = !OtherScreenOwnsInput() && DiplomacyDialogRead(state);
	std::lock_guard<std::mutex> lock(g_lock);
	if (!open) {
		if (g_s.dlg) LogD("ui", "diplomacy screen closed");
		g_s.dlg = nullptr;
		g_s.hits.clear();
		return;
	}
	const bool changed = state.dlg != g_s.dlg;
	if (!changed && GetTickCount() - g_s.lastRead < kReadEveryMs) return;
	if (changed) {
		LogD("ui", "diplomacy screen opened with %d players", static_cast<int>(state.rows.size()));
		g_s.hits.clear();
		g_s.mouseDown = false;
		g_s.view.scroll = 0;
	}
	View fresh;
	fresh.mouseX = g_s.view.mouseX;
	fresh.mouseY = g_s.view.mouseY;
	fresh.scroll = g_s.view.scroll;
	fresh.bar = g_s.view.bar;
	fresh.alliedVictory = state.alliedVictory;
	fresh.locked = state.locked;
	for (const DiplomacyRow &row : state.rows) {
		Card c;
		c.name = row.name;
		c.rgb = row.rgb;
		c.ai = row.ai;
		c.team = row.team;
		c.mine = row.mine;
		c.theirs = row.theirs;
		fresh.cards.push_back(c);
	}
	fresh.scroll = std::max(0, std::min(fresh.scroll, MaxScroll(fresh)));
	g_s.dlg = state.dlg;
	g_s.view = std::move(fresh);
	g_s.lastRead = GetTickCount();
}

bool DiplomacyUiNeedsDraw()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.dlg != nullptr;
}

void DiplomacyUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	void *dlg = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		dlg = g_s.dlg;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	if (!dlg) return;
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kPanelW, PanelHeight(view), 16, true, FitScale(w, h, kPanelW, PanelHeight(view, true), 16))) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintDiplomacy(r.p, view);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.dlg == dlg) g_s.hits = std::move(r.hits);
}

bool DiplomacyUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		g_s.view.bar = ScrollDrag();
		return true;
	}
	void *dlg = OpenDialog();
	if (!dlg) {
		// The screen went away with the key still down: its release is not for the game.
		if (msg == WM_KEYUP && ((wp == VK_ESCAPE && g_escArmed) || (wp == VK_RETURN && g_enterArmed))) {
			g_escArmed = g_enterArmed = false;
			return true;
		}
		return false;
	}
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	Command cmd;
	bool consumed = true;
	switch (msg) {
	case WM_MOUSEMOVE: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.mouseX = at.x;
		g_s.view.mouseY = at.y;
		BarMouse(msg, wp, at);
		consumed = false;
		break;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.dlg != dlg) break;
		g_s.mouseDown = true;
		g_swallowUp = true;
		if (BarMouse(msg, wp, at)) break;
		int arg = 0;
		const int action = HitAt(g_s, at, arg);
		cmd = CommandFor(g_s.view, action, arg);
		break;
	}
	case WM_MOUSEWHEEL: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.scroll = std::max(0, std::min(MaxScroll(g_s.view), g_s.view.scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA));
		break;
	}
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) g_escArmed = true;
		else if (wp == VK_RETURN) g_enterArmed = true;
		else consumed = false;
		break;
	// On the release, as the game does: closing on the press would leave the release to the game, which opens its menu on it.
	case WM_KEYUP:
		if (wp == VK_ESCAPE && g_escArmed) {
			g_escArmed = false;
			cmd.kind = Command::kCancel;
		}
		else if (wp == VK_RETURN && g_enterArmed) {
			g_enterArmed = false;
			std::lock_guard<std::mutex> lock(g_lock);
			if (!g_s.view.locked) cmd.kind = Command::kApply;
		}
		else consumed = false;
		break;
	default:
		consumed = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
		break;
	}
	Run(dlg, cmd);
	return consumed;
}
