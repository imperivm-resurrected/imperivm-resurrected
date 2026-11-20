// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguedropui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "game/gamemenu.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "core/leagueteam.h"
#include "ui/fullscreen.h"
#include "ui/gamemenuui.h"
#include "ui/widgets.h"

using namespace ui;

namespace {

constexpr int kActVoteYes = 2, kActVoteNo = 3;
// The band at the top of the game: a line for each player who dropped, and a ranked 2v2's vote.
constexpr int kTeamW = 780, kTeamLineH = 40, kTeamVoteH = 112, kTeamPad = 10;
struct State {
	LeagueTeamView team;
	// The vote as the last tick saw it: one that opens closes the game's menu, which would hide it.
	LeagueTeamVote vote = LeagueTeamVote::None;
	std::vector<Hit> teamHits;
	int mouseX = -1, mouseY = -1;
	bool mouseDown = false, swallowUp = false;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

bool InGame()
{
	return *reinterpret_cast<void **>(addr::kGame) != nullptr;
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
	const LeagueTeamView team = LeagueTeamState();
	bool voteOpened = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.team = team;
		if (!TeamShown(team)) {
			g_s.teamHits.clear();
			g_s.mouseDown = false;
		}
		voteOpened = team.vote != LeagueTeamVote::None && g_s.vote == LeagueTeamVote::None;
		g_s.vote = team.vote;
	}
	// A vote opens with the menu up (the proposer pressed Surrender in it, or the partner was in it): the menu goes, so its band shows.
	if (voteOpened && InGame() && GameMenuVisible()) ToggleGameMenu();
}

bool LeagueDropUiNeedsDraw()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!TeamShown(g_s.team)) return false;
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
	if (TeamShown(st.team)) TeamDraw(buf, w, h, st);
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
	return TeamHandleMessage(msg, at);
}
