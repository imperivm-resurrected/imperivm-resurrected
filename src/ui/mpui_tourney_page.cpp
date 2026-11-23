// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leaguemodsprep.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/scrollpane.h"

namespace mpui_tourney_impl {
namespace {

// TorneoCuadro's colours that the palette does not have.
const COLORREF kLinePath = RGB(0x7a, 0x75, 0x6e), kGold = RGB(0xe0, 0xb5, 0x4a), kGoldDark = RGB(0x8a, 0x6a, 0x22);
const COLORREF kSilver = RGB(0xc9, 0xce, 0xd4), kBronze = RGB(0xcf, 0x8a, 0x55), kWinBack = RGB(0x3c, 0x5a, 0x32);
// The champion's place after the last match's line.
const int kChampW = 150;
// The page's tab of a Swiss round: kRoundTab + the round. The bracket's tab (0) is the Swiss standings.
const int kRoundTab = 0x10;

// What a frame of the page knows: the tournament, the player and where things go.
struct Ctx {
	const State &st;
	const Ui &u;
	const TourneyDetail &d;
	const TourneyMineView &mine;
	int64_t me = 0;
	int mx = 0, my = 0;
	std::vector<uint64_t> ids;
	// The player's next match, when it is in this tournament.
	const TourneyNext *next = nullptr;
	// The cup's disputes, read once a frame for its organizer only.
	TourneyDisputesView disputes;
};

// The players' Steam ids, for their badges; the ones without an id are left out.
void AddIds(Ctx &c, const std::vector<TourneyMember> &players)
{
	for (const TourneyMember &pl : players) {
		if (pl.steamId) c.ids.push_back(pl.steamId);
	}
}

void HLine(Painter &p, int x1, int x2, int y, COLORREF c)
{
	p.Fill(std::min(x1, x2), y, std::abs(x2 - x1) + 1, 1, c);
}

void VLine(Painter &p, int x, int y1, int y2, COLORREF c)
{
	p.Fill(x, std::min(y1, y2), 1, std::abs(y2 - y1) + 1, c);
}

// The name of whoever comes to an empty side: the two of the match before, or nobody yet. Only the winners' bracket has a match before
// each side; the losers' bracket takes in players from both brackets.
std::string ComingName(const TourneyDetail &d, const TourneyMatch &m, bool sideB)
{
	if (m.round <= 1 || m.bracket != TourneyBracket::Winners) return Tr(m.status == TourneyMatchStatus::Bye ? "Straight through" : "To be decided");
	const TourneyMatch *feeder = TourneyMatchAt(d, m.round - 1, 2 * m.position + (sideB ? 1 : 0));
	if (!feeder) return Tr("To be decided");
	if (feeder->status == TourneyMatchStatus::Walkover && !feeder->winner) return Tr("Nobody: both missed it");
	if (feeder->a.Present() && feeder->b.Present()) return TrFmt("%s or %s", {feeder->a.name, feeder->b.name});
	return Tr("To be decided");
}

// A side of a box, `h` high: its seed, its name (a team's with its players under it when there is room) and its score.
void PaintSide(Painter &p, Ctx &c, const TourneyMatch &m, const TourneyCard &card, bool sideB, int x, int y, int w, int h)
{
	const int seedW = 26, scoreW = 32;
	const bool over = TourneyMatchOver(m.status);
	const bool won = card.Present() && m.winner == card.entryId;
	const bool lost = over && card.Present() && !won;
	// The player's own place: its row on a dark red and its seed on the red, instead of a tag that crowded the badge after the name.
	const bool own = card.Present() && c.me && card.entryId == c.me;
	if (own) p.Fill(x + seedW, y, w - seedW, h, RGB(0x3a, 0x15, 0x17));
	p.Fill(x, y, seedW, h, own ? pal::red : pal::selected);
	UseFace(p, BrandFace::UiStrong, 12);
	p.raw = true;
	p.Text(x, y, seedW, h, card.Present() ? (card.seed ? std::to_string(card.seed) : std::string("-")) : std::string(), own ? pal::onRed : pal::caption,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	const int nx = x + seedW + 10, nw = w - seedW - scoreW - 20;
	if (!card.Present()) {
		UseFace(p, BrandFace::Body, 15);
		p.raw = true;
		p.Text(nx, y, nw, h, ComingName(c.d, m, sideB), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	else {
		UseFace(p, won || card.entryId == c.me ? BrandFace::UiStrong : BrandFace::Body, 15);
		const COLORREF ink = won || card.entryId == c.me ? pal::text : lost ? pal::caption : pal::textDim;
		const bool mine = card.entryId == c.me;
		const int youW = 0;
		std::string extra;
		if (c.next && c.next->match == m.id && !mine && c.next->opponent.inRoom && !over) extra = Tr("in the room");
		// A team: its name over its players, when the box is tall enough and the name is not theirs already.
		const std::string players = c.d.Teams() ? TeamPlayersLine(card.name, card.members) : std::string();
		const bool two = !players.empty() && h >= 36;
		const int nameH = two ? 22 : h, nameY = two ? y + 2 : y;
		p.raw = true;
		const int textW = std::min(nw - youW, p.TextWidth(card.name) + 2);
		p.raw = false;
		if (c.d.Teams()) {
			p.raw = true;
			p.Text(nx, nameY, nw - youW - (extra.empty() ? 0 : 70), nameH, card.name, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		int at = nx;
		if (!c.d.Teams()) at = PaintName(p, nx, y, nw - youW - (extra.empty() ? 0 : 70), h, card.name, card.steamId, ink, c.mx, c.my);
		const int shownW = std::min(textW, nw - youW - (at - nx));
		// Out of the match: the name struck through.
		if (lost && m.status != TourneyMatchStatus::Bye) p.Fill(at - 2, nameY + nameH / 2, shownW + 4, 1, pal::caption);
		if (two) {
			UseFace(p, BrandFace::Ui, 12);
			p.raw = true;
			p.Text(nx, y + 22, nw, h - 24, players, lost ? pal::caption : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		if (!extra.empty()) {
			UseFace(p, BrandFace::Ui, 12);
			p.raw = true;
			p.Text(nx + nw - 70, nameY, 70, nameH, "\xC2\xB7 " + extra, pal::ok, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.raw = false;
		AddIds(c, TourneyPlayersOf(card));
	}
	const int sx = x + w - scoreW;
	// The loser's score on a dark red, the winner's on the button's grey.
	const COLORREF kLostScore = RGB(0x4e, 0x15, 0x18);
	// The player's row keeps its red through the score until there is one.
	p.Fill(sx, y, scoreW, h, won ? pal::button : lost ? kLostScore : own ? RGB(0x3a, 0x15, 0x17) : pal::field);
	if (m.status == TourneyMatchStatus::Bye && won) {
		UseFace(p, BrandFace::UiStrong, 11);
		p.TextCaps(sx - 14, y, scoreW + 14, h, "Bye", pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	}
	else if (card.Present() && m.status != TourneyMatchStatus::Bye) {
		const bool showScore = over || m.scoreA + m.scoreB > 0;
		UseDisplay(p, 21);
		p.raw = true;
		const int score = sideB ? m.scoreB : m.scoreA;
		std::string text = showScore ? std::to_string(score) : std::string();
		if (m.status == TourneyMatchStatus::Walkover && won) text = "W";
		else if (m.status == TourneyMatchStatus::Live && !showScore) text = "-";
		p.Text(sx, y, scoreW, h, text, won ? pal::text : lost ? pal::redText : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}
}

// What is happening in a match, over its box or beside its pairing: live, its room open for this player, under review or a no-show; empty
// otherwise. `ink` is the colour to write it in.
std::string MatchStateText(const Ctx &c, const TourneyMatch &m, COLORREF &ink)
{
	ink = pal::caption;
	if (m.status == TourneyMatchStatus::Live) {
		ink = pal::redText;
		return m.startedAt ? TrFmt("Live \xC2\xB7 %s", {DurationText(NowMs() - m.startedAt)}) : Tr("Live");
	}
	if (c.me && m.Has(c.me) && m.status == TourneyMatchStatus::Ready) {
		ink = pal::warn;
		// Paced by rounds, the match is open until the round's deadline, days away, which the progress card gives: no countdown here.
		if (TourneyByRounds(c.d) || m.deadlineAt <= NowMs()) return Tr("Room open");
		return TrFmt("Room open \xC2\xB7 %s to enter", {TourneyClock(m.deadlineAt - NowMs())});
	}
	if (m.status == TourneyMatchStatus::Disputed) {
		ink = pal::warn;
		return Tr("Under review");
	}
	if (m.status == TourneyMatchStatus::Walkover && m.a.Present() && m.b.Present()) return Tr("No-show");
	return std::string();
}

void PaintBox(Painter &p, Ctx &c, const TourneyMatch &m, int x, int y, const TourneyBracketMetrics &bm)
{
	const bool live = m.status == TourneyMatchStatus::Live;
	const bool mine = c.me && m.Has(c.me) && !TourneyMatchOver(m.status);
	const bool bye = m.status == TourneyMatchStatus::Bye || (m.status == TourneyMatchStatus::Walkover && !(m.a.Present() && m.b.Present()));
	p.Fill(x, y, bm.boxW, bm.boxH, live || mine ? pal::selected : pal::raised);
	if (bye) PaintDashedFrame(p, x, y, bm.boxW, bm.boxH, pal::edgeHot);
	else p.Frame(x, y, bm.boxW, bm.boxH, live ? pal::red : mine ? pal::warn : pal::edge);
	const int sideH = (bm.boxH - 2) / 2;
	PaintSide(p, c, m, m.a, false, x + 1, y + 1, bm.boxW - 2, sideH);
	p.Fill(x + 1, y + 1 + sideH, bm.boxW - 2, 1, pal::hairline);
	PaintSide(p, c, m, m.b, true, x + 1, y + 2 + sideH, bm.boxW - 2, bm.boxH - 3 - sideH);
	// The label over the box: what is happening in it.
	COLORREF ink = pal::caption;
	const std::string label = MatchStateText(c, m, ink);
	if (!label.empty()) {
		int lx = x;
		if (live) {
			p.Shape(x, y - 15, 8, 8, InUnitEllipse, pal::red);
			lx += 15;
		}
		UseFace(p, BrandFace::UiStrong, 11);
		p.raw = true;
		p.TextCaps(lx, y - 21, bm.boxW, 18, label, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		p.raw = false;
	}
	if (live && m.lobby) p.AddHit(x, y, bm.boxW, bm.boxH, kTourney, ArgOf(kSubWatch, m.id));
	else if (mine && c.next && c.next->match == m.id) p.AddHit(x, y, bm.boxW, bm.boxH, kTourney, ArgOf(kSubMatch, m.id));
}

COLORREF LineInk(const Ctx &c, const TourneyMatch *m)
{
	if (!m) return pal::lineStrong;
	if (m->status == TourneyMatchStatus::Live) return pal::red;
	if (c.me && m->Has(c.me) && m->status == TourneyMatchStatus::Ready) return pal::warn;
	return m->a.Present() && m->b.Present() ? kLinePath : pal::lineStrong;
}

// The map a match is played on when the page can tell: a round of a single elimination or of a Swiss tournament. The other parts of a
// double elimination learn theirs from the room.
TourneyMap MatchMap(const TourneyDetail &d, const TourneyMatch &m)
{
	if (m.bracket == TourneyBracket::Swiss) return TourneyMapOfRound(d, m.round, 0);
	if (m.bracket != TourneyBracket::Winners || TourneyIsDouble(d)) return TourneyMap();
	return TourneyMapOfRound(d, m.round, d.rounds);
}

// A bracket larger than its view, panned by its scroll bars: where its origin falls this frame.
// Zoomed, the bracket is drawn with the painter's scale times the zoom: its origin is in those units.
struct Pan {
	int x = 0, y = 0, w = 0, h = 0, viewW = 0, viewH = 0, contentW = 0, sx = 0, sy = 0, maxX = 0, maxY = 0;
	bool barX = false, barY = false;
	float zoom = 1.0f, scale = 1.0f;
	std::shared_ptr<ClipArea> clip;
	int ox() const { return static_cast<int>((x - sx) / zoom); }
	int oy() const { return static_cast<int>((y - sy) / zoom); }
};

Pan BeginPan(Painter &p, const Ctx &c, int x, int y, int w, int h, int contentW, int contentH, PaintOut &out)
{
	Pan v;
	v.x = x;
	v.y = y;
	v.w = w;
	v.h = h;
	v.zoom = std::max(kZoomMin, std::min(kZoomMax, c.u.bracketZoom)) / 100.0f;
	out.pan.contentW = contentW;
	out.pan.contentH = contentH;
	contentW = static_cast<int>(contentW * v.zoom);
	contentH = static_cast<int>(contentH * v.zoom);
	v.contentW = std::max(1, contentW);
	v.barY = contentH > h;
	v.barX = contentW > w - (v.barY ? 14 : 0);
	v.viewW = w - (v.barY ? 14 : 0);
	v.viewH = h - (v.barX ? 14 : 0);
	v.maxX = std::max(0, contentW - v.viewW);
	v.maxY = std::max(0, contentH - v.viewH);
	v.sx = std::min(c.u.bracketX, v.maxX);
	v.sy = std::min(c.u.bracketY, v.maxY);
	SetScrollArea(p, out.bracketArea, x, y, w, h, v.maxY);
	out.bracketArea.maxX = v.maxX;
	out.pan.viewW = v.viewW;
	out.pan.viewH = v.viewH;
	v.clip = std::make_shared<ClipArea>(p, x, y, v.viewW, v.viewH);
	v.scale = p.s;
	p.s = v.scale * v.zoom;
	return v;
}

void EndPan(Painter &p, const Ctx &c, const Pan &v, PaintOut &out)
{
	p.s = v.scale;
	if (v.clip) v.clip->End();
	if (v.barY) PaintScrollbar(p, kBarBracketY, v.x + v.w - 8, v.y, v.viewH, {v.sy, v.maxY, v.viewH}, c.st.dropBar, c.mx, c.my);
	if (v.barX) {
		// A bar along the bottom: its thumb is dragged, the halves either side of it page left and right.
		const int track = v.viewW, thumb = std::max(40, track * v.viewW / v.contentW), at = v.maxX ? (track - thumb) * v.sx / v.maxX : 0;
		const bool hot = c.u.panning && c.u.panMode == 1;
		const bool over = p.Hover(v.x + at, v.y + v.h - 14, thumb, 14, c.mx, c.my);
		p.Fill(v.x, v.y + v.h - 6, track, 4, pal::field);
		p.Fill(v.x + at, v.y + v.h - (hot || over ? 8 : 6), thumb, hot || over ? 6 : 4, hot || over ? pal::edgeHot : pal::lineStrong);
		p.AddHit(v.x, v.y + v.h - 14, at, 14, kTourney, ArgOf(kSubPan, 0));
		p.AddHit(v.x + at + thumb, v.y + v.h - 14, track - at - thumb, 14, kTourney, ArgOf(kSubPan, 1));
		out.pan.valid = true;
		out.pan.thumb = p.R(v.x + at, v.y + v.h - 14, thumb, 14);
		OffsetRect(&out.pan.thumb, p.ox, p.oy);
		out.pan.track = track;
		out.pan.thumbLen = thumb;
	}
	// The zoom: out, the zoom itself (which fits the bracket in its view) and in. Ctrl and the wheel zoom too.
	const int bw = 34, mw = 64, bh = 30, zx = v.x + v.viewW - (2 * bw + mw + 4) - 6, zy = v.y + v.viewH - bh - 8;
	p.Fill(zx - 4, zy - 4, 2 * bw + mw + 12, bh + 8, pal::panel);
	p.raw = true;
	p.Button(zx, zy, bw, bh, "-", kTourney, ArgOf(kSubZoom, 2), c.mx, c.my, c.u.bracketZoom > kZoomMin, false, 3);
	p.Button(zx + bw + 2, zy, mw, bh, std::to_string(c.u.bracketZoom) + " %", kTourney, ArgOf(kSubZoom, 0), c.mx, c.my, true, false, 3);
	p.Button(zx + bw + mw + 4, zy, bw, bh, "+", kTourney, ArgOf(kSubZoom, 1), c.mx, c.my, c.u.bracketZoom < kZoomMax, false, 3);
	p.raw = false;
}

// A column's heading: its round in small capitals and, after it, the series and the map.
// The tip of a round's heading, drawn after the bracket so no box covers it.
struct HeadTip {
	bool show = false;
	int x = 0, y = 0;
	std::string text;
};
HeadTip g_headTip;

// A column's heading: its round in small capitals. How it is played (the series, the map) shows on hovering it.
void PaintColumnHead(Painter &p, const Ctx &c, int x, int y, int w, const std::string &stage, const std::string &detail)
{
	UseFace(p, BrandFace::UiStrong, 12);
	p.raw = true;
	const int stageW = std::min(w, p.CapsWidth(stage, 2) + 4);
	const bool hover = !detail.empty() && p.Hover(x, y, stageW, 20, c.mx, c.my);
	p.TextCaps(x, y, stageW, 20, stage, hover ? pal::redText : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	p.raw = false;
	if (hover) {
		g_headTip.show = true;
		g_headTip.x = x;
		g_headTip.y = y + 24;
		g_headTip.text = detail;
	}
}

void PaintHeadTip(Painter &p)
{
	if (!g_headTip.show) return;
	g_headTip.show = false;
	UseFace(p, BrandFace::Ui, 13);
	p.raw = true;
	const int tw = p.TextWidth(g_headTip.text) + 24;
	p.Fill(g_headTip.x, g_headTip.y, tw, 30, pal::panel);
	p.Frame(g_headTip.x, g_headTip.y, tw, 30, pal::edgeHot);
	p.Text(g_headTip.x + 12, g_headTip.y, tw - 24, 30, g_headTip.text, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
}

std::string BestOfText(int bestOf)
{
	return TrFmt("Bo%s", {std::to_string(bestOf)});
}

// A line from a match to the next one: it leaves in the colour of the match it comes from and arrives in the colour of the one it goes
// into; the upright between them, and a straight line, take the arriving colour only when it marks something (live, the player's).
void PaintConnector(Painter &p, int ox, int oy, const std::vector<TourneySegment> &segs, COLORREF own, COLORREF into)
{
	const COLORREF marked = into == pal::lineStrong || into == kLinePath ? own : into;
	for (size_t i = 0; i < segs.size(); i++) {
		const TourneySegment &s = segs[i];
		const COLORREF ink = segs.size() > 1 && i == 0 ? own : segs.size() > 1 && i + 1 == segs.size() ? into : marked;
		if (s.y1 == s.y2) HLine(p, ox + s.x1, ox + s.x2, oy + s.y1, ink);
		else VLine(p, ox + s.x1, oy + s.y1, oy + s.y2, ink);
	}
}

COLORREF OwnInk(const TourneyMatch &m)
{
	return TourneyMatchOver(m.status) && m.winner ? kLinePath : pal::lineStrong;
}

// The champion at the end of the line out of the last match, which leaves its right side at (fx, fy).
void PaintChampionEnd(Painter &p, Ctx &c, int fx, int fy, bool over)
{
	const TourneyDetail &d = c.d;
	HLine(p, fx, fx + 40, fy, over ? kLinePath : pal::lineStrong);
	const int cx = fx + 40 + kChampW / 2, size = 76;
	const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
	// On the line out of the final: the cup's crown once it has a champion, the gold phalera the champion wins until then.
	if (champ) DrawCupCrown(p, cx - size / 2, fy - size / 2, size, d.official);
	else DrawLeaguePhalera(p, cx - size / 2, fy - size / 2, size, LeagueMetal::Gold);
	const int ty = fy + size / 2 + 8;
	PaintCaps(p, fx + 40, ty, kChampW, 18, "Champion", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	UseFace(p, BrandFace::UiStrong, 15);
	p.raw = true;
	if (champ) p.Text(fx + 40, ty + 22, kChampW, 22, champ->name, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const std::string players = champ && d.Teams() ? TeamPlayersLine(champ->name, champ->members) : std::string();
	if (!players.empty()) {
		UseFace(p, BrandFace::Ui, 12);
		p.Text(fx + 40, ty + 44, kChampW, 18, players, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	p.raw = false;
}

// The boxes of a cup of teams are taller, for each team's name over its two players.
TourneyBracketMetrics BoxMetrics(const TourneyDetail &d)
{
	TourneyBracketMetrics bm;
	bm.top = 46;
	// Room over each box for its state ("Live · 3 min", "Room open"), which is written 21 px above it: never on the box before.
	bm.rowPitch = bm.boxH + 26;
	if (d.Teams()) {
		bm.boxH = 82;
		bm.rowPitch = bm.boxH + 26;
	}
	return bm;
}

void PaintBracket(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const TourneyBracketMetrics bm = BoxMetrics(d);
	const int contentW = TourneyBracketWidth(bm, d.rounds) + 40 + kChampW;
	const int contentH = std::max(TourneyBracketHeight(bm, d.rounds) + 10, d.rounds ? TourneyBoxMidY(bm, d.rounds, 0) + 84 : 0);
	const Pan v = BeginPan(p, c, x, y, w, h, contentW, contentH, out);
	const int ox = v.ox(), oy = v.oy();
	for (int r = 1; r <= d.rounds; r++) {
		const TourneyMatch *first = TourneyMatchAt(d, r, 0);
		const int bestOf = first ? first->bestOf : r == d.rounds ? d.finalBestOf : d.bestOf;
		const TourneyMap map = TourneyMapOfRound(d, r, d.rounds);
		PaintColumnHead(p, c, ox + TourneyBoxX(bm, r), oy, bm.boxW, StageText(TourneyStageOf(r, d.rounds), r), BestOfText(bestOf) + (map.Known() ? " \xC2\xB7 " + MapText(map) : std::string()));
	}
	for (const TourneyMatch &m : d.matches) {
		const std::vector<TourneySegment> segs = TourneyConnector(bm, m.round, m.position, d.rounds);
		if (!segs.empty()) PaintConnector(p, ox, oy, segs, OwnInk(m), LineInk(c, TourneyMatchAt(d, m.round + 1, m.position / 2)));
	}
	for (const TourneyMatch &m : d.matches) PaintBox(p, c, m, ox + TourneyBoxX(bm, m.round), oy + TourneyBoxY(bm, m.round, m.position), bm);
	// The champion, at the end of the final's line.
	if (const TourneyMatch *final = TourneyMatchAt(d, d.rounds, 0)) {
		PaintChampionEnd(p, c, ox + TourneyBoxX(bm, d.rounds) + bm.boxW, oy + TourneyBoxMidY(bm, d.rounds, 0), TourneyMatchOver(final->status));
	}
	PaintHeadTip(p);
	EndPan(p, c, v, out);
}

// Before the draw: the bracket of the cup's places, its boxes empty, filled once the check-in closes.
void PaintBracketPreview(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	TourneyDetail s = c.d;
	s.rounds = TourneyFullRounds(std::max(2, std::max(c.d.maxEntries, static_cast<int>(c.d.entrants.size()))));
	s.matches.clear();
	s.entrants.clear();
	for (int r = 1; r <= s.rounds; r++) {
		for (int k = 0; k < (1 << (s.rounds - r)); k++) {
			TourneyMatch m;
			m.round = r;
			m.position = k;
			m.status = TourneyMatchStatus::Pending;
			m.bestOf = r == s.rounds ? s.finalBestOf : s.bestOf;
			s.matches.push_back(m);
		}
	}
	Ctx sc{c.st, c.u, s, c.mine};
	sc.mx = c.mx;
	sc.my = c.my;
	PaintBracket(p, sc, x, y, w, h, out);
}

// The players beside the bracket: seed, name, and how they stand (signed up, checked in, still in, knocked out) or their place.
void PaintEntrantColumn(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const bool teams = d.Teams() && !d.DrawPending();
	PaintCaps(p, x, y, w - 70, 20, teams ? "Teams" : "Players", pal::caption);
	UseFace(p, BrandFace::Ui, 12);
	p.raw = true;
	p.Text(x + w - 90, y, 90, 20, std::to_string(d.entrants.size()) + (d.maxEntries ? " / " + std::to_string(d.DrawPending() ? d.maxEntries * 2 : d.maxEntries) : std::string()), pal::caption,
		DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	int top = y + 28;
	const int rowH = 46, viewH = y + h - top;
	const int total = static_cast<int>(d.entrants.size()) * rowH, maxY = std::max(0, total - viewH), sy = std::min(c.u.sideScroll, maxY);
	SetScrollArea(p, out.sideArea, x, top, w, viewH, maxY);
	p.Clip(x, top, w, viewH);
	int ry = top - sy;
	for (const TourneyEntrant &e : d.entrants) {
		p.Fill(x, ry, w, 1, pal::hairline);
		if (ry + rowH < top || ry > top + viewH) {
			ry += rowH;
			continue;
		}
		UseFace(p, BrandFace::UiStrong, 12);
		p.raw = true;
		p.Text(x, ry, 22, rowH, e.seed ? std::to_string(e.seed) : std::string("-"), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		const std::vector<TourneyMember> players = TourneyPlayersOf(e);
		int nx = x + 26;
		if (teams) nx += PaintSideDiscs(p, nx, ry + 9, 28, players) + 8;
		else {
			PaintPlayerDisc(p, nx, ry + 9, 28, e.steamId, e.name);
			nx += 36;
		}
		// Before the draw the organizer takes a player off from their row, on hovering it.
		const bool own = e.steamId == SteamOwnId() || TourneyMemberOf(e.members, SteamOwnId());
		const bool before = d.status == TourneyStatus::Signup || d.status == TourneyStatus::Checkin;
		const bool removable = IsOrganizer(d) && before && !own && (e.status == TourneyEntryStatus::Signed || e.status == TourneyEntryStatus::Waitlist) &&
			ry >= top && ry + rowH <= top + viewH;
		const bool busy = removable && LeagueTourneyBusy(TourneyOp::RemoveEntry, e.entryId);
		const bool offer = removable && (busy || p.Hover(x, ry, w, rowH, c.mx, c.my));
		const int place = EntrantPlace(d, e), placeW = offer ? 76 : place ? 40 : 0, nw = x + w - nx - placeW - 4;
		UseFace(p, e.entryId == c.me ? BrandFace::UiStrong : BrandFace::Body, 15);
		const COLORREF ink = e.entryId == c.me ? pal::redText : e.status == TourneyEntryStatus::Out ? pal::caption : pal::text;
		if (teams) {
			p.raw = true;
			p.Text(nx, ry + 4, nw, 22, e.name, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		else PaintName(p, nx, ry + 4, nw, 22, e.name, e.steamId, ink, c.mx, c.my);
		AddIds(c, players);
		const char *status = "Signed up";
		COLORREF sink = pal::caption;
		switch (e.status) {
		case TourneyEntryStatus::PendingPartner:
			status = "Waiting for the partner";
			sink = pal::warn;
			break;
		case TourneyEntryStatus::Waitlist: status = "Waiting list"; break;
		case TourneyEntryStatus::Playing:
			status = "Still in";
			sink = pal::ok;
			break;
		case TourneyEntryStatus::Out: status = "Knocked out"; break;
		case TourneyEntryStatus::Withdrawn: status = "Withdrew"; break;
		case TourneyEntryStatus::Disqualified:
			status = "Disqualified";
			sink = pal::bad;
			break;
		case TourneyEntryStatus::MissedCheckin: status = "Missed the check-in"; break;
		default:
			if (e.checkedIn) {
				status = "Checked in";
				sink = pal::ok;
			}
			break;
		}
		if (*FinishedEntrantText(d, e)) {
			status = FinishedEntrantText(d, e);
			sink = place == 1 ? kGold : pal::caption;
		}
		UseFace(p, BrandFace::Ui, 12);
		p.Text(nx, ry + 25, nw, 16, status, sink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (offer) p.Button(x + w - 72, ry + 9, 72, 28, busy ? "Taking off..." : "Take off", kTourney, ArgOf(kSubRemove, e.entryId), c.mx, c.my, !busy, false, 3);
		else if (place) {
			const int last = TourneyIsDouble(d) ? TourneyDoublePlaceEnd(place) : place;
			UseDisplay(p, 20);
			p.raw = true;
			p.Text(x + w - placeW, ry, placeW, rowH, last > place ? std::to_string(place) + "\xE2\x80\x93" + std::to_string(last) : std::to_string(place),
				place == 1 ? kGold : pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		ry += rowH;
	}
	if (d.entrants.empty()) {
		p.Font(3);
		p.Text(x, top + 8, w, 24, teams ? "No team has signed up yet." : "Nobody has signed up yet.", pal::textDim);
	}
	p.Unclip();
	if (maxY > 0) PaintScrollbar(p, kBarSide, x + w + 6, top, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
}

// A double elimination's column heading: the plain stage, as the title over the bracket says which bracket it is.
std::string DoubleColumnText(int round, int rounds, bool losers)
{
	if (round >= rounds) return Tr("Final");
	if (!losers && round == rounds - 1) return Tr("Semifinal");
	return TrFmt("Round %s", {std::to_string(round)});
}

// The title over each of the two brackets, with a rule along the rest of the width.
void PaintBracketTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	UseFace(p, BrandFace::UiStrong, 13);
	p.raw = true;
	const int titleW = std::min(w, p.CapsWidth(title, 2) + 4);
	p.TextCaps(x, y, titleW, 22, title, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	p.raw = false;
	if (titleW + 16 < w) p.Fill(x + titleW + 16, y + 11, w - titleW - 16, 1, pal::hairline);
}

const TourneyMatch *FirstOf(const TourneyDetail &d, TourneyBracket bracket)
{
	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == bracket) return &m;
	}
	return nullptr;
}

// The winners' bracket on top and the losers' under it; to their right, halfway between the two finals, the grand final, its reset and
// the champion. Before the grand final is played, the reset's place says when it is needed.
void PaintDoubleBracket(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	TourneyBracketMetrics bm = BoxMetrics(d);
	bm.colPitch = bm.boxW + 44;
	const int titleH = 36, gap = 40;
	const TourneyMatch *grand = FirstOf(d, TourneyBracket::Final), *reset = FirstOf(d, TourneyBracket::Reset);
	const bool resetColumn = reset || !grand || !TourneyMatchOver(grand->status) || TourneyResetDue(d);
	const TourneyDoubleLayout l = TourneyDoubleLayoutOf(bm, d.rounds, d.losersRounds, resetColumn, titleH, gap);
	const Pan v = BeginPan(p, c, x, y, w, h, l.champX + kChampW, std::max(l.height, l.finalY + bm.boxH + 70) + 10, out);
	const int ox = v.ox(), oy = v.oy();
	PaintBracketTitle(p, ox, oy + l.winnersY - titleH, l.width, Tr("Winners' bracket"));
	PaintBracketTitle(p, ox, oy + l.losersY - titleH, TourneyBoxX(bm, l.losersRounds) + bm.boxW, Tr("Losers' bracket"));
	for (int r = 1; r <= l.winnersRounds; r++) {
		const TourneyMatch *first = TourneyMatchAt(d, r, 0);
		PaintColumnHead(p, c, ox + TourneyBoxX(bm, r), oy + l.winnersY, bm.boxW, DoubleColumnText(r, l.winnersRounds, false), BestOfText(first ? first->bestOf : d.bestOf));
	}
	for (int r = 1; r <= l.losersRounds; r++) {
		const TourneyMatch *first = TourneyMatchAt(d, r, 0, TourneyBracket::Losers);
		PaintColumnHead(p, c, ox + TourneyBoxX(bm, r), oy + l.losersY, bm.boxW, DoubleColumnText(r, l.losersRounds, true), BestOfText(first ? first->bestOf : d.bestOf));
	}
	const int finalBestOf = grand ? grand->bestOf : d.finalBestOf;
	PaintColumnHead(p, c, ox + l.finalX, oy + l.finalY - 46, bm.boxW, Tr("Grand final"), BestOfText(finalBestOf));
	if (l.reset) PaintColumnHead(p, c, ox + l.resetX, oy + l.finalY - 46, bm.boxW, Tr("Reset match"), BestOfText(reset ? reset->bestOf : finalBestOf));

	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Winners) {
			const std::vector<TourneySegment> segs = TourneyConnector(bm, m.round, m.position, l.winnersRounds);
			if (!segs.empty()) PaintConnector(p, ox, oy + l.winnersY, segs, OwnInk(m), LineInk(c, TourneyMatchAt(d, m.round + 1, m.position / 2)));
		}
		else if (m.bracket == TourneyBracket::Losers) {
			const std::vector<TourneySegment> segs = TourneyLosersConnector(bm, l, m.round, m.position);
			const TourneyMatch *next = TourneyMatchAt(d, m.round + 1, m.round % 2 ? m.position : m.position / 2, TourneyBracket::Losers);
			if (!segs.empty()) PaintConnector(p, ox, oy, segs, OwnInk(m), LineInk(c, next));
		}
	}
	// The two finals into the grand final, and the grand final into its reset.
	const std::vector<TourneySegment> grandLines = TourneyGrandFinalConnector(bm, l);
	const TourneyMatch *winnersFinal = TourneyMatchAt(d, l.winnersRounds, 0), *losersFinal = TourneyMatchAt(d, l.losersRounds, 0, TourneyBracket::Losers);
	const COLORREF intoGrand = grand ? LineInk(c, grand) : pal::lineStrong;
	const COLORREF fromWinners = winnersFinal ? OwnInk(*winnersFinal) : pal::lineStrong, fromLosers = losersFinal ? OwnInk(*losersFinal) : pal::lineStrong;
	for (size_t i = 0; i < grandLines.size(); i++) {
		const TourneySegment &s = grandLines[i];
		COLORREF ink = i == 0 ? fromWinners : i == 1 ? fromLosers : i == 3 ? intoGrand : i == 4 ? (reset ? LineInk(c, reset) : pal::lineStrong) : pal::lineStrong;
		if (i == 2) ink = intoGrand != kLinePath && intoGrand != pal::lineStrong ? intoGrand : fromWinners == kLinePath && fromLosers == kLinePath ? kLinePath : pal::lineStrong;
		if (s.y1 == s.y2) HLine(p, ox + s.x1, ox + s.x2, oy + s.y1, ink);
		else VLine(p, ox + s.x1, oy + s.y1, oy + s.y2, ink);
	}

	for (const TourneyMatch &m : d.matches) {
		if (m.bracket == TourneyBracket::Winners) PaintBox(p, c, m, ox + TourneyBoxX(bm, m.round), oy + TourneyWinnersBoxY(bm, l, m.round, m.position), bm);
		else if (m.bracket == TourneyBracket::Losers) PaintBox(p, c, m, ox + TourneyBoxX(bm, m.round), oy + TourneyLosersBoxY(bm, l, m.round, m.position), bm);
	}
	if (grand) PaintBox(p, c, *grand, ox + l.finalX, oy + l.finalY, bm);
	else {
		TourneyMatch slot;
		slot.bracket = TourneyBracket::Final;
		slot.status = TourneyMatchStatus::Pending;
		PaintBox(p, c, slot, ox + l.finalX, oy + l.finalY, bm);
	}
	if (reset) PaintBox(p, c, *reset, ox + l.resetX, oy + l.finalY, bm);
	else if (l.reset) {
		const int rx = ox + l.resetX, ry = oy + l.finalY;
		PaintDashedFrame(p, rx, ry, bm.boxW, bm.boxH, pal::edgeHot);
		UseFace(p, BrandFace::Ui, 13);
		const std::string when = Tr("Only if the losers' bracket player wins");
		p.raw = true;
		const int th = std::min(bm.boxH - 8, p.Measure(bm.boxW - 24, when));
		p.TextWrapped(rx + 12, ry + (bm.boxH - th) / 2, bm.boxW - 24, th + 2, when, pal::caption);
		p.raw = false;
	}
	PaintChampionEnd(p, c, ox + l.width, oy + l.finalY + bm.boxH / 2, TourneyChampion(d) != 0);
	PaintHeadTip(p);
	EndPan(p, c, v, out);
}

// A player's result in a Swiss round: 'w' won, 'l' lost (a no-show of both is lost by both), 'b' a bye, 'v' being played, 'p' still to
// play; 0 when not paired in it.
char RoundResult(const TourneyDetail &d, int64_t entry, int round)
{
	for (const TourneyMatch &m : d.matches) {
		if (m.bracket != TourneyBracket::Swiss || m.round != round || !m.Has(entry)) continue;
		if (m.status == TourneyMatchStatus::Bye || (!(m.a.Present() && m.b.Present()) && TourneyMatchOver(m.status))) return 'b';
		if (!TourneyMatchOver(m.status)) return m.status == TourneyMatchStatus::Live ? 'v' : 'p';
		return m.winner == entry ? 'w' : 'l';
	}
	return 0;
}

int SwissPaired(const TourneyDetail &d)
{
	int paired = 0;
	for (const TourneyMatch &m : d.matches) paired = std::max(paired, m.round);
	return paired;
}

// The strip over a Swiss tournament's tab: the standings and then each round, the one being played marked in red. The rounds not paired
// yet are there, closed.
void PaintSwissStrip(Painter &p, Ctx &c, int x, int y, int w, int selected)
{
	const TourneyDetail &d = c.d;
	const int total = TourneySwissTotalRounds(d), paired = SwissPaired(d), live = d.status == TourneyStatus::Running ? TourneyCurrentRound(d) : 0;
	const int n = total + 1, gap = 2, h = 40, cw = TourneyStripCellWidth(w, n, gap, 190);
	for (int i = 0; i < n; i++) {
		const int cx = x + i * (cw + gap);
		const bool on = i == selected, open = i == 0 || i <= paired, hot = open && !on && p.Hover(cx, y, cw, h, c.mx, c.my), current = i && i == live;
		p.Fill(cx, y, cw, h, on ? pal::text : hot ? pal::button : pal::field);
		const std::string label = i ? TrFmt("Round %s", {std::to_string(i)}) : Tr("Standings");
		UseFace(p, BrandFace::UiStrong, 15);
		p.raw = true;
		const int dot = current ? 14 : 0, tw = std::min(cw - 16 - dot, p.TextWidth(label) + 2), tx = cx + (cw - tw - dot) / 2;
		if (current) p.Shape(tx, y + h / 2 - 4, 8, 8, InUnitEllipse, pal::red);
		p.Text(tx + dot, y, tw, h, label, on ? pal::onLight : !open ? pal::disabled : hot || current ? pal::text : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (current && !on) p.Fill(cx, y + h - 2, cw, 2, pal::red);
		if (open && !on) p.AddHit(cx, y, cw, h, kTourney, ArgOf(kSubPageTab, i ? kRoundTab + i : 0));
	}
}

// The Swiss standings: place, player, each round's result when there is room, points, wins and losses, Buchholz and byes. The player's
// own row is marked, and the podium coloured once it is over.
void PaintStandings(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const bool over = d.status == TourneyStatus::Finished;
	const int total = TourneySwissTotalRounds(d);
	const TourneyStandingsColumns col = TourneyStandingsColumnsFor(w, total);
	const int numW = col.figureW, byesX = x + col.byes, buchX = x + col.buchholz, wlX = x + col.record, ptsX = x + col.points;
	const int nameX = x + col.name, formW = col.roundsW, formX = x + col.rounds, nameW = col.nameW;
	const bool form = col.showRounds;
	const UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	p.raw = true;
	PaintCaps(p, x + 14, y, 40, 20, "#", pal::caption);
	p.raw = false;
	const bool teams = d.Teams();
	PaintCaps(p, nameX, y, nameW, 20, teams ? "Team" : "Player", pal::caption);
	if (form) PaintCaps(p, formX, y, formW, 20, "Rounds", pal::caption, center);
	PaintCaps(p, ptsX, y, numW, 20, "Points", pal::caption, center);
	PaintCaps(p, wlX, y, numW, 20, "W\xE2\x80\x93L", pal::caption, center);
	PaintCaps(p, buchX, y, col.buchholzW, 20, "Buchholz", pal::caption, center);
	PaintCaps(p, byesX, y, numW, 20, "Byes", pal::caption, center);
	// A team's row holds its name over its players.
	const int foot = over ? 30 : 54, top = y + 30, rowH = teams ? 56 : 46, viewH = h - 30 - foot;
	if (d.standings.empty()) {
		p.Font(0);
		p.Text(x, top + 10, w, 30, "The standings show once the first round is paired.", pal::textDim);
		return;
	}
	const int rows = static_cast<int>(d.standings.size()) * rowH, maxY = std::max(0, rows - viewH), sy = std::min(c.u.bracketY, maxY);
	SetScrollArea(p, out.bracketArea, x, top, w, viewH, maxY);
	ClipArea view(p, x, top, w, viewH);
	int ry = top - sy;
	for (const TourneyStanding &s : d.standings) {
		const bool mine = c.me && s.entryId == c.me;
		if (mine) {
			p.Fill(x, ry, w, rowH, pal::selected);
			p.Fill(x, ry, 3, rowH, pal::red);
		}
		p.Fill(x, ry, w, 1, pal::hairline);
		const bool podium = over && s.place >= 1 && s.place <= 3;
		UseDisplay(p, 24);
		p.raw = true;
		p.Text(x + 14, ry, 44, rowH, s.place ? std::to_string(s.place) : std::string("-"), !podium ? pal::textDim : s.place == 1 ? kGold : s.place == 2 ? kSilver : kBronze,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		const std::vector<TourneyMember> players = !s.members.empty() ? s.members : std::vector<TourneyMember>{{s.steamId, s.name, -1}};
		int nx = nameX + (teams ? PaintSideDiscs(p, nameX, ry + (rowH - 26) / 2, 26, players) : 28) + 12;
		if (!teams) PaintPlayerDisc(p, nameX, ry + 9, 28, s.steamId, s.name);
		if (podium && d.official) {
			DrawLeaguePhalera(p, nx, ry + (rowH - 22) / 2, 22, s.place == 1 ? LeagueMetal::Gold : s.place == 2 ? LeagueMetal::Silver : LeagueMetal::Bronze);
			nx += 30;
		}
		const int youW = mine ? TagWidth(p, "You") + 10 : 0, room = nameW - (nx - nameX) - youW;
		UseFace(p, mine ? BrandFace::UiStrong : BrandFace::Body, 17);
		p.raw = true;
		const int textW = std::min(room, p.TextWidth(s.name) + 2 + (teams ? 0 : LeagueBadgeRoom(s.steamId ? PlayerBadgeOf(s.steamId) : LeagueBadge())));
		p.raw = false;
		const std::string under = teams ? TeamPlayersLine(s.name, players) : std::string();
		if (teams) {
			p.raw = true;
			p.Text(nx, under.empty() ? ry : ry + 6, room, under.empty() ? rowH : 24, s.name, mine ? pal::redText : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (!under.empty()) {
				p.Font(3);
				p.Text(nx, ry + 30, nameW - (nx - nameX), 20, under, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
			p.raw = false;
		}
		else PaintName(p, nx, ry, room, rowH, s.name, s.steamId, mine ? pal::redText : pal::text, c.mx, c.my);
		if (mine) PaintTag(p, nx + textW + 10, (under.empty() ? ry + (rowH - 22) / 2 : ry + 7), "You", pal::red, pal::onRed);
		AddIds(c, players);
		for (int r = 1; form && r <= total; r++) {
			const int fx = x + TourneyRoundSquareX(col, r), fy = ry + rowH / 2 - 8;
			switch (RoundResult(d, s.entryId, r)) {
			case 'w': p.Fill(fx, fy, 16, 16, pal::ok); break;
			case 'l': p.Fill(fx, fy, 16, 16, pal::red); break;
			case 'b':
				p.Fill(fx, fy, 16, 16, kWinBack);
				p.Frame(fx, fy, 16, 16, pal::ok);
				break;
			case 'v':
				p.Frame(fx, fy, 16, 16, pal::red);
				p.Shape(fx + 5, fy + 5, 6, 6, InUnitEllipse, pal::red);
				break;
			case 'p': p.Frame(fx, fy, 16, 16, pal::edgeHot); break;
			default: p.Frame(fx, fy, 16, 16, pal::hairline); break;
			}
		}
		UseDisplay(p, 24);
		p.raw = true;
		p.Text(ptsX, ry, numW, rowH, std::to_string(s.points), pal::text, center);
		UseFace(p, BrandFace::Body, 16);
		p.Text(wlX, ry, numW, rowH, std::to_string(s.wins) + "\xE2\x80\x93" + std::to_string(s.losses), pal::body, center);
		p.Text(buchX, ry, col.buchholzW, rowH, std::to_string(s.buchholz), pal::body, center);
		p.Text(byesX, ry, numW, rowH, std::to_string(s.byes), s.byes ? pal::body : pal::caption, center);
		p.raw = false;
		ry += rowH;
	}
	view.End();
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, top, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
	int ny = y + h - foot + 8;
	p.Font(3);
	p.raw = true;
	if (!over) {
		p.Text(x, ny, w, 22, Tr("Provisional until the last round is over."), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ny += 24;
	}
	p.Text(x, ny, w, 22, Tr("Ties") + ": " + Tr("Buchholz (the points of the opponents met), then head-to-head, then seed"), pal::caption,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
}

// A side of a Swiss pairing: the disc next to the score and the name away from it, with the points the player had before the round.
void PaintPairingSide(Painter &p, Ctx &c, const TourneyMatch &m, const TourneyCard &card, bool right, int x1, int x2, int y, int h)
{
	const TourneyDetail &d = c.d;
	// A team: its players' discs next to the score, and its players before its points under its name.
	const std::vector<TourneyMember> players = TourneyPlayersOf(card);
	const int disc = d.Teams() ? 28 : 32, discsW = d.Teams() ? SideDiscsWidth(disc, players.size()) : disc;
	const int discX = right ? x1 : x2 - discsW, textX1 = right ? x1 + discsW + 12 : x1, textX2 = right ? x2 : x2 - discsW - 12, avail = std::max(40, textX2 - textX1);
	if (d.Teams()) PaintSideDiscs(p, discX, y + (h - disc) / 2, disc, players);
	else PaintPlayerDisc(p, discX, y + (h - disc) / 2, disc, card.steamId, card.name);
	const bool won = TourneyMatchOver(m.status) && m.winner == card.entryId, lost = TourneyMatchOver(m.status) && !won, mine = card.entryId == c.me;
	const COLORREF ink = mine ? pal::redText : won ? pal::text : lost ? pal::caption : pal::textDim;
	UseFace(p, won || mine ? BrandFace::UiStrong : BrandFace::Body, 17);
	p.raw = true;
	const int nameW = std::min(avail, p.TextWidth(card.name) + 2 + (d.Teams() ? 0 : LeagueBadgeRoom(card.steamId ? PlayerBadgeOf(card.steamId) : LeagueBadge())));
	if (d.Teams()) p.Text(right ? textX1 : textX2 - nameW, y + 4, nameW, 26, card.name, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (!d.Teams()) PaintName(p, right ? textX1 : textX2 - nameW, y + 4, nameW, 26, card.name, card.steamId, ink, c.mx, c.my);
	UseFace(p, BrandFace::Ui, 13);
	p.raw = true;
	std::string under = PointsText(TourneySwissPointsBefore(d, card.entryId, m.round));
	if (card.seed) under += " \xC2\xB7 " + TrFmt("Seed %s", {std::to_string(card.seed)});
	const std::string team = d.Teams() ? TeamPlayersLine(card.name, players) : std::string();
	if (!team.empty()) under = team + " \xC2\xB7 " + under;
	p.Text(textX1, y + h - 24, avail, 18, under, pal::caption, (right ? DT_LEFT : DT_RIGHT) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	AddIds(c, players);
}

// A pairing of a Swiss round in a row: its table, the two players either side of the score and, on the right, what is happening in it.
void PaintPairing(Painter &p, Ctx &c, const TourneyMatch &m, int table, int x, int y, int w, int h)
{
	const bool over = TourneyMatchOver(m.status), live = m.status == TourneyMatchStatus::Live;
	const bool mine = c.me && m.Has(c.me), ready = mine && m.status == TourneyMatchStatus::Ready;
	const bool bye = m.status == TourneyMatchStatus::Bye || !(m.a.Present() && m.b.Present());
	p.Fill(x, y, w, h, mine || live ? pal::selected : pal::raised);
	if (bye) PaintDashedFrame(p, x, y, w, h, pal::edgeHot);
	else p.Frame(x, y, w, h, live ? pal::red : ready ? pal::warn : pal::edge);
	UseFace(p, BrandFace::UiStrong, 13);
	p.raw = true;
	const TourneyPairingRow row = TourneyPairingRowFor(w);
	p.Text(x, y, row.tableW, h, std::to_string(table), pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	p.Fill(x + row.tableW, y + 10, 1, h - 20, pal::hairline);
	if (bye) {
		const TourneyCard &who = m.a.Present() ? m.a : m.b;
		if (!who.Present()) return;
		const std::vector<TourneyMember> players = TourneyPlayersOf(who);
		const int discsW = c.d.Teams() ? PaintSideDiscs(p, x + row.left, y + (h - 32) / 2, 32, players) : 32;
		if (!c.d.Teams()) PaintPlayerDisc(p, x + row.left, y + (h - 32) / 2, 32, who.steamId, who.name);
		UseFace(p, BrandFace::Body, 16);
		p.raw = true;
		p.Text(x + row.left + discsW + 12, y, w - row.left - discsW - 28, h, TrFmt(c.d.Teams() ? "%s rest this round: a bye, worth a win" : "%s rests this round: a bye, worth a win", {who.name}),
			who.entryId == c.me ? pal::redText : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		AddIds(c, players);
		return;
	}
	// What is happening in it, on the right.
	COLORREF ink = pal::caption;
	std::string state = MatchStateText(c, m, ink);
	if (state.empty() && !over) state = Tr("To be played");
	const int stateW = row.stateW, stateX = x + row.stateX;
	if (!state.empty()) {
		int tx = stateX;
		if (live) {
			p.Shape(tx, y + h / 2 - 4, 8, 8, InUnitEllipse, pal::red);
			tx += 15;
		}
		UseFace(p, BrandFace::UiStrong, 11);
		p.raw = true;
		p.TextCaps(tx, y, stateX + stateW - tx, h, state, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		p.raw = false;
	}
	const int scoreW = row.scoreW, left = x + row.left, right = x + row.right, scoreX = x + row.scoreX;
	PaintPairingSide(p, c, m, m.a, false, left, scoreX - 14, y, h);
	PaintPairingSide(p, c, m, m.b, true, scoreX + scoreW + 14, right, y, h);
	p.Fill(scoreX, y + 8, scoreW, h - 16, over ? pal::button : pal::field);
	std::string score = "\xE2\x80\x93";
	if (m.status == TourneyMatchStatus::Walkover) score = m.winner == m.a.entryId ? "W \xE2\x80\x93 0" : m.winner == m.b.entryId ? "0 \xE2\x80\x93 W" : "0 \xE2\x80\x93 0";
	else if (over || m.scoreA + m.scoreB > 0) score = std::to_string(m.scoreA) + " \xE2\x80\x93 " + std::to_string(m.scoreB);
	UseDisplay(p, 26);
	p.raw = true;
	p.Text(scoreX, y + 8, scoreW, h - 16, score, over ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	if (live && m.lobby) p.AddHit(x, y, w, h, kTourney, ArgOf(kSubWatch, m.id));
	else if (mine && c.next && c.next->match == m.id) p.AddHit(x, y, w, h, kTourney, ArgOf(kSubMatch, m.id));
}

// A Swiss round: its heading (which of how many, how it stands, its series and map) and its pairings by table.
void PaintSwissRound(Painter &p, Ctx &c, int round, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const std::vector<const TourneyMatch *> list = TourneyRoundMatches(d, round, TourneyBracket::Swiss);
	bool allOver = !list.empty();
	for (const TourneyMatch *m : list) allOver = allOver && TourneyMatchOver(m->status);
	const bool playing = !list.empty() && !allOver && d.status == TourneyStatus::Running;
	UseFace(p, BrandFace::UiStrong, 15);
	p.raw = true;
	const std::string title = TrFmt("Round %s of %s", {std::to_string(round), std::to_string(TourneySwissTotalRounds(d))});
	const int titleW = std::min(w / 2, p.CapsWidth(title, 2) + 4);
	p.TextCaps(x, y, titleW, 26, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	std::string state = playing ? Tr("Being played") : allOver ? Tr("Over") : std::string();
	if (playing && TourneyByRounds(d) && d.roundEndsAt && round == TourneyCurrentRound(d)) state += " \xC2\xB7 " + TrFmt("Deadline: %s", {WhenText(d.roundEndsAt)});
	const TourneyMap map = TourneyMapOfRound(d, round, 0);
	const std::string series = BestOfText(list.empty() ? d.bestOf : list[0]->bestOf) + (map.Known() ? " \xC2\xB7 " + MapText(map) : std::string());
	UseFace(p, BrandFace::Ui, 15);
	const int seriesW = std::min(w / 3, p.TextWidth(series) + 4);
	p.Text(x + titleW + 16, y, w - titleW - 16 - seriesW - 16, 26, state, playing ? pal::redText : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Text(x + w - seriesW, y, seriesW, 26, series, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	const int top = y + 42, rowH = 64, viewH = h - 42;
	if (list.empty()) {
		p.Font(0);
		p.raw = true;
		p.Text(x, top + 6, w, 30, TrFmt("Paired when round %s is over", {std::to_string(round - 1)}), pal::textDim);
		p.raw = false;
		return;
	}
	const int rows = static_cast<int>(list.size()) * rowH - 8, maxY = std::max(0, rows - viewH), sy = std::min(c.u.bracketY, maxY);
	SetScrollArea(p, out.bracketArea, x, top, w, viewH, maxY);
	ClipArea view(p, x, top, w, viewH);
	int ry = top - sy, table = 0;
	for (const TourneyMatch *m : list) {
		PaintPairing(p, c, *m, ++table, x, ry, w, rowH - 8);
		ry += rowH;
	}
	view.End();
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, top, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
}

// A Swiss tournament's tab: the strip, then the standings or the round chosen in it.
void PaintSwiss(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const int round = c.u.pageTab >= kRoundTab ? std::min(c.u.pageTab - kRoundTab, SwissPaired(c.d)) : 0;
	PaintSwissStrip(p, c, x, y, w + 20, round);
	if (round > 0) PaintSwissRound(p, c, round, x, y + 60, w, h - 60, out);
	else PaintStandings(p, c, x, y + 60, w, h - 60, out);
}

void PaintEntrants(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const bool teams = d.Teams();
	PaintCaps(p, x, y, 60, 20, "Seed", pal::caption);
	PaintCaps(p, x + 70, y, w / 2, 20, teams ? "Team" : "Player", pal::caption);
	PaintCaps(p, x + w / 2 + 40, y, w / 4, 20, "Status", pal::caption);
	PaintCaps(p, x + w - 120, y, 120, 20, "Place", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int top = y + 30, rowH = teams ? 54 : 42, viewH = h - 30;
	const int total = static_cast<int>(d.entrants.size()) * rowH, maxY = std::max(0, total - viewH), sy = std::min(c.u.bracketY, maxY);
	SetScrollArea(p, out.bracketArea, x, top, w, viewH, maxY);
	ClipArea view(p, x, top, w, viewH);
	int ry = top - sy;
	for (const TourneyEntrant &e : d.entrants) {
		p.Fill(x, ry, w, 1, pal::hairline);
		UseFace(p, BrandFace::UiStrong, 15);
		p.raw = true;
		p.Text(x, ry, 60, rowH, e.seed ? std::to_string(e.seed) : std::string("-"), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		const std::vector<TourneyMember> players = TourneyPlayersOf(e);
		UseFace(p, e.entryId == c.me ? BrandFace::UiStrong : BrandFace::Body, 17);
		if (teams) {
			const int nx = x + 70 + PaintSideDiscs(p, x + 70, ry + (rowH - 28) / 2, 28, players) + 12, nw = x + w / 2 + 30 - nx;
			const std::string under = TeamPlayersLine(e.name, players);
			p.raw = true;
			p.Text(nx, under.empty() ? ry : ry + 6, nw, under.empty() ? rowH : 24, e.name, e.entryId == c.me ? pal::redText : pal::text,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (!under.empty()) {
				p.Font(3);
				p.Text(nx, ry + 29, nw, 20, under, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
			p.raw = false;
		}
		else {
			PaintPlayerDisc(p, x + 70, ry + 7, 28, e.steamId, e.name);
			PaintName(p, x + 108, ry, w / 2 - 70, rowH, e.name, e.steamId, e.entryId == c.me ? pal::redText : pal::text, c.mx, c.my);
		}
		AddIds(c, players);
		const char *status = "Signed up";
		COLORREF ink = pal::textDim;
		switch (e.status) {
		case TourneyEntryStatus::PendingPartner:
			status = "Waiting for the partner";
			ink = pal::warn;
			break;
		case TourneyEntryStatus::Waitlist: status = "Waiting list"; break;
		case TourneyEntryStatus::Playing:
			status = "Still in";
			ink = pal::ok;
			break;
		case TourneyEntryStatus::Out: status = "Knocked out"; break;
		case TourneyEntryStatus::Withdrawn: status = "Withdrew"; break;
		case TourneyEntryStatus::Disqualified:
			status = "Disqualified";
			ink = pal::bad;
			break;
		case TourneyEntryStatus::MissedCheckin: status = "Missed the check-in"; break;
		default:
			if (e.checkedIn) {
				status = "Checked in";
				ink = pal::ok;
			}
			break;
		}
		const int place = EntrantPlace(d, e);
		if (*FinishedEntrantText(d, e)) {
			status = FinishedEntrantText(d, e);
			ink = place == 1 ? kGold : pal::textDim;
		}
		p.Font(3);
		p.Text(x + w / 2 + 40, ry, w / 4, rowH, status, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (place) {
			const LeagueMetal metal = place == 1 ? LeagueMetal::Gold : place == 2 ? LeagueMetal::Silver : place == 3 ? LeagueMetal::Bronze : LeagueMetal::None;
			if (metal != LeagueMetal::None && d.official) DrawLeaguePhalera(p, x + w - 120, ry + 11, 20, metal);
			// A double elimination shares the places below the fourth: 5-6, 7-8, 9-12...
			const int last = TourneyIsDouble(d) ? TourneyDoublePlaceEnd(place) : place;
			UseDisplay(p, 22);
			p.raw = true;
			p.Text(x + w - 90, ry, 90, rowH, last > place ? std::to_string(place) + "\xE2\x80\x93" + std::to_string(last) : std::to_string(place), pal::text,
				DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		ry += rowH;
	}
	if (d.entrants.empty()) {
		p.Font(0);
		p.Text(x, top + 10, w, 30, teams ? "No team has signed up yet." : "Nobody has signed up yet.", pal::textDim);
	}
	view.End();
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, top, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
}

void PaintRules(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const bool swiss = TourneyIsSwiss(d), twice = TourneyIsDouble(d), byRounds = TourneyByRounds(d);
	// The rules scroll when they are longer than the page, which a short screen and a Swiss or double elimination cup reach.
	const int sy = std::max(0, std::min(c.u.bracketY, c.u.bracketArea.max));
	ClipArea view(p, x, y, w + 20, h);
	int ry = y - sy;
	auto row = [&](const std::string &key, const std::string &value) {
		p.Font(0);
		p.Text(x, ry, 220, 28, key, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = true;
		p.Text(x + 230, ry, w - 230, 28, value, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ry += 32;
	};
	PaintSectionLabel(p, x, ry, w, 22, "Format");
	ry += 32;
	row(Tr(swiss ? "Format" : "Bracket"),
		FormatText(d) + ", " + TrFmt(d.Teams() ? "%s to %s teams" : "%s to %s players", {std::to_string(d.minEntries), std::to_string(d.maxEntries)}));
	if (d.Teams()) {
		row(Tr("Teams"), Tr("A captain and the partner they invite; if either withdraws, the team withdraws"));
		row(Tr("Check-in and room"), Tr("Either player checks the team in; side A's captain opens the room"));
	}
	if (swiss) {
		row(Tr("Rounds"), TrFmt("%s, each between players on the same points, without repeating an opponent", {std::to_string(TourneySwissTotalRounds(d))}));
		row(Tr("Points"), Tr("A win or a bye, 1 point; a loss or a no-show, 0"));
		row(Tr("Ties"), Tr("Buchholz (the points of the opponents met), then head-to-head, then seed"));
		row(Tr("Series"), TrFmt("Best of %s", {std::to_string(d.bestOf)}));
	}
	else if (twice) {
		row(Tr("Losers' bracket"), Tr("Whoever loses in the winners' bracket drops to it; a second loss is out"));
		row(Tr("Grand final"), Tr("If the losers' bracket player wins it, a reset match decides"));
		row(Tr("Series"), TrFmt("Best of %s; the grand final and its reset, best of %s", {std::to_string(d.bestOf), std::to_string(d.finalBestOf)}));
	}
	else row(Tr("Series"), TrFmt("Best of %s; the final, best of %s", {std::to_string(d.bestOf), std::to_string(d.finalBestOf)}));
	row(Tr("Seeding"), Tr(d.seeding == "random" ? "Drawn at random"
			: d.seeding == "manual"					 ? "Set by the organisers"
			: d.Teams()								 ? "By both players' average 2v2 Glory (1v1 without it), frozen when the check-in closes"
													 : "By the ranked leaderboard, frozen when the check-in closes"));
	if (byRounds) {
		row(Tr("Pace"), d.roundDays > 1 ? TrFmt("%s days per round; the next opens when all its matches are played or the time is up", {std::to_string(d.roundDays)})
										: Tr("One round a day; the next opens when all its matches are played or the day is up"));
	}
	else row(Tr("Pace"), Tr("Live, in one evening"));
	row(Tr("Check-in"), TrFmt("From %s to %s; whoever does not check in loses the place", {HourText(d.checkinOpensAt), HourText(d.startsAt)}));
	row(Tr("No-show"), byRounds ? TrFmt("%s min, only in the last 24 h of the round's deadline", {std::to_string(d.noshowMinutes)})
								: TrFmt("%s min from when the match is ready", {std::to_string(d.noshowMinutes)}));
	ry += 10;
	PaintSectionLabel(p, x, ry, w, 22, "Maps and options");
	ry += 32;
	if (swiss || twice) {
		// One map a round, the last one repeating: a Swiss tournament's rounds, or the maps in the order the cup lists them.
		const int rounds = swiss ? TourneySwissTotalRounds(d) : static_cast<int>(d.maps.size());
		for (int r = 1; r <= rounds && !d.maps.empty(); r++) {
			row(rounds > 1 ? TrFmt("Round %s", {std::to_string(r)}) : Tr("Every round"), MapText(TourneyMapOfRound(d, r, 0)));
		}
	}
	else {
		// Before the bracket is drawn, a list of maps by stage is shown for the full bracket of the cup's places.
		const int planned = d.rounds ? d.rounds : d.maps.size() > 1 ? TourneyFullRounds(d.maxEntries) : 0;
		const int rounds = std::max(planned, 1);
		for (int r = 1; r <= rounds && !d.maps.empty(); r++) row(planned ? StageText(TourneyStageOf(r, planned), r) : Tr("Every round"), MapText(TourneyMapOfRound(d, r, planned)));
	}
	if (d.maps.empty()) row(Tr("Maps"), Tr("Chosen in the room"));
	if (!d.seriesPool.empty()) {
		std::string pool;
		for (const TourneyMap &m : d.seriesPool) pool += (pool.empty() ? "" : ", ") + MapText(m);
		row(Tr("Next games of a series"), TrFmt("the loser of the last game picks: %s", {pool}));
	}
	row(Tr("Content"), ContentText(d));
	if (d.mods.set) row(Tr("Gameplay mods"), d.mods.BaseGame() ? Tr("None: the base game") : LeagueModsNames(d.mods.mods));
	const TourneyRoomRules rules = TourneyRulesOf(d.settings);
	row(Tr("Settings"), PresetText(d.settings) + " (" + PresetSummary(rules.preset) + ")");
	row(Tr("Civilizations"), Tr(rules.hideCivs ? "Hidden until the game starts" : "Visible in the room"));
	row(Tr("Map and diplomacy"), Tr(rules.fog ? "fog" : "no fog") + ", " + Tr(rules.explored ? "map explored" : "map unexplored") + ", " +
		Tr(rules.lockedDiplomacy ? "diplomacy locked" : "free diplomacy"));
	const int delay = TourneySpecDelay(d.settings);
	row(Tr("Spectators"), delay ? TrFmt("With a %s s delay", {std::to_string(delay)}) : Tr("No spectators"));
	if (!d.official) row(Tr("Requirements"), RequirementsText(d.requirements) + (d.Teams() && d.requirements.Any() ? " \xC2\xB7 " + Tr("both players") : std::string()));
	if (d.official && d.Teams()) row(Tr("Prizes"), Tr("Phalerae for both players of the podium's teams"));
	ry += 10;
	PaintSectionLabel(p, x, ry, w, 22, "If something fails");
	ry += 32;
	p.Font(0);
	const char *fails = d.Teams() ? "The mod reports the result of every game; if the reports differ, the organisers decide. A whole team can claim the win when a player "
									"of the other team does not come to the room in time; a player who drops is played by the AI and can come back."
		: swiss ? "The mod reports the result of every game; if the two reports differ, the organisers decide. If your opponent does not come to the room in "
				  "time, you can claim the win from your match's screen; if neither comes, neither gets the point."
				: "The mod reports the result of every game; if the two reports differ, the organisers decide. If your opponent does not come to the room in "
				  "time, you can claim the win from your match's screen; if neither comes, both are out.";
	ry += p.TextWrapped(x, ry, w, 80, fails, pal::body);
	view.End();
	const int maxY = std::max(0, ry + sy - y + 10 - h);
	SetScrollArea(p, out.bracketArea, x, y, w, h, maxY);
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, y, h, {std::min(sy, maxY), maxY, h}, c.st.dropBar, c.mx, c.my);
}

const char *LogAction(const std::string &action)
{
	static const std::pair<const char *, const char *> kNames[] = {{"created", "Created"}, {"published", "Published"}, {"checkin_opened", "Check-in opened"},
		{"seeded", "Bracket drawn"}, {"cancelled", "Cancelled"}, {"walkover", "Match given without playing"}, {"withdrew", "A player withdrew"},
		{"disputed", "Result disputed"}, {"resolved", "Dispute resolved"}, {"replay", "Game to be replayed"}, {"finished", "Finished"}};
	for (const auto &n : kNames) {
		if (action == n.first) return n.second;
	}
	return nullptr;
}

void PaintLog(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const int rowH = 36, total = static_cast<int>(d.log.size()) * rowH, maxY = std::max(0, total - h), sy = std::min(c.u.bracketY, maxY);
	SetScrollArea(p, out.bracketArea, x, y, w, h, maxY);
	ClipArea view(p, x, y, w, h);
	int ry = y - sy;
	for (const TourneyLogLine &l : d.log) {
		p.Fill(x, ry + rowH - 1, w, 1, pal::hairline);
		p.Font(3);
		p.raw = true;
		p.Text(x, ry, 170, rowH, DayText(l.at) + " \xC2\xB7 " + HourText(l.at), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		const char *name = LogAction(l.action);
		p.Font(1);
		p.Text(x + 180, ry, 260, rowH, name ? Tr(name) : l.action, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(0);
		const std::string who = l.actor == "system" ? Tr("Automatic") : l.actor == "staff" ? Tr("Organisers") : Tr("A player");
		p.Text(x + 450, ry, w - 450, rowH, who + (l.detail.empty() ? std::string() : "  \xC2\xB7  " + l.detail), pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ry += rowH;
	}
	view.End();
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, y, h, {sy, maxY, h}, c.st.dropBar, c.mx, c.my);
}

// The bar over the page when the player has something to do in this tournament now.
bool PaintBanner(Painter &p, Ctx &c, int x, int y, int w)
{
	const TourneyDetail &d = c.d;
	const TourneyMyEntry *e = MyEntry(c.mine, d.id);
	std::string title, text;
	std::vector<std::pair<std::string, int>> buttons;
	int strong = -1;
	if (c.next && c.next->status == TourneyMatchStatus::Ready) {
		const TourneyNext &n = *c.next;
		const int64_t left = n.deadlineAt - NowMs();
		const TourneyMatch *match = MatchById(d, n.match);
		title = TrFmt("Your %s match is ready.", {match ? MatchStageText(d, *match) : StageText(n.stage, n.round)});
		if (n.canClaimNoShow) {
			text = NoShowClaimText(n);
			buttons.push_back({Tr("Claim the win"), ArgOf(kSubNoShow, n.match)});
		}
		// Paced by rounds, the match waits until the round's deadline, days away: its day and hour, not a countdown.
		else if (TourneyByRounds(d) && left > 0) text = TrFmt("Deadline: %s", {WhenText(n.deadlineAt)});
		else if (n.opponent.inRoom && left > 0 && n.Teams()) {
			text = TrFmt("%s are already in the room. If your team is not complete within %s, they can claim the win by no-show.", {n.opponent.name, TourneyClock(left)});
		}
		else if (n.opponent.inRoom && left > 0) text = TrFmt("%s is already in the room. If you don't enter within %s, they can claim the win by no-show.", {n.opponent.name, TourneyClock(left)});
		else if (left > 0 && n.Teams()) text = TrFmt("Your team has to be complete in the room within %s, or the match counts as a no-show.", {TourneyClock(left)});
		else if (left > 0) text = TrFmt("Enter the room within %s, or the match counts as a no-show.", {TourneyClock(left)});
		else text = Tr("The waiting time is over: enter the room now.");
		// One way in: the match's page, where the civilization and the colour are chosen and Ready takes the player to the room.
		buttons.push_back({Tr("Your match"), ArgOf(kSubMatch, n.match)});
		strong = static_cast<int>(buttons.size()) - 1;
	}
	else if (c.next && c.next->status == TourneyMatchStatus::Live) {
		title = Tr("Your match is being played.");
	}
	else if (c.next && c.next->status == TourneyMatchStatus::Disputed) {
		title = Tr("Your match is under review.");
		text = Tr("The organisers are looking at the reports of the game.");
	}
	else if (d.status == TourneyStatus::Checkin && e && TourneyEntryActive(e->status) && !e->checkedIn) {
		title = Tr("The check-in is open.");
		text = TrFmt(d.Teams() ? "Confirm before %s that your team is playing, or it loses its place: either of you can do it."
							   : "Confirm before %s that you are playing, or you lose your place.",
			{HourText(d.startsAt)});
		buttons.push_back({Tr("Check in"), ArgOf(kSubCheckIn, d.id)});
		strong = 0;
	}
	else if (const TourneyInvite *invite = (d.status == TourneyStatus::Signup || d.status == TourneyStatus::Checkin) && !(e && TourneyEntryActive(e->status))
				 ? TourneyInviteOf(c.mine.mine, d.id)
				 : nullptr) {
		title = Tr("Invitation to a team.");
		text = InviteText(*invite);
		if (!InviteBusy(d.id)) {
			buttons.push_back({Tr("Decline"), ArgOf(kSubInviteDecline, d.id)});
			buttons.push_back({Tr("Accept"), ArgOf(kSubInviteAccept, d.id)});
			strong = 1;
		}
	}
	else return false;
	const int h = 64;
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::edge);
	p.Fill(x, y, 4, h, pal::red);
	int bx = x + w - 12;
	for (int i = static_cast<int>(buttons.size()) - 1; i >= 0; i--) {
		p.Font(1);
		p.raw = true;
		const int bw = p.CapsWidth(buttons[static_cast<size_t>(i)].first, 1) + 48;
		bx -= bw;
		p.Button(bx, y + 13, bw, 38, buttons[static_cast<size_t>(i)].first, kTourney, buttons[static_cast<size_t>(i)].second, c.mx, c.my, true, i == strong);
		p.raw = false;
		bx -= 10;
	}
	UseFace(p, BrandFace::UiStrong, 17);
	p.raw = true;
	const int titleW = std::min(bx - x - 24, p.TextWidth(title) + 4);
	p.Text(x + 24, y, titleW, h, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(0);
	p.Text(x + 24 + titleW + 14, y, bx - (x + 24 + titleW + 14) - 10, h, text, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	return true;
}

int PaintStatusCard(Painter &p, Ctx &c, int x, int y, int w)
{
	const TourneyDetail &d = c.d;
	const int pad = 22, mx = c.mx, my = c.my;
	const TourneyMyEntry *e = MyEntry(c.mine, d.id);
	const bool in = e && TourneyEntryActive(e->status);
	if (d.status == TourneyStatus::Signup || d.status == TourneyStatus::Checkin) {
		const bool reqs = d.requirements.Any(), teams = d.Teams() && !d.teamDraw;
		const TourneyInvite *invite = !in && teams ? TourneyInviteOf(c.mine.mine, d.id) : nullptr;
		const bool pending = !in && !invite && e && e->Pending();
		const TourneyUnmet unmet = in || invite || pending || (IsOrganizer(d) && !teams) ? TourneyUnmet::None : CupUnmet(d.requirements, c.mine);
		// The cup's gameplay mods: they count to sign up and, once in, until the check-in is done.
		const LeagueModsCheck modsFit = LeagueModsFitNow(d.mods);
		const LeagueModsPrepView prep = LeagueModsPrepState("tourney:" + std::to_string(d.id));
		const bool modsNeeded = modsFit.fit != LeagueModsFit::Ok && !invite && !pending && unmet == TourneyUnmet::None &&
			(!in || (d.status == TourneyStatus::Checkin && !e->checkedIn));
		// What the player's state is in the cup, which can take a few lines: the card grows with it.
		std::string status;
		COLORREF ink = pal::ok;
		if (in && teams) {
			status = e->status == TourneyEntryStatus::Waitlist ? Tr("Your team is on the waiting list.") : e->checkedIn ? Tr("Your team is checked in.") : Tr("Your team is signed up.");
			const TourneyEntrant *team = TourneyEntrantBySteamId(d, SteamOwnId());
			for (const TourneyMember &m : team ? team->members : std::vector<TourneyMember>()) {
				if (m.steamId != SteamOwnId() && !m.name.empty()) status += " " + TrFmt("You play with %s.", {m.name});
			}
		}
		else if (in) status = e->status == TourneyEntryStatus::Waitlist ? Tr("You are on the waiting list.") : e->checkedIn ? Tr("You are checked in.") : Tr("You are signed up.");
		else if (invite) {
			status = InviteText(*invite);
			ink = pal::redText;
		}
		else if (pending) {
			// The page names the partner the team waits for; the player's own tournaments don't.
			std::string partner;
			for (const TourneyEntrant &x2 : d.entrants) {
				if (x2.status == TourneyEntryStatus::PendingPartner && x2.steamId == SteamOwnId()) partner = x2.partner.name;
			}
			status = PendingText(partner) + ". " + Tr("Until they accept, your team takes no place.");
			ink = pal::warn;
		}
		else if (unmet != TourneyUnmet::None) {
			status = UnmetText(d.requirements, unmet, c.mine);
			ink = pal::bad;
		}
		if (modsNeeded) {
			status = LeagueModsProblemText(d.mods, modsFit) + " " + Tr("Preparing them restarts the game.");
			ink = pal::bad;
		}
		p.Font(3);
		const int sh = std::min(88, p.Measure(w - 2 * pad, status));
		// The staging server only: the cup filled with test bots, to try it alone or with a friend. A second button under the card's own.
		const bool bots = d.testBots && LeagueLinked();
		const std::string botsNote = bots ? Tr("Only on the test server: the bots take the free places; the game's AI plays against you.") : std::string();
		p.raw = true;
		const int bh = bots ? std::min(44, p.Measure(w - 2 * pad, botsNote)) : 0;
		p.raw = false;
		const int extra = bots ? 12 + bh + 8 + 40 : 0;
		const int h = (reqs ? 262 : 236) + (d.mods.set ? 26 : 0) + std::max(0, sh - 44) + extra;
		PaintGlassCard(p, x, y, w, h);
		PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, d.status == TourneyStatus::Checkin ? "Check-in" : "Sign-up", pal::caption);
		UseFace(p, BrandFace::UiStrong, 18);
		p.raw = true;
		p.Text(x + pad, y + 48, w - 2 * pad, 26, SignedUpText(d, d.entries, d.maxEntries), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		p.Fill(x + pad, y + 82, w - 2 * pad, 6, pal::button);
		if (d.maxEntries) p.Fill(x + pad, y + 82, (w - 2 * pad) * std::min(d.entries, d.maxEntries) / d.maxEntries, 6, pal::red);
		p.Font(3);
		p.raw = true;
		const std::string window = d.status == TourneyStatus::Checkin ? TrFmt("Check-in open until %s", {HourText(d.startsAt)})
																	   : TrFmt("Check-in %s, from %s to %s", {DayText(d.checkinOpensAt), HourText(d.checkinOpensAt), HourText(d.startsAt)});
		p.Text(x + pad, y + 98, w - 2 * pad, 22, window, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		int ly = y + 122;
		if (reqs) {
			const std::string both = teams ? " \xC2\xB7 " + Tr("both players") : std::string();
			p.Text(x + pad, ly, w - 2 * pad, 22, Tr("Requirements") + ": " + RequirementsText(d.requirements) + both, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			ly += 26;
		}
		if (d.mods.set) {
			const bool progress = !prep.line.empty();
			const COLORREF modsInk = progress ? (prep.failed ? pal::bad : pal::ok) : modsFit.fit == LeagueModsFit::Ok ? pal::textDim : pal::bad;
			p.Text(x + pad, ly, w - 2 * pad, 22, progress ? prep.line : LeagueModsRuleText(d.mods), modsInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			ly += 26;
		}
		p.Font(3);
		p.raw = true;
		p.TextWrapped(x + pad, ly, w - 2 * pad, sh + 2, status, ink);
		p.raw = false;
		const int by = y + h - 22 - 44 - extra, bw = w - 2 * pad;
		if (!LeagueLinked()) p.Button(x + pad, by, bw, 44, "Link to sign up", kLeagueLink, 0, mx, my, true, true);
		else if (invite) {
			const int half = (bw - 10) / 2;
			const bool busy = InviteBusy(d.id);
			p.Button(x + pad, by, half, 44, "Accept", kTourney, ArgOf(kSubInviteAccept, d.id), mx, my, !busy, true);
			p.Button(x + pad + half + 10, by, bw - half - 10, 44, "Decline", kTourney, ArgOf(kSubInviteDecline, d.id), mx, my, !busy);
		}
		else if (pending) {
			const bool busy = LeagueTourneyBusy(TourneyOp::CancelTeam, d.id);
			p.Button(x + pad, by, bw, 44, busy ? "Cancelling..." : "Cancel", kTourney, ArgOf(kSubTeamWithdraw, d.id), mx, my, !busy);
		}
		else if (modsNeeded && !in) {
			p.Button(x + pad, by, bw, 44, "Prepare mods (restarts the game)", kTourney, ArgOf(kSubPrepareMods, d.id), mx, my, !prep.busy, true);
		}
		else if (modsNeeded) {
			const int half = (bw - 10) / 2;
			p.Button(x + pad, by, half, 44, "Prepare mods", kTourney, ArgOf(kSubPrepareMods, d.id), mx, my, !prep.busy, true);
			p.Button(x + pad + half + 10, by, bw - half - 10, 44, "Withdraw", kTourney, ArgOf(kSubWithdraw, d.id), mx, my);
		}
		else if (!in) {
			const bool busy = LeagueTourneyBusy(TourneyOp::SignUp, d.id);
			p.Button(x + pad, by, bw, 44, busy ? "Signing up..." : teams ? "Sign up my team" : "Sign up", kTourney, ArgOf(teams ? kSubTeamOpen : kSubSignUp, d.id), mx, my,
				!busy && unmet == TourneyUnmet::None, true);
		}
		else if (d.status == TourneyStatus::Checkin && !e->checkedIn) {
			const int half = (bw - 10) / 2;
			const bool busy = LeagueTourneyBusy(TourneyOp::CheckIn, d.id);
			p.Button(x + pad, by, half, 44, busy ? "Checking in..." : "Check in", kTourney, ArgOf(kSubCheckIn, d.id), mx, my, !busy, true);
			p.Button(x + pad + half + 10, by, bw - half - 10, 44, "Withdraw", kTourney, ArgOf(kSubWithdraw, d.id), mx, my);
		}
		else p.Button(x + pad, by, bw, 44, "Withdraw", kTourney, ArgOf(kSubWithdraw, d.id), mx, my, !LeagueTourneyBusy(TourneyOp::Withdraw, d.id));
		if (bots) {
			const int ty = by + 44 + 12;
			p.Font(3);
			p.raw = true;
			p.TextWrapped(x + pad, ty, bw, bh + 2, botsNote, pal::textDim);
			p.raw = false;
			const bool busy = LeagueTourneyBusy(TourneyOp::Bots, d.id);
			p.Button(x + pad, ty + bh + 8, bw, 40, busy ? "Filling with bots..." : "Fill with bots (test)", kTourney, ArgOf(kSubFillBots, d.id), mx, my, !busy);
		}
		return h;
	}
	if (d.status == TourneyStatus::Finished) {
		const int h = 224;
		PaintGlassCard(p, x, y, w, h);
		PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, "Champion", kGold);
		const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
		DrawCupCrown(p, x + pad, y + 54, 64, d.official);
		UseDisplay(p, 40);
		p.raw = true;
		p.TextCaps(x + pad + 80, y + 54, w - 2 * pad - 80, 64, champ ? champ->name : Tr("No champion"), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		p.Button(x + pad, y + h - 22 - 44, w - 2 * pad, 44, "See the result", kTourney, ArgOf(kSubChampion, d.id), mx, my, true, true);
		return h;
	}
	// Being played: the match on now, to watch.
	const TourneyMatch *live = nullptr;
	for (const TourneyMatch &m : d.matches) {
		if (m.status == TourneyMatchStatus::Live && !live) live = &m;
	}
	const int h = live ? 250 : 120;
	PaintGlassCard(p, x, y, w, h);
	p.Shape(x + pad, y + 26, 8, 8, InUnitEllipse, live ? pal::red : pal::disabled);
	PaintCaps(p, x + pad + 18, y + 20, w - 2 * pad, 20, live ? Tr("Live") + " \xC2\xB7 " + MatchStageText(d, *live) : Tr("Live"), live ? pal::redText : pal::caption);
	if (!live) {
		p.Font(0);
		p.Text(x + pad, y + 52, w - 2 * pad, 26, "No game is being played right now.", pal::textDim);
		return h;
	}
	int cy = y + 54;
	for (const TourneyCard *card : {&live->a, &live->b}) {
		UseFace(p, BrandFace::UiStrong, 18);
		if (d.Teams()) {
			const int dw = PaintSideDiscs(p, x + pad, cy, 32, TourneyPlayersOf(*card)) + 12;
			p.raw = true;
			p.Text(x + pad + dw, cy, w - 2 * pad - dw - 90, 32, card->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		else {
			PaintPlayerDisc(p, x + pad, cy, 32, card->steamId, card->name);
			PaintName(p, x + pad + 44, cy, w - 2 * pad - 44 - 90, 32, card->name, card->steamId, pal::text, mx, my);
		}
		UseFace(p, BrandFace::UiStrong, 11);
		p.raw = true;
		p.TextCaps(x + w - pad - 90, cy, 90, 32, TrFmt("Seed %s", {std::to_string(card->seed)}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 2);
		p.raw = false;
		cy += 40;
	}
	p.Font(3);
	p.raw = true;
	const TourneyMap map = MatchMap(d, *live);
	p.Text(x + pad, cy + 4, w - 2 * pad, 20, (map.Known() ? MapText(map) + " \xC2\xB7 " : std::string()) + (live->startedAt ? TrFmt("%s of play", {DurationText(NowMs() - live->startedAt)}) : std::string()),
		pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Text(x + pad, cy + 26, w - 2 * pad, 20, (TourneySpecDelay(d.settings) ? TrFmt("Spectators with a %s s delay", {std::to_string(TourneySpecDelay(d.settings))}) : Tr("No spectators")), pal::textDim,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	p.Button(x + pad, y + h - 20 - 40, w - 2 * pad, 40, "Watch live", kTourney, ArgOf(kSubWatch, live->id), mx, my, live->lobby != 0);
	return h;
}

// The organizer's tab: the players, who can be taken off before the bracket, and the disputes to settle.
void PaintOrganizer(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	const int mx = c.mx, my = c.my;
	// Reached from the organizer's card when a result is disputed: the players are on the bracket's tab.
	const int rx = x, rw = w;

	// The disputes.
	const TourneyDisputesView &disputes = c.disputes;
	PaintSectionLabel(p, rx, y, rw, 22, "Disputes to settle");
	const int dTop = y + 32, cardH = 168, dViewH = y + h - dTop, bg = 8, pw = rw - 32;
	if (disputes.disputes.empty() && !disputes.loaded && !disputes.failed) {
		for (int k = 0; k < 2 && dTop + k * (cardH + 12) + cardH <= y + h; k++) PaintSkeleton(p, rx, dTop + k * (cardH + 12), rw, cardH);
		return;
	}
	if (disputes.disputes.empty()) {
		p.Font(0);
		const char *text = !disputes.loaded ? "Couldn't reach the league server. Check your connection and try again."
											 : "No result is disputed. When a player disputes one, it shows here with the buttons to settle it.";
		p.TextWrapped(rx, dTop + 6, rw, 80, text, pal::textDim);
		return;
	}
	// The four answers to a dispute go on one row when their labels fit it, else two by two, and the card grows by a row.
	auto answers = [&](const TourneyMatch &m) {
		return std::vector<std::string>{TrFmt("Game to %s", {m.a.name}), TrFmt("Game to %s", {m.b.name}), Tr("Play it again"), Tr("Walkover")};
	};
	std::vector<int> heights;
	std::vector<bool> stacked;
	int dTotal = 0;
	for (const TourneyDispute &q : disputes.disputes) {
		const TourneyMatch *m = MatchById(d, q.matchId);
		int need = 3 * bg;
		if (m && !OrganizerPlays(d, *m)) {
			p.raw = true;
			for (const std::string &label : answers(*m)) need += p.ButtonWidth(label, 0, 3);
			p.raw = false;
		}
		stacked.push_back(need > pw);
		heights.push_back(cardH + (stacked.back() ? 36 + bg : 0));
		dTotal += heights.back() + 12;
	}
	const int dMax = std::max(0, dTotal - dViewH), ds = std::min(c.u.sideScroll, dMax);
	SetScrollArea(p, out.sideArea, rx, dTop, rw, dViewH, dMax);
	ClipArea disputesView(p, rx, dTop, rw, dViewH);
	int cy = dTop - ds;
	for (size_t qi = 0; qi < disputes.disputes.size(); qi++) {
		const TourneyDispute &q = disputes.disputes[qi];
		const TourneyMatch *m = MatchById(d, q.matchId);
		const int ch = heights[qi];
		if (cy >= dTop + dViewH || cy + ch <= dTop) {
			cy += ch + 12;
			continue;
		}
		p.Fill(rx, cy, rw, ch, pal::raised);
		p.Frame(rx, cy, rw, ch, pal::edge);
		p.Fill(rx, cy, 3, ch, pal::warn);
		const int px = rx + 16;
		UseFace(p, BrandFace::UiStrong, 16);
		p.raw = true;
		const std::string title = m ? MatchStageText(d, *m) + "  \xC2\xB7  " + TrFmt("%s vs %s", {m->a.name, m->b.name}) : TrFmt("Match %s", {std::to_string(q.matchId)});
		p.Text(px, cy + 10, pw, 24, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		std::string who = q.openedByName;
		if (who.empty() && q.openedBy) {
			const TourneyEntrant *e = TourneyEntrantBySteamId(d, q.openedBy);
			const TourneyMember *m2 = e ? TourneyMemberOf(e->members, q.openedBy) : nullptr;
			if (m2 && !m2->name.empty()) who = d.Teams() ? m2->name + " (" + e->name + ")" : m2->name;
			else if (e) who = e->name;
		}
		p.Font(3);
		p.Text(px, cy + 34, pw, 20, TrFmt("Disputed by %s \xC2\xB7 %s", {who.empty() ? Tr("a player") : who, WhenText(q.createdAt)}), pal::caption,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(0);
		p.TextWrapped(px, cy + 58, pw, 48, "\xE2\x80\x9C" + q.text + "\xE2\x80\x9D", pal::body);
		p.raw = false;
		const int by = cy + ch - 16 - 36 - (stacked[qi] ? 36 + bg : 0);
		if (!m || OrganizerPlays(d, *m)) {
			p.Font(3);
			p.Text(px, by, pw, 36, m ? "You play this match: the league's team settles it." : "This match is not in the bracket any more.", pal::warn,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		else {
			const bool busy = LeagueTourneyBusy(TourneyOp::Resolve, m->id);
			const std::vector<std::string> labels = answers(*m);
			const int subs[] = {kSubGameA, kSubGameB, kSubReplay, kSubWalkover};
			p.raw = true;
			if (stacked[qi]) {
				const int bw = (pw - bg) / 2;
				for (int i = 0; i < 4; i++) {
					const int bx = px + (i % 2) * (bw + bg), bw2 = i % 2 ? pw - bw - bg : bw;
					p.Button(bx, by + (i / 2) * (36 + bg), bw2, 36, labels[i], kTourney, ArgOf(subs[i], m->id), mx, my, !busy, false, 3);
				}
			}
			else {
				// Each takes its label's width and an equal share of what is left.
				int widths[4], used = 3 * bg;
				for (int i = 0; i < 4; i++) used += widths[i] = p.ButtonWidth(labels[i], 0, 3);
				int bx = px;
				for (int i = 0; i < 4; i++) {
					const int bw = i < 3 ? widths[i] + (pw - used) / 4 : px + pw - bx;
					p.Button(bx, by, bw, 36, labels[i], kTourney, ArgOf(subs[i], m->id), mx, my, !busy, false, 3);
					bx += bw + bg;
				}
			}
			p.raw = false;
		}
		cy += ch + 12;
	}
	disputesView.End();
	if (dMax > 0) PaintScrollbar(p, kBarSide, rx + rw + 8, dTop, dViewH, {ds, dMax, dViewH}, c.st.dropBar, mx, my);
}

// The organizer's card beside the page: the code of a private cup, where the cup stands and what they can do now.
int PaintOrganizerCard(Painter &p, Ctx &c, int x, int y, int w)
{
	const TourneyDetail &d = c.d;
	const int pad = 22, mx = c.mx, my = c.my, bw = w - 2 * pad;
	const TourneyOrganized *own = TourneyOrganizedOf(c.mine.mine, d.id);
	std::string code = LeagueTourneyCodeOf(d.slug);
	if (own && code.empty()) code = own->inviteCode;
	int signedUp = 0, checkedIn = 0;
	for (const TourneyEntrant &e : d.entrants) {
		if (e.status != TourneyEntryStatus::Signed && e.status != TourneyEntryStatus::Playing) continue;
		signedUp++;
		checkedIn += e.checkedIn ? 1 : 0;
	}
	const TourneyDisputesView &open = c.disputes;
	const int disputes = open.loaded ? static_cast<int>(open.disputes.size()) : own ? own->openDisputes : 0;
	const bool on = d.status == TourneyStatus::Signup || d.status == TourneyStatus::Checkin || d.status == TourneyStatus::Running;
	const int codeH = d.isPrivate && !code.empty() ? 96 : 0, buttons = !on ? 0 : d.status == TourneyStatus::Running ? (disputes ? 2 : 1) : 2;
	const int h = 20 + 26 + codeH + 64 + buttons * 50 + 10;
	PaintGlassCard(p, x, y, w, h);
	p.Fill(x, y, 4, h, pal::red);
	PaintCaps(p, x + pad, y + 20, bw, 20, "You organise this cup", pal::redText);
	int cy = y + 50;
	if (codeH) {
		PaintCaps(p, x + pad, cy, bw, 18, "Invitation code", pal::caption);
		UseDisplay(p, 44);
		p.raw = true;
		p.TextCaps(x + pad, cy + 20, bw - 110, 54, code, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 6);
		p.raw = false;
		p.Button(x + w - pad - 100, cy + 28, 100, 38, "Copy", kTourney, ArgOf(kSubCopyCode, d.id), mx, my);
		cy += codeH;
	}
	std::string state, detail;
	switch (d.status) {
	case TourneyStatus::Signup:
		state = SignedUpText(d, signedUp, d.maxEntries);
		detail = TrFmt(d.Teams() ? "Check-in from %s; at least %s teams" : "Check-in from %s; at least %s players", {WhenText(d.checkinOpensAt), std::to_string(d.minEntries)});
		break;
	case TourneyStatus::Checkin:
		state = TrFmt("%s of %s checked in", {std::to_string(checkedIn), std::to_string(signedUp)});
		detail = checkedIn >= d.minEntries ? TrFmt("You can start it now; it starts on its own at %s.", {HourText(d.startsAt)})
										   : TrFmt("%s are needed to start; the check-in closes at %s.", {std::to_string(d.minEntries), HourText(d.startsAt)});
		break;
	case TourneyStatus::Running:
		state = Tr("Being played");
		detail = disputes == 1 ? Tr("1 dispute to settle.") : disputes > 1 ? TrFmt("%s disputes to settle.", {std::to_string(disputes)}) : Tr("No dispute to settle.");
		break;
	case TourneyStatus::Finished:
		state = Tr("Over");
		detail = Tr("Thank you for organising it.");
		break;
	default:
		state = Tr("Cancelled");
		detail = Tr("The cup is cancelled.");
		break;
	}
	UseFace(p, BrandFace::UiStrong, 18);
	p.raw = true;
	p.Text(x + pad, cy, bw, 26, state, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(x + pad, cy + 28, bw, 22, detail, disputes && d.status == TourneyStatus::Running ? pal::warn : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	cy += 64;
	const bool cancelling = LeagueTourneyBusy(TourneyOp::Cancel, d.id);
	if (d.status == TourneyStatus::Signup) {
		const bool busy = LeagueTourneyBusy(TourneyOp::CheckinNow, d.id);
		p.Button(x + pad, cy, bw, 40, busy ? "Opening..." : "Open the check-in now", kTourney, ArgOf(kSubCheckinNow, d.id), mx, my, !busy, true);
		cy += 50;
	}
	else if (d.status == TourneyStatus::Checkin) {
		const bool busy = LeagueTourneyBusy(TourneyOp::StartNow, d.id);
		p.Button(x + pad, cy, bw, 40, busy ? "Starting..." : "Start now", kTourney, ArgOf(kSubStartNow, d.id), mx, my, !busy && checkedIn >= d.minEntries, true);
		cy += 50;
	}
	else if (d.status == TourneyStatus::Running && disputes) {
		p.Button(x + pad, cy, bw, 40, "Settle the disputes", kTourney, ArgOf(kSubPageTab, 3), mx, my, c.u.pageTab != 3, true);
		cy += 50;
	}
	if (on) {
		const std::string cancel = Tr(cancelling ? "Cancelling..." : "Cancel the cup");
		if (d.status == TourneyStatus::Running) PaintSecondButton(p, x + pad, cy, bw, 40, cancel, ArgOf(kSubCancelCup, d.id), !cancelling, mx, my);
		else {
			const int half = (bw - 10) / 2;
			const bool editing = LeagueTourneyBusy(TourneyOp::Edit, d.id);
			PaintSecondButton(p, x + pad, cy, half, 40, Tr("Edit"), ArgOf(kSubEdit, d.id), !editing && !cancelling, mx, my);
			PaintSecondButton(p, x + pad + half + 10, cy, bw - half - 10, 40, cancel, ArgOf(kSubCancelCup, d.id), !cancelling, mx, my);
		}
	}
	return h;
}

int PaintProgressCard(Painter &p, Ctx &c, int x, int y, int w)
{
	const TourneyDetail &d = c.d;
	// A Swiss tournament or one paced by rounds: the round open now and, with a deadline, until when, on a line of its own.
	const int round = d.status == TourneyStatus::Running ? TourneyCurrentRound(d) : 0;
	const bool rounds = round > 0 && (TourneyIsSwiss(d) || TourneyByRounds(d)), deadline = rounds && TourneyByRounds(d) && d.roundEndsAt;
	const int pad = 22, h = deadline ? 174 : 150;
	PaintGlassCard(p, x, y, w, h);
	PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, "Progress", pal::caption);
	int done = 0, total = 0;
	TourneyProgress(d, done, total);
	UseFace(p, BrandFace::UiStrong, 18);
	p.raw = true;
	p.Text(x + pad, y + 50, w - 2 * pad, 26, total ? TrFmt("%s of %s matches", {std::to_string(done), std::to_string(total)}) : Tr("The bracket is not drawn yet"), pal::text);
	p.Font(3);
	if (total) p.Text(x + pad, y + 50, w - 2 * pad, 26, std::to_string(done * 100 / total) + " %", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	p.Fill(x + pad, y + 86, w - 2 * pad, 6, pal::button);
	if (total) p.Fill(x + pad, y + 86, (w - 2 * pad) * done / total, 6, pal::red);
	std::string when = d.status == TourneyStatus::Running || d.status == TourneyStatus::Finished ? TrFmt("Started at %s", {HourText(d.startsAt)}) : TrFmt("Starts %s", {WhenText(d.startsAt)});
	if (rounds) when = TourneyIsSwiss(d) ? TrFmt("Round %s of %s", {std::to_string(round), std::to_string(TourneySwissTotalRounds(d))}) : TrFmt("Round %s", {std::to_string(round)});
	// A cup of one evening still to end: when it should, from its format and those in it.
	const int64_t end = d.status != TourneyStatus::Finished && d.status != TourneyStatus::Cancelled ? TourneyEstimatedEnd(d) : 0;
	if (!rounds && end > NowMs()) when += "  \xC2\xB7  " + TrFmt("estimated end around %s", {HourText(end)});
	p.Font(3);
	p.raw = true;
	p.Text(x + pad, y + 104, w - 2 * pad, 22, when, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (deadline) p.Text(x + pad, y + 128, w - 2 * pad, 22, TrFmt("Deadline: %s", {WhenText(d.roundEndsAt)}), pal::warn, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	return h;
}

int PaintPrizesCard(Painter &p, const TourneyDetail &d, int x, int y, int w)
{
	const int pad = 22, h = 120;
	PaintGlassCard(p, x, y, w, h);
	PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, "Prizes", pal::caption);
	const bool swiss = TourneyIsSwiss(d), twice = TourneyIsDouble(d);
	const struct {
		LeagueMetal metal;
		const char *name, *who;
	} kPrizes[] = {{LeagueMetal::Gold, "Gold", "Champion"}, {LeagueMetal::Silver, "Silver", swiss ? "2nd place" : "Finalist"},
		{LeagueMetal::Bronze, "Bronze", swiss || twice ? "3rd place" : "Semifinals"}};
	const int cw = (w - 2 * pad) / 3;
	for (int i = 0; i < 3; i++) {
		const int cx = x + pad + i * cw;
		DrawLeaguePhalera(p, cx, y + 56, 30, kPrizes[i].metal);
		p.Font(1);
		p.Text(cx + 38, y + 50, cw - 40, 20, kPrizes[i].name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(cx + 38, y + 70, cw - 40, 20, kPrizes[i].who, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	return h;
}

}

bool PaintPageHeader(Painter &p, const State &st, const HeaderUi &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const auto view = LeagueTourneyPage(u.slug);
	const TourneyPageView &page = *view;
	const int right = PaintHeaderActions(p, l, {{Tr("Tournaments"), kBack, true, true}}, mx, my);
	// A Swiss tournament has standings instead of a bracket, and its rounds open from them.
	const bool swiss = page.loaded && TourneyIsSwiss(page.detail);
	std::vector<TabSpec> tabs = {{Tr(swiss ? "Standings" : "Bracket"), kTourney, ArgOf(kSubPageTab, 0), u.pageTab == 0 || u.pageTab >= kRoundTab, true},
		{Tr("Rules"), kTourney, ArgOf(kSubPageTab, 2), u.pageTab == 2, true},
		{Tr("Log"), kTourney, ArgOf(kSubPageTab, 3), u.pageTab == 3, true}};
	if (!page.loaded) {
		// The name is on its way: its place shows as a bar the size of a title.
		PaintSkeleton(p, l.margin, l.titleY + 24, std::min(560, right - l.margin - 40), l.titleSize - 30);
		return true;
	}
	p.raw = true;
	PaintScreenTitle(p, l, page.detail.name, tabs, right, mx, my);
	p.raw = false;
	const TourneyDetail &d = page.detail;
	int x = l.margin;
	if (d.official) x += PaintTag(p, x, l.infoY + 2, "Official", pal::red, pal::onRed) + 14;
	else x += PaintTag(p, x, l.infoY + 2, "Community", pal::edgeHot, pal::textDim, true) + 14;
	if (d.isPrivate) x += PaintTag(p, x, l.infoY + 2, "Private", pal::edgeHot, pal::textDim, true) + 14;
	const TourneyStatus s = d.status;
	const char *status = s == TourneyStatus::Signup ? "Sign-up open" : s == TourneyStatus::Checkin ? "Check-in open" : s == TourneyStatus::Running ? "Live" : s == TourneyStatus::Finished ? "Over" : "Cancelled";
	x += PaintTag(p, x, l.infoY + 2, status, s == TourneyStatus::Running ? pal::redText : pal::edgeHot, s == TourneyStatus::Running ? pal::redText : pal::textDim, true) + 14;
	(void)x;
	return true;
}

void PaintPageScreen(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const auto pageView = LeagueTourneyPage(u.slug);
	const auto mineView = LeagueTourneyMine();
	const TourneyPageView &page = *pageView;
	const TourneyMineView &mine = *mineView;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	if (!page.loaded) {
		PaintGlassCard(p, m, l.top, inner, bottom - l.top);
		if (!page.missing && !page.failed) {
			// The tournament's card: a heading, a few lines, and the rows of its players or its bracket under them.
			PaintSkeleton(p, m + 32, l.top + 32, 320, 18);
			for (int k = 0; k < 3; k++) PaintSkeleton(p, m + 32, l.top + 70 + k * 26, (inner - 64) * (70 - k * 15) / 100, 12);
			const int rowsY = l.top + 170, rowH = 52;
			PaintSkeletonRows(p, m + 1, rowsY, inner - 2, rowH, std::max(0, std::min(8, (bottom - rowsY - 16) / rowH)),
				{{m + 32, inner * 40 / 100}, {m + 32 + inner * 40 / 100, inner * 25 / 100}, {m + 32 + inner * 65 / 100, inner * 25 / 100}});
			return;
		}
		p.Font(0);
		p.Text(m + 32, l.top + 32, inner - 64, 30,
			page.missing ? "This tournament is no longer published." : "Couldn't reach the league server. Check your connection and try again.", pal::textDim);
		return;
	}
	Ctx c{st, u, page.detail, mine};
	c.mx = st.mouseX;
	c.my = st.mouseY;
	if (const TourneyEntrant *e = TourneyEntrantBySteamId(page.detail, SteamOwnId())) c.me = e->entryId;
	if (mine.mine.next.present && mine.mine.next.tournamentId == page.detail.id) c.next = &mine.mine.next;
	// The page may know before the player's tournaments that the match is over (they are read apart): it is not theirs to play then.
	if (c.next) {
		const TourneyMatch *shown = MatchById(page.detail, c.next->match);
		if ((shown && TourneyMatchOver(shown->status)) || page.detail.status == TourneyStatus::Finished || page.detail.status == TourneyStatus::Cancelled) c.next = nullptr;
	}
	int y = l.top - (l.roomy ? 14 : 8);
	if (PaintBanner(p, c, m, y, inner)) y += 64 + 20;
	else y += l.roomy ? 14 : 8;
	const int sideW = std::max(320, std::min(400, inner * 22 / 100)), mainW = inner - sideW - kCardGap, sx = m + mainW + kCardGap;
	PaintGlassCard(p, m, y, mainW, bottom - y);
	const int ix = m + 24, iy = y + 18, iw = mainW - 48, ih = bottom - y - 36;
	const bool organizer = IsOrganizer(page.detail), swiss = TourneyIsSwiss(page.detail);
	if (organizer) c.disputes = LeagueTourneyDisputes(page.detail.id);
	const bool bracketTab = u.pageTab == 0 || u.pageTab >= kRoundTab;
	// What scrolls out of the card is not drawn and can't be clicked through the header, the banner or the side column.
	ClipArea mainClip(p, m, y, mainW, bottom - y);
	// The players are listed on the bracket's tab (1 was their own tab).
	int tab = u.pageTab == 1 ? 0 : u.pageTab;
	(void)bracketTab;
	// The organizer's tab is gone: its disputes are in the log's tab.
	if (tab == kOrganizerTab) tab = 0;
	if ((tab == 0 || tab >= kRoundTab) && swiss) {
		if (page.detail.matches.empty()) {
			p.Font(3);
			p.Text(ix, iy, iw, 22, "The first round is paired when the check-in closes. Meanwhile, these are the players signed up.", pal::caption);
			PaintEntrants(p, c, ix, iy + 34, iw - 20, ih - 34, out);
		}
		else PaintSwiss(p, c, ix, iy, iw - 20, ih, out);
	}
	else if (tab == 0 || tab >= kRoundTab) {
		// The players on the left, the bracket they fill on the right: empty until the draw.
		const int listW = std::max(240, std::min(320, iw * 24 / 100)), gap = 36, bx = ix + listW + gap, bw = iw - listW - gap;
		PaintEntrantColumn(p, c, ix, iy, listW, ih, out);
		p.Fill(ix + listW + gap / 2, iy, 1, ih, pal::hairline);
		if (page.detail.matches.empty()) PaintBracketPreview(p, c, bx, iy, bw, ih, out);
		else if (TourneyIsDouble(page.detail)) PaintDoubleBracket(p, c, bx, iy, bw, ih, out);
		else PaintBracket(p, c, bx, iy, bw, ih, out);
	}
	else if (tab == 2) PaintRules(p, c, ix, iy, iw - 20, ih, out);
	else if (organizer && !c.disputes.disputes.empty()) {
		// The organizer's disputes over the log, while there are any.
		const int dh = std::min(ih / 2 + 40, 40 + static_cast<int>(c.disputes.disputes.size()) * 180);
		PaintOrganizer(p, c, ix, iy, iw - 20, dh, out);
		PaintLog(p, c, ix, iy + dh + 24, iw - 20, ih - dh - 24, out);
	}
	else PaintLog(p, c, ix, iy, iw - 20, ih, out);
	mainClip.End();

	// The side column scrolls when its cards are taller than the screen; its height is last frame's, as the cards measure as they paint.
	const int sideH = bottom - y, sideMax = std::max(0, u.pageSideH - sideH), sideScroll = std::min(u.pageSideScroll, sideMax);
	int sy = y - sideScroll;
	{
		ClipArea sideClip(p, sx, y, sideW, sideH);
		if (organizer) sy += PaintOrganizerCard(p, c, sx, sy, sideW) + 12;
		sy += PaintStatusCard(p, c, sx, sy, sideW) + 12;
		if (CupHasScenarios(page.detail) && page.detail.status != TourneyStatus::Finished && page.detail.status != TourneyStatus::Cancelled) {
			int mapsH = 0;
			PaintCupMapsState(p, st, page.detail, sx, sy, sideW, mapsH);
			sy += mapsH + 12;
		}
		// The cards that only add to what is above show when there is room for them, and don't make the column scroll.
		const int room = bottom - (sy + sideScroll);
		if (room > (TourneyByRounds(page.detail) ? 174 : 150)) sy += PaintProgressCard(p, c, sx, sy, sideW) + 12;
		if (page.detail.official && bottom - (sy + sideScroll) > 120) sy += PaintPrizesCard(p, page.detail, sx, sy, sideW) + 12;
	}
	out.pageSideH = sy + sideScroll - y - 12;
	if (sideMax > 0) {
		PaintScrollbar(p, kBarPageSide, sx + sideW + std::max(4, (m - 6) / 2 - 3), y, sideH, {sideScroll, sideMax, sideH}, st.dropBar, c.mx, c.my);
		SetScrollArea(p, out.pageSideArea, sx, y, sideW + m, sideH, sideMax);
	}
	PatronsWatch(c.ids);
}

void PaintWalkoverDialog(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const auto page = LeagueTourneyPage(u.slug, 60000);
	const TourneyMatch *m = MatchById(page->detail, u.walkoverMatch);
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 560, 330, "Give the match without playing", pal::redText);
	const std::string a = m ? m->a.name : Tr("Side A"), b = m ? m->b.name : Tr("Side B");
	p.raw = true;
	DialogText(p, d, 66, TrFmt("%s against %s: the whole series goes to one of them, or both are out. Who goes through?", {a, b}), pal::textDim, 3);
	const int bx = d.BodyX(), bw = d.BodyW(), bh = 38;
	int by = d.y + 118;
	p.Button(bx, by, bw, bh, TrFmt("%s goes through", {a}), kTourney, ArgOf(kSubWalkoverA, 0), mx, my);
	by += bh + 8;
	p.Button(bx, by, bw, bh, TrFmt("%s goes through", {b}), kTourney, ArgOf(kSubWalkoverB, 0), mx, my);
	by += bh + 8;
	p.Button(bx, by, bw, bh, Tr("Both are out"), kTourney, ArgOf(kSubWalkoverNobody, 0), mx, my);
	p.raw = false;
	DialogButtons(p, d, {{"Cancel", kTourney, ArgOf(kSubWalkoverCancel, 0)}}, mx, my);
}

}
