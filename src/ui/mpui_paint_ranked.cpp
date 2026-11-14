// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueranked.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/leagueui.h"
#include "ui/patronui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kGap = 24, kBandH = 112;
const COLORREF kWinBack = RGB(0x24, 0x30, 0x20), kLossBack = RGB(0x3a, 0x18, 0x1a);

int64_t EpochNow()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return static_cast<int64_t>((((static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) - 116444736000000000ULL) / 10000ULL);
}

// "15/02/2027" in this computer's time.
std::string DateText(int64_t ms, bool withTime = false)
{
	const time_t t = static_cast<time_t>(ms / 1000);
	tm local = {};
	if (localtime_s(&local, &t) != 0) return std::string();
	char buf[48];
	if (withTime) sprintf_s(buf, "%02d/%02d/%04d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900, local.tm_hour, local.tm_min);
	else sprintf_s(buf, "%02d/%02d/%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
	return buf;
}

std::string TimeText(int64_t ms)
{
	const time_t t = static_cast<time_t>(ms / 1000);
	tm local = {};
	if (localtime_s(&local, &t) != 0) return std::string();
	char buf[16];
	sprintf_s(buf, "%02d:%02d", local.tm_hour, local.tm_min);
	return buf;
}

// "40 min ago", "3 h ago", "yesterday" or the date.
std::string AgoText(int64_t at, int64_t now)
{
	const int64_t minutes = std::max<int64_t>(0, (now - at) / 60000);
	if (minutes < 1) return Tr("just now");
	if (minutes < 60) return TrFmt("%s min ago", {std::to_string(minutes)});
	if (minutes < 24 * 60) return TrFmt("%s h ago", {std::to_string(minutes / 60)});
	if (minutes < 48 * 60) return Tr("yesterday");
	return DateText(at);
}

std::string SignedText(int v)
{
	return v > 0 ? "+" + std::to_string(v) : v < 0 ? "\xE2\x88\x92" + std::to_string(-v) : "0";
}

COLORREF Ink(LeagueTier t)
{
	return t == LeagueTier::None ? pal::caption : LeagueTierInk(t);
}

std::string RankName(LeagueTier t, int division)
{
	return t == LeagueTier::None ? Tr("Unranked") : LeagueRankLabel(t, division);
}

LeagueBadge RankBadge(LeagueTier t, int division)
{
	LeagueBadge b;
	if (t == LeagueTier::None) return b;
	b.kind = LeagueBadgeKind::Rank;
	b.tier = t;
	b.division = division;
	return b;
}

void Display(Painter &p, int size)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
}

void Caps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE, int spacing = 2)
{
	p.Font(4);
	p.TextCaps(x, y, w, h, text, c, fmt | DT_END_ELLIPSIS, spacing);
}

void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == kRankedAct && p.pressArg == arg;
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
	Caps(p, x, y, w, h, label, !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	if (enabled) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void Chip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back)
{
	p.Fill(x, y, w, h, back);
	Caps(p, x, y, w, h, text, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
}

void Tile(Painter &p, int x, int y, int w, int h, const std::string &value, const std::string &label, COLORREF ink = pal::text)
{
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::hairline);
	Display(p, 34);
	p.raw = true;
	p.Text(x, y + 6, w, 42, value, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	Caps(p, x, y + h - 26, w, 18, label, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// A player's name with the patron's laurel on its left and, where asked, the badge on its right.
void PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, const LeagueBadge &badge, bool you, int mx, int my)
{
	const PatronInfo patron = id ? PatronOf(id) : PatronInfo();
	const int laurel = patron.tier != PatronTier::None ? 20 : 0;
	const int nameX = x + (laurel ? laurel + 4 : 0);
	const int youW = you ? 40 : 0;
	const int nameW = std::max(20, w - (nameX - x) - LeagueBadgeRoom(badge) - (youW ? youW + 8 : 0));
	p.Font(0);
	p.raw = true;
	const int textW = std::min(nameW, p.TextWidth(name));
	if (laurel) DrawPatron(p, x, y + (h - laurel) / 2, laurel, nameX, y, nameW, h, name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	else p.Text(nameX, y, nameW, h, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DrawBadgeAfterName(p, nameX, y, nameW, h, name, badge, mx, my);
	p.raw = false;
	if (you) Chip(p, nameX + textW + LeagueBadgeRoom(badge) + 8, y + (h - 20) / 2, youW, 20, "You", pal::textDim, pal::hairline);
}

struct HubLayout {
	int m = 64, top = 0, bottom = 0, cardY = 0, cardH = 0, leftW = 520, midX = 0, midW = 0, rightX = 0, rightW = 600, inner = 0;
};

HubLayout HubFor(const State &st)
{
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	HubLayout h;
	h.m = l.margin;
	h.top = l.top;
	h.bottom = l.h - l.margin;
	h.inner = l.w - 2 * l.margin;
	h.cardY = h.top + kBandH + kGap;
	h.cardH = h.bottom - h.cardY;
	h.leftW = std::max(440, h.inner * 520 / 1792);
	h.rightW = std::max(420, h.inner * 600 / 1792);
	h.midX = h.m + h.leftW + kGap;
	h.midW = h.inner - h.leftW - h.rightW - 2 * kGap;
	h.rightX = h.midX + h.midW + kGap;
	return h;
}

void PaintSeasonBand(Painter &p, const State &st, const HubLayout &h)
{
	const LeagueRankedView &v = st.ranked.view;
	const int x = h.m, y = h.top, w = h.inner;
	PaintGlassCard(p, x, y, w, kBandH);
	PaintIsotype(p, x + 56, y + kBandH / 2, 72, 200);
	if (!v.seasonLoaded) {
		Caps(p, x + 120, y + 14, 700, 18, "Ranked season", pal::redText);
		Display(p, 52);
		p.TextCaps(x + 120, y + 34, w - 160, 52, v.noSeason ? "No season yet" : "Ranked 1 vs 1", pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
		p.Font(0);
		p.Text(x + 120, y + 84, w - 160, 22,
			v.seasonLoading ? "Reading the season..." : v.noSeason ? "The first season starts soon." : "Couldn't reach the league server. Check your connection and try again.",
			pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	const LeagueSeason &s = v.season;
	const int64_t now = EpochNow();
	const bool started = s.startsAt <= now, over = s.endsAt <= now;
	std::string state = TrFmt("Season %s", {std::to_string(s.number)}) + "  \xC2\xB7  " +
		(over ? Tr("finished") : started ? Tr("in progress") : TrFmt("starts on %s", {DateText(s.startsAt)}));
	p.raw = true;
	Caps(p, x + 120, y + 14, 700, 18, state, pal::redText);
	p.raw = false;
	int nx = x + 120;
	if (s.beta) {
		p.Font(4);
		const int cw = p.CapsWidth("Test season", 1) + 20;
		Chip(p, x + 120 + std::min(560, p.CapsWidth(state, 2)) + 16, y + 12, cw, 22, "Test season", pal::warn, RGB(0x3a, 0x31, 0x18));
	}
	Display(p, 52);
	p.raw = true;
	const std::string name = LeagueSeasonName(s, v.lang);
	const int nameW = std::min(w - 560, p.CapsWidth(name, 1) + 6);
	p.TextCaps(nx, y + 32, nameW, 52, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	nx += nameW + 16;
	if (!s.theme.empty() && s.theme != "beta") {
		p.Font(0);
		RawText(p, nx, y + 44, std::max(0, x + w - 360 - nx), 30, s.theme, pal::textDim);
	}
	std::string facts = DateText(s.startsAt) + " \xE2\x80\x93 " + DateText(s.endsAt) + "  \xC2\xB7  " + Tr("Ranked 1 vs 1");
	if (!s.maps.empty()) facts += "  \xC2\xB7  " + TrFmt("%s maps", {std::to_string(s.maps.size())});
	p.Font(0);
	RawText(p, x + 120, y + 84, w - 520, 22, facts, pal::caption);
	// The days left, with how much of the season has gone.
	const int rx = x + w - 342;
	Display(p, 64);
	p.raw = true;
	const std::string days = std::to_string(over ? 0 : started ? LeagueDaysLeft(s.endsAt, now) : LeagueDaysLeft(s.startsAt, now));
	const int daysW = p.CapsWidth(days, 0) + 8;
	p.Text(rx, y + 10, daysW, 66, days, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	Caps(p, rx + daysW + 8, y + 22, 220, 18, started ? "Days" : "Days to", pal::textDim);
	Caps(p, rx + daysW + 8, y + 42, 220, 18, started ? "left" : "the start", pal::textDim);
	p.Fill(rx, y + 82, 318, 4, pal::bg);
	p.Fill(rx, y + 82, static_cast<int>(318 * LeagueSeasonElapsed(s, now)), 4, pal::red);
	p.Font(3);
	RawText(p, rx, y + 88, 318, 20, TrFmt("Ends: %s", {DateText(s.endsAt, true)}), pal::caption);
}

// What the hub says under the big button: the lock after declining, the last thing that happened, or what the queue is.
void PaintQueueLine(Painter &p, const LeagueRankedView &v, int x, int y, int w)
{
	const int64_t now = EpochNow();
	const LeagueMeView me = LeagueMeState();
	const int64_t ban = std::max(v.banUntil, me.me.queueBanUntil);
	p.Font(3);
	if (ban > now) {
		RawText(p, x, y, w, 22, TrFmt("You can search again at %s.", {TimeText(ban)}), pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	if (!v.notice.empty()) {
		p.Text(x, y, w, 22, v.notice, v.noticeBad ? pal::bad : pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	p.Text(x, y, w, 22, "The league picks the map and the options of every match.", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void PaintRankCard(Painter &p, const State &st, const HubLayout &h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const LeagueMeView me = LeagueMeState();
	const LeagueRanked &rk = me.me.ranked;
	const int x = h.m, y = h.cardY, w = h.leftW, cardH = h.cardH, ix = x + 24, iw = w - 48, cx = x + w / 2;
	PaintGlassCard(p, x, y, w, cardH);
	const bool searching = v.phase == LeagueRankedPhase::Joining || v.phase == LeagueRankedPhase::Searching;
	const bool matched = v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted || v.phase == LeagueRankedPhase::Ready ||
		v.phase == LeagueRankedPhase::Playing;
	const LeagueTier tier = rk.present ? rk.tier : LeagueTier::None;
	const int64_t now = EpochNow();

	if (searching) {
		Caps(p, ix, y + 20, iw, 20, "Searching for a match", pal::redText);
		p.Spinner(cx, y + 158, 108, 6, GetTickCount());
		DrawLeagueEmblem(p, cx - 80, y + 78, 160, tier);
		Display(p, 84);
		p.raw = true;
		p.Text(x, y + 272, w, 84, LeagueClockText(now - v.since), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		p.Font(0);
		std::string wait = v.phase == LeagueRankedPhase::Joining ? Tr("Entering the queue...")
															   : TrFmt("Estimated wait ~%s", {LeagueClockText(static_cast<int64_t>(v.estimateSec) * 1000)}) + "  \xC2\xB7  " +
				(v.searching == 1 ? Tr("1 searching now") : TrFmt("%s searching now", {std::to_string(v.searching)}));
		RawText(p, x, y + 356, w, 24, wait, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Fill(ix, y + 404, iw, 1, pal::hairline);
		p.Font(3);
		p.TextWrapped(ix, y + 418, iw, 44, "The league widens the range of opponents the longer you wait.", pal::caption);
		Ghost(p, ix, cardH + y - 140, iw, 48, "Cancel search", kRkCancel, v.phase == LeagueRankedPhase::Searching, mx, my);
		p.Font(3);
		p.TextWrapped(ix, cardH + y - 82, iw, 44, "You can keep looking at rooms: a sound plays and the window flashes when a match is found.", pal::caption);
		return;
	}

	if (matched) {
		Caps(p, ix, y + 20, iw, 20, "Your ranked match", pal::redText);
		DrawLeagueEmblem(p, cx - 80, y + 54, 160, v.opponent.tier);
		Display(p, 44);
		p.raw = true;
		const std::string other = v.opponent.name.empty() ? Tr("Opponent") : v.opponent.name;
		p.Text(x + 16, y + 226, w - 32, 50, TrFmt("Against %s", {other}), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		p.Font(0);
		const bool inRoom = st.room.id != 0;
		std::string line = v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted ? Tr("Waiting for both players to accept...")
			: inRoom																			   ? Tr("You are in the room: get ready and start.")
			: v.host																			   ? Tr("Creating the room...")
																										   : Tr("Waiting for the host's room...");
		p.Text(x + 16, y + 280, w - 32, 24, line, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!v.ticket.map.name.empty()) {
			p.Font(3);
			RawText(p, x + 16, y + 308, w - 32, 22, TrFmt("Map: %s", {LeagueMapText(v.ticket.map.name)}), pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if ((v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing) && !inRoom) {
			p.Button(ix, y + cardH - 196, iw, 56, "Go to the room", kRankedAct, kRkRoom, mx, my, !st.connecting, true);
			const bool canCallOff = v.ticketLoaded && LeagueTicketAbandonable(v.ticket) && !v.abandoning;
			Ghost(p, ix, y + cardH - 128, iw, 44, v.abandoning ? "Cancelling..." : "Cancel match", kRkAbandon, canCallOff, mx, my);
			p.Font(3);
			// The server's rule: whether cancelling now locks this player out of the queue for a while.
			const bool costs = v.ticketLoaded && LeagueAbandonCosts(v.ticket, v.me.steamId, now);
			p.TextWrapped(ix, y + cardH - 72, iw, 44,
				costs ? "Cancelling now counts as declining: you couldn't search for a few minutes."
					  : "If the room won't open, press it again or cancel the match: that doesn't count against you.",
				pal::caption);
		}
		return;
	}

	Caps(p, ix, y + 20, iw, 20, "Your rank", pal::redText);
	Chip(p, x + w - 24 - 98, y + 14, 98, 32, "1 vs 1", pal::text, pal::selected);
	p.Frame(x + w - 24 - 98, y + 14, 98, 32, pal::edgeHot);
	const bool placing = !rk.present || rk.placementLeft > 0;
	DrawLeagueEmblem(p, cx - 80, y + 54, 160, tier);
	Display(p, 60);
	p.raw = true;
	p.TextCaps(x, y + 216, w, 62, placing ? Tr("Placement") : RankName(tier, rk.division), Ink(placing ? LeagueTier::None : tier),
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	if (!me.loaded) {
		p.Text(x, y + 278, w, 24, me.loading ? "Reading your rank..." : "Your rank will show here.", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else if (placing) {
		const int left = rk.present ? rk.placementLeft : 5, played = std::max(0, 5 - left);
		RawText(p, x, y + 278, w, 24, left == 1 ? Tr("1 placement game left") : TrFmt("%s placement games left", {std::to_string(left)}), pal::body,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		const int box = 56, gap = 12, bx = cx - (5 * box + 4 * gap) / 2;
		for (int i = 0; i < 5; i++) {
			p.Fill(bx + i * (box + gap), y + 318, box, 12, i < played ? pal::textDim : pal::bg);
			p.Frame(bx + i * (box + gap), y + 318, box, 12, pal::hairline);
		}
		p.Font(3);
		p.TextWrapped(ix, y + 340, iw, 40, "Your first five games place you, at most in Centurion I.", pal::caption);
	}
	else {
		const int total = st.ranked.view.top.total;
		std::string line = TrFmt("%s Glory", {std::to_string(rk.glory)});
		// The place only motivates in the top half; below it the bar says more.
		if (rk.position > 0 && total > 0 && rk.position <= std::max(1, total / 2)) line += "  \xC2\xB7  " + TrFmt("place %s of %s", {std::to_string(rk.position), std::to_string(total)});
		else if (rk.position > 0 && total == 0) line += "  \xC2\xB7  " + TrFmt("place %s", {std::to_string(rk.position)});
		RawText(p, x, y + 278, w, 24, line, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		const LeagueGloryPlace gp = LeagueGloryPlaceOf(rk.glory);
		const bool elite = !LeagueTierHasDivisions(tier);
		p.Fill(ix, y + 318, iw, 10, pal::bg);
		const float frac = gp.capped && !elite ? gp.into / 100.0f : 1.0f;
		p.Fill(ix, y + 318, static_cast<int>(iw * frac), 10, Ink(tier));
		p.Font(0);
		if (elite || !gp.capped) {
			RawText(p, ix, y + 334, iw, 22, elite ? Tr("Elite rank: it goes by your place in the leaderboard.") : Tr("Top division: climb the leaderboard to the elite."),
				pal::caption);
		}
		else {
			RawText(p, ix, y + 334, iw / 2, 22, std::to_string(gp.into) + " / 100 " + Tr("Glory"), pal::caption);
			LeagueTier nt = LeagueTier::None;
			int nd = 0;
			LeagueNextDivision(gp.tier, gp.division, nt, nd);
			if (nt != LeagueTier::None) {
				const int wins = LeagueWinsToNext(rk.glory);
				RawText(p, ix + iw / 2, y + 334, iw / 2, 22,
					wins == 1 ? TrFmt("~1 win to %s", {LeagueRankLabel(nt, nd)}) : TrFmt("~%s wins to %s", {std::to_string(wins), LeagueRankLabel(nt, nd)}), Ink(nt),
					DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
		}
	}
	// Record, win rate and place.
	const int tw = (iw - 2 * 20) / 3, ty = y + 374;
	Tile(p, ix, ty, tw, 72, std::to_string(rk.wins) + "\xE2\x80\x93" + std::to_string(rk.losses), "Record");
	Tile(p, ix + tw + 20, ty, tw, 72, std::to_string(LeagueWinRate(rk.wins, rk.losses)) + " %", "Win rate");
	Tile(p, ix + 2 * (tw + 20), ty, tw, 72, rk.position > 0 ? "#" + std::to_string(rk.position) : std::string("\xE2\x80\x94"), "Place");
	p.Font(0);
	if (rk.shield > 0) {
		p.Fill(ix, y + 458, iw, 30, RGB(0x22, 0x2c, 0x1e));
		RawText(p, ix + 12, y + 458, iw - 24, 30, rk.shield == 1 ? Tr("Rank shield: 1 more game") : TrFmt("Rank shield: %s more games", {std::to_string(rk.shield)}),
			pal::ok);
	}
	else {
		p.Font(3);
		RawText(p, ix, y + 462, iw, 22, TrFmt("Season %s  \xC2\xB7  %s games", {std::to_string(rk.season), std::to_string(rk.wins + rk.losses)}), pal::caption);
	}
	const int64_t ban = std::max(v.banUntil, me.me.queueBanUntil);
	const bool canSearch = v.linked && v.seasonLoaded && v.season.active && ban <= now && v.phase == LeagueRankedPhase::Idle;
	p.fonts[5] = BrandFont(BrandFace::Display, 34, p.s);
	p.Button(ix, y + cardH - 108, iw, 64, "Search for a match  1 vs 1", kRankedAct, kRkSearch, mx, my, canSearch, true, 5);
	if (v.seasonLoaded && !v.season.active) {
		p.Font(3);
		p.Text(ix, y + cardH - 38, iw, 22, "The season is not being played now.", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else PaintQueueLine(p, v, ix, y + cardH - 38, iw);
}

void PaintRecent(Painter &p, const State &st, const HubLayout &h, int y, int cardH)
{
	const LeagueRankedView &v = st.ranked.view;
	const int x = h.midX, w = h.midW, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	Caps(p, ix, y + 16, iw, 22, "Recent games", pal::redText);
	p.Fill(x + 1, y + 50, w - 2, 1, pal::hairline);
	const int rowH = 51, rows = std::min<int>(static_cast<int>(v.recent.size()), (cardH - 56) / (rowH + 1));
	if (!rows) {
		p.Font(0);
		p.TextWrapped(ix, y + 68, iw, 60, "Your ranked games will show here, with the Glory each one gave.", pal::caption);
		return;
	}
	const int64_t now = EpochNow();
	for (int i = 0; i < rows; i++) {
		const LeagueRankedGame &g = v.recent[i];
		const int ry = y + 51 + i * (rowH + 1);
		const bool counted = g.status == "confirmed" || g.status == "settled";
		Chip(p, ix, ry + (rowH - 24) / 2, 84, 24, g.won ? "Victory" : "Defeat", g.won ? pal::ok : pal::bad, g.won ? kWinBack : kLossBack);
		Display(p, 26);
		p.raw = true;
		const std::string delta = counted ? SignedText(g.delta) : g.status == "disputed" ? std::string("?") : std::string("\xE2\x80\xA6");
		p.Text(ix + 96, ry, 56, rowH, delta, !counted ? pal::caption : g.delta >= 0 ? pal::ok : pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		const int tx = ix + 160, tw = iw - 160 - 110;
		p.Font(0);
		RawText(p, tx, ry + 4, tw, 24, g.opponent.empty() ? Tr("Opponent") : g.opponent, pal::text);
		std::string facts = g.map.empty() ? std::string() : g.map;
		if (g.durationMs) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + TrFmt("%s min", {std::to_string(g.durationMs / 60000)});
		if (!counted) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + Tr(g.status == "disputed" ? "in dispute" : g.status.empty() ? "not sent yet" : "awaiting the other report");
		p.Font(3);
		RawText(p, tx, ry + 27, tw, 20, facts, pal::caption);
		RawText(p, ix + iw - 110, ry, 110, rowH, AgoText(g.at, now), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Fill(x + 1, ry + rowH, w - 2, 1, pal::hairline);
	}
}

// The ranks of the league, lit up to the player's, and the rules in four lines.
void PaintRules(Painter &p, const State &st, const HubLayout &h, int y, int cardH)
{
	const LeagueMeView me = LeagueMeState();
	const LeagueTier mine = me.loaded && me.me.ranked.present ? me.me.ranked.tier : LeagueTier::None;
	const int x = h.midX, w = h.midW, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	Caps(p, ix, y + 16, iw, 22, "Ranks of the season", pal::redText);
	const int n = kLeagueTierCount, step = iw / n, track = y + 106;
	p.Fill(ix + step / 2, track, step * (n - 1), 2, pal::lineStrong);
	if (mine != LeagueTier::None) p.Fill(ix + step / 2, track, step * (static_cast<int>(mine) - 1), 2, Ink(mine));
	for (int i = 0; i < n; i++) {
		const LeagueTier t = static_cast<LeagueTier>(i + 1);
		const int cx = ix + step * i + step / 2;
		const bool lit = mine != LeagueTier::None && static_cast<int>(t) <= static_cast<int>(mine);
		DrawLeagueEmblem(p, cx - 20, y + 52, 40, t, lit ? 255 : 110);
		p.Fill(cx - 6, track - 5, 12, 12, t == mine ? pal::bg : lit ? Ink(t) : pal::hairline);
		if (t == mine) p.Frame(cx - 6, track - 5, 12, 12, Ink(t));
		Display(p, 16);
		p.TextCaps(cx - step / 2, y + 118, step, 18, LeagueTierName(t), lit || t == mine ? Ink(t) : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
			0);
	}
	static const char *const kRules[] = {
		"Each division is 100 Glory; reach the next hundred and you go up, with no promotion series.",
		"After going up, a rank shield keeps you there for 3 games.",
		"Praetorian and Imperator go to the best 50 and 10 of the leaderboard.",
		"When the season closes you keep the emblem of the highest rank you reached (10 games).",
	};
	p.Font(3);
	int ry = y + 150;
	for (const char *rule : kRules) {
		const int th = p.Measure(iw - 16, rule);
		if (ry + th > y + cardH - 12) break;
		p.Fill(ix, ry + 7, 4, 4, pal::redText);
		p.TextWrapped(ix + 16, ry, iw - 16, th + 2, rule, pal::textDim);
		ry += th + 8;
	}
}

void PaintMiniBoard(Painter &p, const State &st, const HubLayout &h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const int x = h.rightX, y = h.cardY, w = h.rightW, cardH = h.cardH, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	Caps(p, ix, y + 16, iw - 160, 22, "Leaderboard  \xC2\xB7  1 vs 1", pal::redText);
	p.Font(3);
	if (v.boardLoaded) {
		RawText(p, ix + iw - 200, y + 16, 200, 22, v.top.total == 1 ? Tr("1 player") : TrFmt("%s players", {std::to_string(v.top.total)}), pal::caption,
			DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	const int glW = 80, wlW = 96, nameW = iw - 40 - glW - wlW - 16;
	Caps(p, ix, y + 48, 40, 28, "#", pal::caption);
	Caps(p, ix + 40, y + 48, nameW, 28, "Player", pal::caption);
	Caps(p, ix + 40 + nameW + 16, y + 48, glW, 28, "Glory", pal::caption);
	Caps(p, ix + iw - wlW, y + 48, wlW, 28, "W\xE2\x80\x93L", pal::caption);
	p.Fill(ix, y + 78, iw, 1, pal::lineStrong);
	const std::string own = std::to_string(SteamOwnId());
	std::vector<uint64_t> ids;
	for (const LeagueBoardRow &r : v.top.rows) ids.push_back(_strtoui64(r.steamId.c_str(), nullptr, 10));
	if (!ids.empty()) PatronsWatch(ids);
	const int rowH = 43;
	auto row = [&](const LeagueBoardRow &r, int ry, bool you) {
		p.Font(you || r.position <= 3 ? 1 : 0);
		RawText(p, ix, ry, 40, rowH, std::to_string(r.position), you || r.position <= 3 ? pal::text : pal::caption);
		const uint64_t id = _strtoui64(r.steamId.c_str(), nullptr, 10);
		PaintName(p, ix + 40, ry, nameW, rowH, r.name.empty() ? Tr("Player") : r.name, id, RankBadge(r.tier, r.division), you, mx, my);
		p.Font(1);
		RawText(p, ix + 40 + nameW + 16, ry, glW, rowH, std::to_string(r.glory), pal::text);
		p.Font(0);
		RawText(p, ix + iw - wlW, ry, wlW, rowH, std::to_string(r.wins) + "\xE2\x80\x93" + std::to_string(r.losses), pal::caption);
	};
	if (!v.boardLoaded) {
		p.Spinner(ix + 12, y + 104, 9, 4, GetTickCount());
		p.Font(3);
		p.Text(ix + 32, y + 92, iw - 32, 24, "Reading the leaderboard...", pal::caption);
	}
	else if (v.top.rows.empty()) {
		p.Font(0);
		p.TextWrapped(ix, y + 96, iw, 60, "Nobody has finished their placement games yet. Be the first on the leaderboard.", pal::caption);
	}
	bool listed = false;
	for (size_t i = 0; i < v.top.rows.size() && i < 10; i++) {
		const LeagueBoardRow &r = v.top.rows[i];
		const bool you = r.steamId == own;
		listed = listed || you;
		const int ry = y + 82 + static_cast<int>(i) * rowH;
		if (you) {
			p.Fill(x + 1, ry, w - 2, rowH, pal::selected);
			p.Fill(x + 1, ry, 3, rowH, pal::red);
		}
		row(r, ry, you);
	}
	// The player's own row stays pinned under the first ten.
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	const int pinY = y + 82 + 10 * rowH + 28;
	if (mine && !listed) {
		p.Font(0);
		p.Text(ix, pinY - 28, iw, 26, "\xC2\xB7 \xC2\xB7 \xC2\xB7", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Fill(x + 1, pinY, w - 2, 50, pal::selected);
		p.Fill(x + 1, pinY, 3, 50, pal::red);
		LeagueBoardRow pinned = *mine;
		row(pinned, pinY + 3, true);
	}
	Ghost(p, ix, y + cardH - 52, iw, 36, "Full leaderboard", kRkBoard, true, mx, my);
}

void PaintHub(Painter &p, const State &st)
{
	const HubLayout h = HubFor(st);
	PaintSeasonBand(p, st, h);
	PaintRankCard(p, st, h);
	const int half = (h.cardH - kGap) / 2;
	PaintRecent(p, st, h, h.cardY, half);
	PaintRules(p, st, h, h.cardY + half + kGap, h.cardH - half - kGap);
	PaintMiniBoard(p, st, h);
}

// The full leaderboard: a page of the table, the player's row pinned under it and the card of the one picked.
void PaintBoard(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m;
	const int detailW = std::min(537, std::max(400, inner * 537 / 1792)), tableW = inner - detailW - 32;
	const LeagueBoard &b = v.page;
	const int pages = std::max(1, (b.total + kRankedBoardRows - 1) / kRankedBoardRows), page = b.offset / kRankedBoardRows + 1;

	int tx = m;
	{
		const int w = 150;
		const bool hot = p.Hover(tx, top, w, 44, mx, my);
		p.Frame(tx, top, w, 44, hot ? pal::edgeHot : pal::edge);
		PaintBackArrow(p, tx + 18, top + 22, hot ? pal::text : pal::textDim);
		Caps(p, tx + 18 + kBackArrowW, top, w - 26 - kBackArrowW, 44, "Summary", hot ? pal::text : pal::textDim);
		p.AddHit(tx, top, w, 44, kRankedAct, kRkHub);
		tx += w + 16;
	}
	Chip(p, tx, top, 110, 44, "1 vs 1", pal::text, pal::selected);
	p.Frame(tx, top, 110, 44, pal::edgeHot);
	tx += 126;
	Ghost(p, tx, top, 130, 44, "Top", kRkBoardTop, v.pageLoaded && b.offset > 0, mx, my);
	tx += 146;
	Ghost(p, tx, top, 170, 44, "My place", kRkBoardMe, v.around.me > 0, mx, my);
	tx += 186;
	Ghost(p, tx, top, 52, 44, "\xE2\x80\xB9", kRkBoardPrev, v.pageLoaded && b.offset > 0, mx, my);
	Ghost(p, tx + 60, top, 52, 44, "\xE2\x80\xBA", kRkBoardNext, v.pageLoaded && page < pages, mx, my);
	p.Font(0);
	if (v.pageLoaded) {
		RawText(p, tx + 128, top, m + tableW - tx - 128, 44, TrFmt("Page %s of %s", {std::to_string(page), std::to_string(pages)}) + "  \xC2\xB7  " +
			(b.total == 1 ? Tr("1 player") : TrFmt("%s players", {std::to_string(b.total)})), pal::caption);
	}

	const int y0 = top + 72, h = l.h - m - y0;
	PaintGlassCard(p, m, y0, tableW, h);
	struct Col {
		const char *label;
		int x, w;
	};
	const int nameW = std::max(160, tableW - 720);
	const Col cols[] = {{"#", 20, 56}, {"Player", 80, nameW}, {"Rank", 80 + nameW + 12, 208}, {"Glory", 80 + nameW + 232, 96},
		{"Games", 80 + nameW + 336, 96}, {"W\xE2\x80\x93L", 80 + nameW + 440, 104}, {"%", 80 + nameW + 552, 64}};
	for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]); i++) Caps(p, m + cols[i].x, y0, cols[i].w, 44, cols[i].label, i == 3 ? pal::redText : pal::caption);
	p.Fill(m + 1, y0 + 43, tableW - 2, 1, pal::lineStrong);
	const std::string own = std::to_string(SteamOwnId());
	const int rowH = 46;
	auto paintRow = [&](const LeagueBoardRow &r, int ry, int rh, bool you, bool selected, int index) {
		if (selected || you) p.Fill(m + 1, ry, tableW - 2, rh, selected ? pal::raised : pal::selected);
		if (you) p.Fill(m + 1, ry, 3, rh, pal::red);
		const bool hot = index >= 0 && p.Hover(m, ry, tableW, rh, mx, my);
		if (hot && !selected && !you) p.Fill(m + 1, ry, tableW - 2, rh, pal::raised);
		p.Font(r.position <= 10 || you ? 1 : 0);
		RawText(p, m + cols[0].x, ry, cols[0].w, rh, std::to_string(r.position), r.position <= 10 || you ? pal::text : pal::caption);
		const uint64_t id = _strtoui64(r.steamId.c_str(), nullptr, 10);
		const int av = 32;
		if (!PaintAvatar(p, m + cols[1].x, ry + (rh - av) / 2, av, id)) p.Fill(m + cols[1].x, ry + (rh - av) / 2, av, av, pal::hairline);
		PaintName(p, m + cols[1].x + av + 12, ry, cols[1].w - av - 12, rh, r.name.empty() ? Tr("Player") : r.name, id, LeagueBadge(), you, mx, my);
		DrawLeagueBadge(p, m + cols[2].x, ry + (rh - kLeagueBadgeSize) / 2, kLeagueBadgeSize, RankBadge(r.tier, r.division));
		Display(p, 22);
		p.raw = true;
		p.TextCaps(m + cols[2].x + 28, ry, cols[2].w - 28, rh, RankName(r.tier, r.division), Ink(r.tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		p.Font(1);
		RawText(p, m + cols[3].x, ry, cols[3].w, rh, std::to_string(r.glory), pal::text);
		p.Font(0);
		RawText(p, m + cols[4].x, ry, cols[4].w, rh, std::to_string(r.wins + r.losses), pal::body);
		RawText(p, m + cols[5].x, ry, cols[5].w, rh, std::to_string(r.wins) + "\xE2\x80\x93" + std::to_string(r.losses), pal::body);
		RawText(p, m + cols[6].x, ry, cols[6].w, rh, std::to_string(LeagueWinRate(r.wins, r.losses)) + " %", pal::body);
		if (index >= 0) p.AddHit(m, ry, tableW, rh, kRankedAct, kRkRow + index);
	};
	std::vector<uint64_t> ids;
	for (const LeagueBoardRow &r : b.rows) ids.push_back(_strtoui64(r.steamId.c_str(), nullptr, 10));
	if (!ids.empty()) PatronsWatch(ids);
	int ry = y0 + 44;
	bool listed = false;
	if (!v.pageLoaded) {
		p.Spinner(m + 32, ry + 32, 9, 4, GetTickCount());
		p.Font(0);
		p.Text(m + 56, ry + 18, tableW - 80, 28, v.pageError.empty() ? Tr("Reading the leaderboard...") : v.pageError, v.pageError.empty() ? pal::caption : pal::bad);
	}
	else if (b.rows.empty()) {
		p.Font(0);
		p.TextWrapped(m + 24, ry + 18, tableW - 48, 60, "Nobody has finished their placement games yet. Be the first on the leaderboard.", pal::caption);
	}
	for (size_t i = 0; i < b.rows.size() && i < static_cast<size_t>(kRankedBoardRows); i++) {
		const LeagueBoardRow &r = b.rows[i];
		const bool you = r.steamId == own;
		listed = listed || you;
		paintRow(r, ry, rowH, you, st.ranked.boardSel == static_cast<int>(i), static_cast<int>(i));
		ry += rowH;
		// The line between the Imperators and the rest: what the best ones play for.
		if (r.position == 10 && i + 1 < b.rows.size()) {
			p.Fill(m + 1, ry, tableW - 2, 28, RGB(0x26, 0x12, 0x13));
			p.Fill(m + 24, ry + 14, 120, 1, RGB(0x5a, 0x2a, 0x2c));
			p.Fill(m + tableW - 144, ry + 14, 120, 1, RGB(0x5a, 0x2a, 0x2c));
			Caps(p, m + 150, ry, tableW - 300, 28, "Imperator line  \xC2\xB7  the best ten", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			ry += 28;
		}
	}
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	if (mine && !listed && v.pageLoaded) {
		const int py = y0 + h - 52;
		p.Fill(m + 1, py - 2, tableW - 2, 2, pal::lineStrong);
		paintRow(*mine, py, 51, true, false, -1);
	}

	// The card of the player picked, the player's own when nobody is.
	const int dx = m + tableW + 32;
	PaintGlassCard(p, dx, y0, detailW, h);
	const LeagueBoardRow *pick = st.ranked.boardSel >= 0 && st.ranked.boardSel < static_cast<int>(b.rows.size()) ? &b.rows[st.ranked.boardSel] : mine;
	const int ix = dx + 24, iw = detailW - 48;
	if (!pick) {
		p.Font(0);
		p.TextWrapped(ix, y0 + 24, iw, 80, "Pick a player of the table to see their rank.", pal::caption);
		return;
	}
	const uint64_t id = _strtoui64(pick->steamId.c_str(), nullptr, 10);
	if (!PaintAvatar(p, ix, y0 + 24, 88, id)) p.Fill(ix, y0 + 24, 88, 88, pal::hairline);
	p.Frame(ix, y0 + 24, 88, 88, pal::edge);
	SelectObject(p.dc, BrandFont(BrandFace::Body, 24, p.s));
	RawText(p, ix + 104, y0 + 26, iw - 104, 32, pick->name.empty() ? Tr("Player") : pick->name, pal::text);
	p.Font(0);
	RawText(p, ix + 104, y0 + 62, iw - 104, 24, TrFmt("Place %s  \xC2\xB7  1 vs 1", {std::to_string(pick->position)}), pal::caption);
	if (pick->steamId == own) Chip(p, ix + 104, y0 + 90, 40, 20, "You", pal::textDim, pal::hairline);
	p.Fill(ix, y0 + 132, iw, 1, pal::hairline);
	DrawLeagueEmblem(p, ix - 4, y0 + 146, 132, pick->tier);
	Display(p, 54);
	p.raw = true;
	p.TextCaps(ix + 142, y0 + 162, iw - 142, 54, RankName(pick->tier, pick->division), Ink(pick->tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	RawText(p, ix + 142, y0 + 218, iw - 142, 24, "#" + std::to_string(pick->position) + "  \xC2\xB7  " + TrFmt("%s Glory", {std::to_string(pick->glory)}), pal::body);
	const int tw = (iw - 3 * 14) / 4, ty = y0 + 300;
	Tile(p, ix, ty, tw, 74, std::to_string(pick->wins + pick->losses), "Games");
	Tile(p, ix + tw + 14, ty, tw, 74, std::to_string(LeagueWinRate(pick->wins, pick->losses)) + " %", "Win rate");
	Tile(p, ix + 2 * (tw + 14), ty, tw, 74, std::to_string(pick->wins), "Victories", pal::ok);
	Tile(p, ix + 3 * (tw + 14), ty, tw, 74, std::to_string(pick->losses), "Defeats");
	if (id && id != SteamOwnId()) {
		p.Font(1);
		p.Button(ix, y0 + h - 76, iw, 52, "Steam profile", kRankedAct, kRkSteam, mx, my, st.ranked.boardSel >= 0, false);
	}
}

}

bool PaintRankedTab(Painter &p, const State &st)
{
	if (st.ranked.resultOpen) {
		PaintRankedResult(p, st);
		return true;
	}
	if (!st.ranked.view.linked) return false;
	LeagueSeasonRefresh();
	LeagueBoardRefresh();
	LeagueMeRefresh();
	PatronsWatch({SteamOwnId()});
	if (st.ranked.sub == kRankedBoard) PaintBoard(p, st);
	else PaintHub(p, st);
	return true;
}

}
