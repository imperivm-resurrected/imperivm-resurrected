// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguesearchui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
#include "core/leagueranked.h"
#include "game/loadcurtain.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "ui/leagueui.h"
#include "ui/mpui.h"
#include "ui/screenscale.h"
#include "ui/widgets.h"

using namespace ui;

namespace {

constexpr int kActOpen = 1, kActCancel = 2, kActAccept = 3, kActDecline = 4, kActHide = 5;
constexpr int kPillW = 420, kPillH = 44, kBoxW = 620, kBoxH = 330;

struct State {
	LeagueRankedPhase phase = LeagueRankedPhase::Idle;
	std::string queue = "1v1";
	bool partnerSearch = false, accepted = false, answering = false;
	int64_t since = 0, acceptBy = 0, matchId = 0;
	// The window of a match found that the player closed (Ready: they go back to the main menu on their own).
	int64_t hiddenMatch = 0;
	std::vector<Hit> hits;
	int mouseX = -1, mouseY = -1;
	bool mouseDown = false, swallowUp = false;
	DWORD readAt = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

bool InGame()
{
	return *reinterpret_cast<void **>(addr::kGame) != nullptr;
}

bool Searching(const State &s)
{
	return s.phase == LeagueRankedPhase::Joining || s.phase == LeagueRankedPhase::Searching;
}

bool Matched(const State &s)
{
	return s.phase == LeagueRankedPhase::Found || s.phase == LeagueRankedPhase::Accepted || s.phase == LeagueRankedPhase::Ready;
}

bool Shown(const State &s)
{
	return Searching(s) || (Matched(s) && s.matchId != s.hiddenMatch);
}

// The pill at the top centre: spinner, what is searched, the time, and the cross that ends it (for both of a duo).
void PaintPill(Painter &p, const State &st, int x, int y)
{
	const int mx = st.mouseX, my = st.mouseY;
	p.Fill(x, y, kPillW, kPillH, pal::selected);
	p.Frame(x, y, kPillW, kPillH, pal::edge);
	p.Fill(x, y, 3, kPillH, pal::red);
	p.Spinner(x + 26, y + kPillH / 2, 9, 4, GetTickCount());
	const bool cross = true;
	const int textW = kPillW - 46 - (cross ? 44 : 12) - 86;
	p.Font(4);
	p.TextCaps(x + 46, y, textW, kPillH, st.partnerSearch ? "Your partner is searching" : LeagueQueueIsTeam(st.queue) ? "Searching 2 vs 2" : "Searching 1 vs 1",
		pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	SelectObject(p.dc, BrandFont(BrandFace::Display, 26, p.s));
	p.raw = true;
	p.Text(x + 46 + textW, y, 86, kPillH, LeagueClockText(LeagueServerNowMs() - st.since), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	if (cross) {
		const int cx = x + kPillW - 44;
		const bool hot = p.Hover(cx, y, 44, kPillH, mx, my);
		p.Line(cx + 16, y + 16, cx + 28, y + 28, hot ? pal::text : pal::textDim, 2);
		p.Line(cx + 16, y + 28, cx + 28, y + 16, hot ? pal::text : pal::textDim, 2);
		p.AddHit(cx, y, 44, kPillH, kActCancel, 0);
		p.AddHit(x, y, kPillW - 44, kPillH, kActOpen, 0);
	}
	else p.AddHit(x, y, kPillW, kPillH, kActOpen, 0);
}

// The match found, over whatever screen is up: its countdown, Accept and Decline; then the wait for the others; then where to go.
void PaintBox(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY, mid = kBoxW / 2;
	PaintWindowFrame(p, kBoxW, kBoxH);
	p.Fill(0, 0, kBoxW, 3, pal::red);
	SelectObject(p.dc, BrandFont(BrandFace::Display, 46, p.s));
	p.TextCaps(0, 26, kBoxW, 52, st.phase == LeagueRankedPhase::Ready ? "Match ready" : "Match found", pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	const int64_t now = LeagueServerNowMs();
	if (st.phase == LeagueRankedPhase::Ready) {
		p.Font(1);
		p.raw = true;
		p.TextWrapped(48, 124, kBoxW - 96, 90,
			Tr("Go back to the main menu: Multiplayer opens by itself there, to choose your civilization and colour and enter the room."), pal::body);
		p.raw = false;
		p.Button(mid - 130, kBoxH - 84, 260, 52, "Understood", kActHide, 0, mx, my, true, true);
		return;
	}
	const int ringY = 170;
	PaintArc(p, mid, ringY, 50, 7, pal::hairline, 1.0f);
	PaintArc(p, mid, ringY, 50, 7, st.accepted ? pal::ok : pal::red, LeagueCountdownFraction(st.acceptBy, now));
	SelectObject(p.dc, BrandFont(BrandFace::Display, 48, p.s));
	p.raw = true;
	p.Text(mid - 60, ringY - 28, 120, 50, std::to_string(LeagueCountdownSeconds(st.acceptBy, now)), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	const int by = kBoxH - 84;
	if (st.accepted || st.phase == LeagueRankedPhase::Accepted) {
		p.Font(1);
		p.Text(0, by, kBoxW, 52, "Accepted: waiting for the others...", pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	p.fonts[5] = BrandFont(BrandFace::Display, 30, p.s);
	p.Button(mid - 250, by, 300, 52, "Accept", kActAccept, 0, mx, my, !st.answering, true, 5);
	p.Button(mid + 66, by, 184, 52, "Decline", kActDecline, 0, mx, my, !st.answering, false);
}

}

void LeagueSearchUiGameTick()
{
	const DWORD now = GetTickCount();
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (now - g_s.readAt < 250) return;
		g_s.readAt = now;
	}
	const LeagueRankedView v = LeagueRankedState();
	std::lock_guard<std::mutex> lock(g_lock);
	const LeagueRankedPhase was = g_s.phase;
	g_s.phase = v.phase;
	g_s.queue = v.searchQueue;
	g_s.partnerSearch = v.partnerSearch;
	g_s.since = v.since;
	g_s.matchId = v.matchId;
	g_s.acceptBy = v.found.acceptBy;
	g_s.accepted = v.found.accepted;
	g_s.answering = v.answering;
	if (was != g_s.phase && !Shown(g_s)) {
		g_s.hits.clear();
		g_s.mouseDown = false;
	}
}

bool LeagueSearchUiNeedsDraw()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!Shown(g_s)) return false;
	}
	return !InGame() && !MpUiIsOpen() && !LoadCurtainUp();
}

void LeagueSearchUiDraw(uint32_t *buf, int w, int h)
{
	State st;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		st = g_s;
	}
	const float s = ScreenViewFor(w, h).s;
	PanelRender r(g_canvas, buf, w, h);
	if (Matched(st)) {
		if (!r.Begin(kBoxW, kBoxH, 24, true, s)) return;
		r.Press(st.hits, st.mouseDown, {st.mouseX, st.mouseY});
		PaintBox(r.p, st);
	}
	else {
		if (!r.Begin(kPillW, kPillH, 0, false, s)) return;
		r.oy = r.p.oy = static_cast<int>(18 * s);
		r.Press(st.hits, st.mouseDown, {st.mouseX, st.mouseY});
		PaintPill(r.p, st, 0, 0);
	}
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
}

bool LeagueSearchUiHandleMessage(UINT msg, WPARAM, LPARAM lp)
{
	if (msg == WM_LBUTTONUP) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.mouseDown = false;
		if (g_s.swallowUp) {
			g_s.swallowUp = false;
			return true;
		}
	}
	if (!LeagueSearchUiNeedsDraw()) return false;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	int action = 0;
	bool modal = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		modal = Matched(g_s);
		if (msg == WM_MOUSEMOVE) {
			g_s.mouseX = at.x;
			g_s.mouseY = at.y;
			return false;
		}
		const bool press = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK;
		if (press) {
			for (auto it = g_s.hits.rbegin(); it != g_s.hits.rend() && !action; ++it) {
				if (PtInRect(&it->r, at)) action = it->action;
			}
			if (action || modal) {
				g_s.mouseDown = true;
				g_s.swallowUp = true;
			}
		}
		if (action == kActHide) g_s.hiddenMatch = g_s.matchId;
	}
	if (action == kActCancel) LeagueQueueLeave();
	else if (action == kActAccept) LeagueMatchAnswer(true);
	else if (action == kActDecline) LeagueMatchAnswer(false);
	else if (action == kActOpen) MpUiOpenRanked();
	// The window of a match found keeps the mouse and keys of the screen under it; the pill only its own clicks.
	if (modal) return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE ? true : msg == WM_KEYDOWN || msg == WM_CHAR;
	return action != 0;
}
