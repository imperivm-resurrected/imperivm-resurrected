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
#include "core/leagueteam.h"
#include "ui/fullscreen.h"
#include "ui/gamemenuui.h"
#include "ui/widgets.h"

using namespace ui;

namespace {

constexpr int kW = 640, kH = 284;
constexpr int kActClaim = 1, kActVoteYes = 2, kActVoteNo = 3;
// The 2v2's band at the top of the game: a line for each player who dropped, and the vote.
constexpr int kTeamW = 780, kTeamLineH = 40, kTeamVoteH = 112, kTeamPad = 10;
// The game menu's buttons the claim presses to leave the game: Quit, and Yes in the question that follows.
constexpr int kMenuQuit = 0x1006, kConfirmYes = 0x1002;
constexpr DWORD kLeaveGiveUpMs = 10000;

// Leaving the game after the claim, through the game's own menu, a step a frame; kLeaveManual when it did not close by itself.
enum Leave { kLeaveNone, kLeaveOpenMenu, kLeaveQuit, kLeaveConfirm, kLeaveClosing, kLeaveManual };

struct State {
	LeagueDropView view;
	LeagueTeamView team;
	std::vector<Hit> hits, teamHits;
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

bool TeamShown(const LeagueTeamView &t)
{
	return t.active && (!t.drops.empty() || t.vote != LeagueTeamVote::None);
}

int TeamHeight(const LeagueTeamView &t)
{
	const int drops = static_cast<int>(std::min<size_t>(t.drops.size(), 3));
	const int vote = t.vote == LeagueTeamVote::Asked ? kTeamVoteH : t.vote == LeagueTeamVote::Proposed ? kTeamLineH : 0;
	return 2 * kTeamPad + drops * kTeamLineH + vote;
}

// A line of the band: a mark of its colour and the text, smaller before it is cut.
void TeamLine(Painter &p, int y, const std::string &text, COLORREF mark)
{
	const int tw = kTeamW - 2 * kTeamPad - 36;
	p.Fill(kTeamPad + 8, y + kTeamLineH / 2 - 5, 10, 10, mark);
	p.Font(1);
	p.raw = true;
	if (p.TextWidth(text) > tw) p.Font(0);
	if (p.TextWidth(text) > tw) p.Font(3);
	p.Text(kTeamPad + 30, y, tw, kTeamLineH, text, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
}

void PaintTeam(Painter &p, const LeagueTeamView &t, int h, int mx, int my)
{
	PaintWindowFrame(p, kTeamW, h);
	p.Fill(0, 0, 3, h, pal::red);
	int y = kTeamPad;
	for (size_t i = 0; i < t.drops.size() && i < 3; i++) {
		const LeagueTeamDrop &d = t.drops[i];
		TeamLine(p, y, TrFmt("%s has dropped: the AI controls them. They have %s to come back.", {d.name, LeagueClockText(d.leftMs + 999)}),
			d.partner ? pal::warn : pal::caption);
		y += kTeamLineH;
	}
	const std::string left = LeagueClockText(t.voteLeftMs + 999);
	if (t.vote == LeagueTeamVote::Proposed) {
		TeamLine(p, y, TrFmt("You proposed to surrender: waiting for %s (%s)", {t.partner, left}), pal::red);
		return;
	}
	if (t.vote != LeagueTeamVote::Asked) return;
	// The partner's proposal: yes or no, with what is left of the 30 s.
	const int bw = 120, gap = 10, bx = kTeamW - kTeamPad - 8 - 2 * bw - gap, tw = bx - 30 - 16;
	// The title takes a smaller type before it is cut (Italian is the longest).
	SelectObject(p.dc, BrandFont(BrandFace::Display, 30, p.s));
	if (p.CapsWidth("Your partner proposes to surrender", 1) > tw) SelectObject(p.dc, BrandFont(BrandFace::Display, 24, p.s));
	p.TextCaps(30, y + 8, tw, 36, "Your partner proposes to surrender", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.Font(3);
	p.raw = true;
	p.TextWrapped(30, y + 46, tw, 40, TrFmt("%s left to answer: your team only surrenders if you say yes.", {left}), pal::textDim);
	p.raw = false;
	p.Button(bx, y + 22, bw, 44, "Yes", kActVoteYes, 0, mx, my, true, true);
	p.Button(bx + bw + gap, y + 22, bw, 44, "No", kActVoteNo, 0, mx, my, true, false);
	const float frac = static_cast<float>(t.voteLeftMs) / static_cast<float>(kLeagueVoteMs);
	PaintProgress(p, 30, y + kTeamVoteH - 16, kTeamW - 30 - kTeamPad - 8, 4, std::max(0.0f, std::min(1.0f, frac)));
}

// The band at the menus' scale, at the top of the screen under the game's own bar, without dimming it: the game goes on under it.
void TeamDraw(uint32_t *buf, int w, int h, const State &st)
{
	const int bandH = TeamHeight(st.team);
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kTeamW, bandH, 0, false, ScreenViewFor(w, h).s)) return;
	r.oy = r.p.oy = std::max(0, static_cast<int>(64 * r.s));
	r.Press(st.teamHits, st.mouseDown, {st.mouseX, st.mouseY});
	PaintTeam(r.p, st.team, bandH, st.mouseX, st.mouseY);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.teamHits = std::move(r.hits);
}

// The band's mouse: only the vote's buttons take it; the rest lets the click through to the game.
bool TeamHandleMessage(UINT msg, POINT at)
{
	int action = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (msg == WM_MOUSEMOVE) {
			g_s.mouseX = at.x;
			g_s.mouseY = at.y;
			return false;
		}
		if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONDBLCLK) return false;
		for (auto it = g_s.teamHits.rbegin(); it != g_s.teamHits.rend(); ++it) {
			if (PtInRect(&it->r, at)) action = it->action;
		}
		if (!action) return false;
		g_s.mouseDown = true;
		g_s.swallowUp = true;
	}
	if (action == kActVoteYes || action == kActVoteNo) LeagueVoteAnswer(action == kActVoteYes);
	return true;
}

}

void LeagueDropUiGameTick()
{
	const LeagueDropView v = LeagueDropState();
	const LeagueTeamView team = LeagueTeamState();
	Leave leave = kLeaveNone;
	DWORD since = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view = v;
		g_s.team = team;
		if (!TeamShown(team)) g_s.teamHits.clear();
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
		if (g_s.view.phase == LeagueDropPhase::None && !TeamShown(g_s.team)) return false;
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
	if (st.view.phase == LeagueDropPhase::None) {
		if (TeamShown(st.team)) TeamDraw(buf, w, h, st);
		return;
	}
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
	bool team = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		team = g_s.view.phase == LeagueDropPhase::None;
	}
	if (team) return TeamHandleMessage(msg, at);
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
