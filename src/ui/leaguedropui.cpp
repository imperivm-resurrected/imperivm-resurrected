// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguedropui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "core/leagueranked.h"
#include "core/log.h"
#include "game/gamemenu.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "ui/fullscreen.h"
#include "ui/gamemenuui.h"
#include "ui/widgets.h"

using namespace ui;

namespace {

constexpr int kW = 640, kH = 284;
constexpr int kActClaim = 1;
// The game menu's buttons the claim presses to leave the game: Quit, and Yes in the question that follows.
constexpr int kMenuQuit = 0x1006, kConfirmYes = 0x1002;
constexpr DWORD kLeaveGiveUpMs = 10000;

// Leaving the game after the claim, through the game's own menu, a step a frame; kLeaveManual when it did not close by itself.
enum Leave { kLeaveNone, kLeaveOpenMenu, kLeaveQuit, kLeaveConfirm, kLeaveClosing, kLeaveManual };

struct State {
	LeagueDropView view;
	std::vector<Hit> hits;
	int mouseX = -1, mouseY = -1;
	bool mouseDown = false, swallowUp = false;
	Leave leave = kLeaveNone;
	DWORD leaveAt = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

bool InGame()
{
	return *reinterpret_cast<void **>(addr::kGame) != nullptr;
}

// On the game thread.
void LeaveStep(Leave &step, DWORD since)
{
	if (step == kLeaveNone || step == kLeaveManual) return;
	if (GetTickCount() - since > kLeaveGiveUpMs) {
		LogW("league", "ranked: the game could not be left by itself after the claim; the player leaves it from the menu");
		step = kLeaveManual;
		return;
	}
	if (step == kLeaveClosing) return;
	if (void *confirm = ConfirmOpen()) {
		if (step == kLeaveConfirm) {
			NativeButtonClick(confirm, kConfirmYes);
			step = kLeaveClosing;
		}
		return;
	}
	if (void *menu = GameMenuVisible()) {
		if (step == kLeaveQuit) {
			NativeButtonClick(menu, kMenuQuit);
			step = kLeaveConfirm;
		}
		return;
	}
	if (step == kLeaveOpenMenu) {
		ToggleGameMenu();
		step = kLeaveQuit;
	}
}

void Claim()
{
	if (!LeagueDropClaimWin()) return;
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.leave = kLeaveOpenMenu;
	g_s.leaveAt = GetTickCount();
}

void PaintDrop(Painter &p, const LeagueDropView &v, Leave leave, int mx, int my)
{
	PaintWindowCard(p, kW, kH);
	constexpr int pad = 36;
	p.Font(4);
	p.TextCaps(pad, 26, kW - 2 * pad, 20, "Ranked match paused", pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	const std::string name = v.opponent.empty() ? Tr("Your opponent") : v.opponent;
	std::string title, line;
	switch (v.phase) {
	case LeagueDropPhase::Waiting:
		title = v.returning ? TrFmt("%s is coming back into the game...", {name}) : TrFmt("Waiting for %s: %s to come back", {name, LeagueClockText(v.leftMs + 999)});
		line = v.returning ? Tr("The game goes on as soon as they are in.") : Tr("The game stays paused. If they don't come back in time, you can claim the win.");
		break;
	case LeagueDropPhase::Claimable:
		title = TrFmt("%s did not come back", {name});
		line = Tr("Claim the win to end the game: the league counts it as yours.");
		break;
	default:
		title = Tr("Victory claimed");
		line = leave == kLeaveManual ? Tr("Leave the game from its menu (Esc) to finish.") : Tr("Leaving the game...");
		break;
	}
	SelectObject(p.dc, BrandFont(BrandFace::Display, 38, p.s));
	p.raw = true;
	p.Text(pad, 56, kW - 2 * pad, 48, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	p.Font(0);
	p.TextWrapped(pad, 110, kW - 2 * pad, 48, line, pal::textDim);
	const int by = kH - 36 - 52;
	if (v.phase == LeagueDropPhase::Waiting && !v.returning) {
		const float left = static_cast<float>(v.leftMs) / static_cast<float>(kLeagueDropWaitMs);
		PaintProgress(p, pad, by + 22, kW - 2 * pad, 8, 1.0f - std::max(0.0f, std::min(1.0f, left)));
	}
	else if (v.phase == LeagueDropPhase::Waiting) {
		PaintBusyBar(p, pad, by + 22, kW - 2 * pad, 8, GetTickCount());
	}
	else if (v.phase == LeagueDropPhase::Claimable) {
		p.Button(pad, by, kW - 2 * pad, 52, "Claim the win", kActClaim, 0, mx, my, true, true);
	}
	else if (leave != kLeaveManual) {
		p.Spinner(kW / 2, by + 26, 12, 4, GetTickCount());
	}
}

}

void LeagueDropUiGameTick()
{
	const LeagueDropView v = LeagueDropState();
	Leave leave = kLeaveNone;
	DWORD since = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view = v;
		if (!InGame() || (v.phase != LeagueDropPhase::Claimed && g_s.leave != kLeaveNone)) g_s.leave = kLeaveNone;
		if (v.phase == LeagueDropPhase::None) {
			g_s.hits.clear();
			g_s.mouseDown = false;
		}
		leave = g_s.leave;
		since = g_s.leaveAt;
	}
	if (leave == kLeaveNone || leave == kLeaveManual) return;
	// The game's own dialogs may call back into the mod: no lock while they are pressed.
	LeaveStep(leave, since);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.leave != kLeaveNone) g_s.leave = leave;
}

bool LeagueDropUiNeedsDraw()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.view.phase == LeagueDropPhase::None) return false;
	}
	return InGame() && !GameMenuUiNeedsDraw();
}

void LeagueDropUiDraw(uint32_t *buf, int w, int h)
{
	State st;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		st = g_s;
	}
	if (st.view.phase == LeagueDropPhase::None) return;
	PanelRender r(g_canvas, buf, w, h);
	if (!BeginWindow(r, kW, kH, 16)) return;
	r.Press(st.hits, st.mouseDown, {st.mouseX, st.mouseY});
	PaintDrop(r.p, st.view, st.leave, st.mouseX, st.mouseY);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
}

bool LeagueDropUiHandleMessage(UINT msg, WPARAM, LPARAM lp)
{
	if (msg == WM_LBUTTONUP) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		if (g_s.swallowUp) {
			g_s.swallowUp = false;
			return true;
		}
	}
	if (!LeagueDropUiNeedsDraw()) return false;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	bool claim = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		switch (msg) {
		case WM_MOUSEMOVE:
			g_s.mouseX = at.x;
			g_s.mouseY = at.y;
			return false;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			g_s.mouseDown = true;
			g_s.swallowUp = true;
			for (auto it = g_s.hits.rbegin(); it != g_s.hits.rend(); ++it) {
				if (PtInRect(&it->r, at) && it->action == kActClaim) claim = true;
			}
			break;
		default:
			// The keys stay the game's, so Esc opens its menu; the mouse is the window's while it is up.
			if (msg < WM_MOUSEFIRST || msg > WM_MOUSELAST) return false;
			break;
		}
	}
	if (claim) Claim();
	return true;
}
