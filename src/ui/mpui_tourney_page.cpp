// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

// TorneoCuadro's colours that the palette does not have.
const COLORREF kLinePath = RGB(0x7a, 0x75, 0x6e), kGold = RGB(0xe0, 0xb5, 0x4a), kGoldDark = RGB(0x8a, 0x6a, 0x22);

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
};

void HLine(Painter &p, int x1, int x2, int y, COLORREF c)
{
	p.Fill(std::min(x1, x2), y, std::abs(x2 - x1) + 1, 1, c);
}

void VLine(Painter &p, int x, int y1, int y2, COLORREF c)
{
	p.Fill(x, std::min(y1, y2), 1, std::abs(y2 - y1) + 1, c);
}

std::string MetaText(const TourneyDetail &d)
{
	std::string out = FormatText(d) + "   \xC2\xB7   " + TrFmt("%s players", {std::to_string(d.entries)}) + "   \xC2\xB7   " + ContentText(d) + "   \xC2\xB7   " +
		PresetText(d.settings) + "   \xC2\xB7   ";
	if (d.status == TourneyStatus::Running) out += TrFmt("Since %s", {HourText(d.startsAt)});
	else if (d.status == TourneyStatus::Finished) out += DayText(d.startsAt);
	else out += WhenText(d.startsAt);
	return out;
}

// The name of whoever comes to an empty side: the two of the match before, or nobody yet.
std::string ComingName(const TourneyDetail &d, const TourneyMatch &m, bool sideB)
{
	if (m.round <= 1) return Tr(m.status == TourneyMatchStatus::Bye ? "Straight through" : "To be decided");
	const TourneyMatch *feeder = TourneyMatchAt(d, m.round - 1, 2 * m.position + (sideB ? 1 : 0));
	if (!feeder) return Tr("To be decided");
	if (feeder->status == TourneyMatchStatus::Walkover && !feeder->winner) return Tr("Nobody: both missed it");
	if (feeder->a.Present() && feeder->b.Present()) return TrFmt("%s or %s", {feeder->a.name, feeder->b.name});
	return Tr("To be decided");
}

void PaintSide(Painter &p, Ctx &c, const TourneyMatch &m, const TourneyCard &card, bool sideB, int x, int y, int w)
{
	const int h = 28, seedW = 26, scoreW = 32;
	const bool over = TourneyMatchOver(m.status);
	const bool won = card.Present() && m.winner == card.entryId;
	const bool lost = over && card.Present() && !won;
	p.Fill(x, y, seedW, h, pal::selected);
	UseFace(p, BrandFace::UiStrong, 12);
	p.raw = true;
	p.Text(x, y, seedW, h, card.Present() ? (card.seed ? std::to_string(card.seed) : std::string("-")) : std::string(), pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
		const int youW = mine ? 30 : 0;
		std::string extra;
		if (c.next && c.next->match == m.id && !mine && c.next->opponent.inRoom && !over) extra = Tr("in the room");
		p.raw = true;
		const int textW = std::min(nw - youW, p.TextWidth(card.name) + 2);
		p.raw = false;
		PaintName(p, nx, y, nw - youW - (extra.empty() ? 0 : 70), h, card.name, card.steamId, ink, c.mx, c.my);
		if (mine) PaintTag(p, nx + std::min(textW, nw - youW) + 6, y + 3, "You", pal::red, pal::onRed);
		if (!extra.empty()) {
			UseFace(p, BrandFace::Ui, 12);
			p.raw = true;
			p.Text(nx + nw - 70, y, 70, h, "\xC2\xB7 " + extra, pal::ok, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		p.raw = false;
		if (card.steamId) c.ids.push_back(card.steamId);
	}
	const int sx = x + w - scoreW;
	p.Fill(sx, y, scoreW, h, won ? pal::button : pal::field);
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
		p.Text(sx, y, scoreW, h, text, won ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}
}

void PaintBox(Painter &p, Ctx &c, const TourneyMatch &m, int x, int y, const TourneyBracketMetrics &bm)
{
	const bool live = m.status == TourneyMatchStatus::Live;
	const bool mine = c.me && m.Has(c.me) && !TourneyMatchOver(m.status);
	const bool bye = m.status == TourneyMatchStatus::Bye || (m.status == TourneyMatchStatus::Walkover && !(m.a.Present() && m.b.Present()));
	p.Fill(x, y, bm.boxW, bm.boxH, live || mine ? pal::selected : pal::raised);
	if (bye) PaintDashedFrame(p, x, y, bm.boxW, bm.boxH, pal::edgeHot);
	else p.Frame(x, y, bm.boxW, bm.boxH, live ? pal::red : mine ? pal::warn : pal::edge);
	PaintSide(p, c, m, m.a, false, x + 1, y + 1, bm.boxW - 2);
	p.Fill(x + 1, y + 29, bm.boxW - 2, 1, pal::hairline);
	PaintSide(p, c, m, m.b, true, x + 1, y + 30, bm.boxW - 2);
	// The label over the box: what is happening in it.
	std::string label;
	COLORREF ink = pal::caption;
	if (live) {
		label = m.startedAt ? TrFmt("Live \xC2\xB7 %s", {DurationText(NowMs() - m.startedAt)}) : Tr("Live");
		ink = pal::redText;
	}
	else if (mine && m.status == TourneyMatchStatus::Ready) {
		label = m.deadlineAt > NowMs() ? TrFmt("Room open \xC2\xB7 %s to enter", {TourneyClock(m.deadlineAt - NowMs())}) : Tr("Room open");
		ink = pal::warn;
	}
	else if (m.status == TourneyMatchStatus::Disputed) {
		label = Tr("Under review");
		ink = pal::warn;
	}
	else if (m.status == TourneyMatchStatus::Walkover && m.a.Present() && m.b.Present()) label = Tr("No-show");
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

void PaintBracket(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	TourneyBracketMetrics bm;
	bm.top = 46;
	const int champW = 150;
	const int contentW = TourneyBracketWidth(bm, d.rounds) + 40 + champW;
	const int contentH = std::max(TourneyBracketHeight(bm, d.rounds) + 10, d.rounds ? TourneyBoxMidY(bm, d.rounds, 0) + 70 : 0);
	const bool barY = contentH > h, barX = contentW > w - (barY ? 14 : 0);
	const int viewW = w - (barY ? 14 : 0), viewH = h - (barX ? 14 : 0);
	const int maxX = std::max(0, contentW - viewW), maxY = std::max(0, contentH - viewH);
	const int sx = std::min(c.u.bracketX, maxX), sy = std::min(c.u.bracketY, maxY);
	out.bracketArea.valid = true;
	out.bracketArea.r = p.R(x, y, w, h);
	OffsetRect(&out.bracketArea.r, p.ox, p.oy);
	out.bracketArea.max = maxY;
	out.bracketArea.maxX = maxX;
	const int ox = x - sx, oy = y - sy;
	p.Clip(x, y, viewW, viewH);
	for (int r = 1; r <= d.rounds; r++) {
		const int hx = ox + TourneyBoxX(bm, r);
		const TourneyMatch *first = TourneyMatchAt(d, r, 0);
		UseFace(p, BrandFace::UiStrong, 12);
		const std::string stage = StageText(TourneyStageOf(r, d.rounds), r);
		p.raw = true;
		const int stageW = p.CapsWidth(stage, 2) + 4;
		p.TextCaps(hx, oy, stageW, 20, stage, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
		UseFace(p, BrandFace::Ui, 12);
		const int bestOf = first ? first->bestOf : r == d.rounds ? d.finalBestOf : d.bestOf;
		const TourneyMap map = TourneyMapOfRound(d, r);
		p.Text(hx + stageW + 8, oy, bm.boxW - stageW - 8, 20, TrFmt("Bo%s", {std::to_string(bestOf)}) + (map.Known() ? " \xC2\xB7 " + MapText(map) : std::string()), pal::caption,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	for (const TourneyMatch &m : d.matches) {
		const std::vector<TourneySegment> segs = TourneyConnector(bm, m.round, m.position, d.rounds);
		if (segs.size() != 3) continue;
		const COLORREF own = TourneyMatchOver(m.status) && m.winner ? kLinePath : pal::lineStrong;
		const COLORREF into = LineInk(c, TourneyMatchAt(d, m.round + 1, m.position / 2));
		HLine(p, ox + segs[0].x1, ox + segs[0].x2, oy + segs[0].y1, own);
		VLine(p, ox + segs[1].x1, oy + segs[1].y1, oy + segs[1].y2, into == pal::lineStrong || into == kLinePath ? own : into);
		HLine(p, ox + segs[2].x1, ox + segs[2].x2, oy + segs[2].y1, into);
	}
	for (const TourneyMatch &m : d.matches) PaintBox(p, c, m, ox + TourneyBoxX(bm, m.round), oy + TourneyBoxY(bm, m.round, m.position), bm);
	// The champion, at the end of the final's line.
	const TourneyMatch *final = TourneyMatchAt(d, d.rounds, 0);
	if (final) {
		const int fx = ox + TourneyBoxX(bm, d.rounds) + bm.boxW, fy = oy + TourneyBoxMidY(bm, d.rounds, 0);
		HLine(p, fx, fx + 40, fy, TourneyMatchOver(final->status) ? kLinePath : pal::lineStrong);
		const int cx = fx + 40 + champW / 2, size = 76;
		const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
		if (champ) DrawLeaguePhalera(p, cx - size / 2, fy - size / 2 - 30, size, LeagueMetal::Gold);
		else {
			PaintRing(p, cx, fy - 30, size / 2, kGoldDark);
			PaintRing(p, cx, fy - 30, size / 2 - 6, kGoldDark);
			PaintRing(p, cx, fy - 30, size / 2 - 16, kGoldDark);
		}
		PaintCaps(p, fx + 40, fy + 16, champW, 18, "Champion", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		UseFace(p, champ ? BrandFace::UiStrong : BrandFace::Ui, 15);
		p.raw = true;
		p.Text(fx + 40, fy + 38, champW, 22, champ ? champ->name : Tr("To be decided"), champ ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	p.Unclip();
	if (barY) PaintScrollbar(p, kBarBracketY, x + w - 8, y, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
	if (barX) {
		// A bar along the bottom: the halves either side of the thumb page left and right.
		const int track = viewW, thumb = std::max(40, track * viewW / contentW), at = maxX ? (track - thumb) * sx / maxX : 0;
		p.Fill(x, y + h - 6, track, 4, pal::field);
		p.Fill(x + at, y + h - 6, thumb, 4, pal::lineStrong);
		p.AddHit(x, y + h - 14, at, 14, kTourney, ArgOf(kSubPan, 0));
		p.AddHit(x + at + thumb, y + h - 14, track - at - thumb, 14, kTourney, ArgOf(kSubPan, 1));
	}
}

void PaintEntrants(Painter &p, Ctx &c, int x, int y, int w, int h, PaintOut &out)
{
	const TourneyDetail &d = c.d;
	PaintCaps(p, x, y, 60, 20, "Seed", pal::caption);
	PaintCaps(p, x + 70, y, w / 2, 20, "Player", pal::caption);
	PaintCaps(p, x + w / 2 + 40, y, w / 4, 20, "Status", pal::caption);
	PaintCaps(p, x + w - 120, y, 120, 20, "Place", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const int top = y + 30, rowH = 42, viewH = h - 30;
	const int total = static_cast<int>(d.entrants.size()) * rowH, maxY = std::max(0, total - viewH), sy = std::min(c.u.bracketY, maxY);
	out.bracketArea.valid = true;
	out.bracketArea.r = p.R(x, top, w, viewH);
	OffsetRect(&out.bracketArea.r, p.ox, p.oy);
	out.bracketArea.max = maxY;
	p.Clip(x, top, w, viewH);
	int ry = top - sy;
	for (const TourneyEntrant &e : d.entrants) {
		p.Fill(x, ry, w, 1, pal::hairline);
		UseFace(p, BrandFace::UiStrong, 15);
		p.raw = true;
		p.Text(x, ry, 60, rowH, e.seed ? std::to_string(e.seed) : std::string("-"), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		PaintPlayerDisc(p, x + 70, ry + 7, 28, e.steamId, e.name);
		UseFace(p, e.entryId == c.me ? BrandFace::UiStrong : BrandFace::Body, 17);
		PaintName(p, x + 108, ry, w / 2 - 70, rowH, e.name, e.steamId, e.entryId == c.me ? pal::redText : pal::text, c.mx, c.my);
		if (e.steamId) c.ids.push_back(e.steamId);
		const char *status = "Signed up";
		COLORREF ink = pal::textDim;
		switch (e.status) {
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
		p.Font(3);
		p.Text(x + w / 2 + 40, ry, w / 4, rowH, status, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (e.placement) {
			const LeagueMetal metal = e.placement == 1 ? LeagueMetal::Gold : e.placement == 2 ? LeagueMetal::Silver : e.placement == 3 ? LeagueMetal::Bronze : LeagueMetal::None;
			if (metal != LeagueMetal::None && d.official) DrawLeaguePhalera(p, x + w - 120, ry + 11, 20, metal);
			UseDisplay(p, 22);
			p.raw = true;
			p.Text(x + w - 90, ry, 90, rowH, std::to_string(e.placement), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		ry += rowH;
	}
	if (d.entrants.empty()) {
		p.Font(0);
		p.Text(x, top + 10, w, 30, "Nobody has signed up yet.", pal::textDim);
	}
	p.Unclip();
	if (maxY > 0) PaintScrollbar(p, kBarBracketY, x + w + 8, top, viewH, {sy, maxY, viewH}, c.st.dropBar, c.mx, c.my);
}

void PaintRules(Painter &p, Ctx &c, int x, int y, int w, int h)
{
	const TourneyDetail &d = c.d;
	int ry = y;
	auto row = [&](const std::string &key, const std::string &value) {
		if (ry + 30 > y + h) return;
		p.Font(0);
		p.Text(x, ry, 220, 28, key, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = true;
		p.Text(x + 230, ry, w - 230, 28, value, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ry += 32;
	};
	PaintSectionLabel(p, x, ry, w, 22, "Format");
	ry += 32;
	row(Tr("Bracket"), FormatText(d) + ", " + TrFmt("%s to %s players", {std::to_string(d.minEntries), std::to_string(d.maxEntries)}));
	row(Tr("Series"), TrFmt("Best of %s; the final, best of %s", {std::to_string(d.bestOf), std::to_string(d.finalBestOf)}));
	row(Tr("Seeding"), Tr(d.seeding == "random" ? "Drawn at random" : d.seeding == "manual" ? "Set by the organisers" : "By the ranked leaderboard, frozen when the check-in closes"));
	row(Tr("Check-in"), TrFmt("From %s to %s; whoever does not check in loses the place", {HourText(d.checkinOpensAt), HourText(d.startsAt)}));
	row(Tr("No-show"), TrFmt("%s min from when the match is ready", {std::to_string(d.noshowMinutes)}));
	ry += 10;
	PaintSectionLabel(p, x, ry, w, 22, "Maps and options");
	ry += 32;
	const int rounds = std::max(d.rounds, 1);
	for (int r = 1; r <= rounds && !d.maps.empty(); r++) row(d.rounds ? StageText(TourneyStageOf(r, d.rounds), r) : Tr("Every round"), MapText(TourneyMapOfRound(d, r)));
	if (d.maps.empty()) row(Tr("Maps"), Tr("Chosen in the room"));
	row(Tr("Content"), ContentText(d));
	row(Tr("Settings"), PresetText(d.settings));
	row(Tr("Civilizations"), Tr(d.settings.hideCivs ? "Hidden until the game starts" : "Visible in the room"));
	row(Tr("Spectators"), TrFmt("With a %s s delay", {std::to_string(TourneySpecDelay(d.settings))}));
	ry += 10;
	if (ry + 120 > y + h) return;
	PaintSectionLabel(p, x, ry, w, 22, "If something fails");
	ry += 32;
	p.Font(0);
	p.TextWrapped(x, ry, w, 80,
		"The mod reports the result of every game; if the two reports differ, the organisers decide. If your opponent does not come to the room in "
		"time, you can claim the win from your match's screen; if neither comes, both are out.",
		pal::body);
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
	out.bracketArea.valid = true;
	out.bracketArea.r = p.R(x, y, w, h);
	OffsetRect(&out.bracketArea.r, p.ox, p.oy);
	out.bracketArea.max = maxY;
	p.Clip(x, y, w, h);
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
	p.Unclip();
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
		title = TrFmt("Your %s match is ready.", {StageText(n.stage, n.round)});
		if (n.canClaimNoShow) {
			text = TrFmt("%s did not come to the room in time: you can claim the win.", {n.opponent.name});
			buttons.push_back({Tr("Claim the win"), ArgOf(kSubNoShow, n.match)});
		}
		else if (n.opponent.inRoom && left > 0) text = TrFmt("%s is already in the room. If you don't enter within %s, they can claim the win by no-show.", {n.opponent.name, TourneyClock(left)});
		else if (left > 0) text = TrFmt("Enter the room within %s, or the match counts as a no-show.", {TourneyClock(left)});
		else text = Tr("The waiting time is over: enter the room now.");
		buttons.push_back({Tr("Your match"), ArgOf(kSubMatch, n.match)});
		buttons.push_back({Tr("Go to the room"), ArgOf(kSubGo, n.match)});
		strong = static_cast<int>(buttons.size()) - 1;
	}
	else if (c.next && c.next->status == TourneyMatchStatus::Live) {
		title = Tr("Your match is being played.");
		text = Tr("The result is reported by the mod when the game ends.");
	}
	else if (c.next && c.next->status == TourneyMatchStatus::Disputed) {
		title = Tr("Your match is under review.");
		text = Tr("The organisers are looking at the reports of the game.");
	}
	else if (d.status == TourneyStatus::Checkin && e && TourneyEntryActive(e->status) && !e->checkedIn) {
		title = Tr("The check-in is open.");
		text = TrFmt("Confirm before %s that you are playing, or you lose your place.", {HourText(d.startsAt)});
		buttons.push_back({Tr("Check in"), ArgOf(kSubCheckIn, d.id)});
		strong = 0;
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
		const int h = 236;
		PaintGlassCard(p, x, y, w, h);
		PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, d.status == TourneyStatus::Checkin ? "Check-in" : "Sign-up", pal::caption);
		UseFace(p, BrandFace::UiStrong, 18);
		p.raw = true;
		p.Text(x + pad, y + 48, w - 2 * pad, 26, TrFmt("%s of %s signed up", {std::to_string(d.entries), std::to_string(d.maxEntries)}), pal::text);
		p.raw = false;
		p.Fill(x + pad, y + 82, w - 2 * pad, 6, pal::button);
		if (d.maxEntries) p.Fill(x + pad, y + 82, (w - 2 * pad) * std::min(d.entries, d.maxEntries) / d.maxEntries, 6, pal::red);
		p.Font(3);
		p.raw = true;
		const std::string window = d.status == TourneyStatus::Checkin ? TrFmt("Check-in open until %s", {HourText(d.startsAt)})
																	   : TrFmt("Check-in %s, from %s to %s", {DayText(d.checkinOpensAt), HourText(d.checkinOpensAt), HourText(d.startsAt)});
		p.Text(x + pad, y + 98, w - 2 * pad, 22, window, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		std::string status;
		if (in) status = e->status == TourneyEntryStatus::Waitlist ? Tr("You are on the waiting list.") : e->checkedIn ? Tr("You are checked in.") : Tr("You are signed up.");
		p.Text(x + pad, y + 122, w - 2 * pad, 22, status, pal::ok, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		const int by = y + h - 22 - 44, bw = w - 2 * pad;
		if (!LeagueLinked()) p.Button(x + pad, by, bw, 44, "Link to sign up", kLeagueLink, 0, mx, my, true, true);
		else if (!in) {
			const bool busy = LeagueTourneyBusy(TourneyOp::SignUp, d.id);
			p.Button(x + pad, by, bw, 44, busy ? "Signing up..." : "Sign up", kTourney, ArgOf(kSubSignUp, d.id), mx, my, !busy, true);
		}
		else if (d.status == TourneyStatus::Checkin && !e->checkedIn) {
			const int half = (bw - 10) / 2;
			const bool busy = LeagueTourneyBusy(TourneyOp::CheckIn, d.id);
			p.Button(x + pad, by, half, 44, busy ? "Checking in..." : "Check in", kTourney, ArgOf(kSubCheckIn, d.id), mx, my, !busy, true);
			p.Button(x + pad + half + 10, by, bw - half - 10, 44, "Withdraw", kTourney, ArgOf(kSubWithdraw, d.id), mx, my);
		}
		else p.Button(x + pad, by, bw, 44, "Withdraw", kTourney, ArgOf(kSubWithdraw, d.id), mx, my, !LeagueTourneyBusy(TourneyOp::Withdraw, d.id));
		return h;
	}
	if (d.status == TourneyStatus::Finished) {
		const int h = 224;
		PaintGlassCard(p, x, y, w, h);
		PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, "Champion", kGold);
		const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
		DrawLeaguePhalera(p, x + pad, y + 54, 64, LeagueMetal::Gold);
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
	PaintCaps(p, x + pad + 18, y + 20, w - 2 * pad, 20, live ? Tr("Live") + " \xC2\xB7 " + StageText(live->stage, live->round) : Tr("Live"), live ? pal::redText : pal::caption);
	if (!live) {
		p.Font(0);
		p.Text(x + pad, y + 52, w - 2 * pad, 26, "No game is being played right now.", pal::textDim);
		return h;
	}
	int cy = y + 54;
	for (const TourneyCard *card : {&live->a, &live->b}) {
		PaintPlayerDisc(p, x + pad, cy, 32, card->steamId, card->name);
		UseFace(p, BrandFace::UiStrong, 18);
		PaintName(p, x + pad + 44, cy, w - 2 * pad - 44 - 90, 32, card->name, card->steamId, pal::text, mx, my);
		UseFace(p, BrandFace::UiStrong, 11);
		p.raw = true;
		p.TextCaps(x + w - pad - 90, cy, 90, 32, TrFmt("Seed %s", {std::to_string(card->seed)}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 2);
		p.raw = false;
		cy += 40;
	}
	p.Font(3);
	p.raw = true;
	const TourneyMap map = TourneyMapOfRound(d, live->round);
	p.Text(x + pad, cy + 4, w - 2 * pad, 20, (map.Known() ? MapText(map) + " \xC2\xB7 " : std::string()) + (live->startedAt ? TrFmt("%s of play", {DurationText(NowMs() - live->startedAt)}) : std::string()),
		pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Text(x + pad, cy + 26, w - 2 * pad, 20, TrFmt("Spectators with a %s s delay", {std::to_string(TourneySpecDelay(d.settings))}), pal::textDim,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	p.Button(x + pad, y + h - 20 - 40, w - 2 * pad, 40, "Watch live", kTourney, ArgOf(kSubWatch, live->id), mx, my, live->lobby != 0);
	return h;
}

int PaintProgressCard(Painter &p, Ctx &c, int x, int y, int w)
{
	const TourneyDetail &d = c.d;
	const int pad = 22, h = 150;
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
	p.Font(3);
	p.raw = true;
	p.Text(x + pad, y + 104, w - 2 * pad, 22, d.status == TourneyStatus::Running || d.status == TourneyStatus::Finished ? TrFmt("Started at %s", {HourText(d.startsAt)})
																									 : TrFmt("Starts %s", {WhenText(d.startsAt)}),
		pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	return h;
}

int PaintPrizesCard(Painter &p, int x, int y, int w)
{
	const int pad = 22, h = 120;
	PaintGlassCard(p, x, y, w, h);
	PaintCaps(p, x + pad, y + 20, w - 2 * pad, 20, "Prizes", pal::caption);
	const struct {
		LeagueMetal metal;
		const char *name, *who;
	} kPrizes[] = {{LeagueMetal::Gold, "Gold", "Champion"}, {LeagueMetal::Silver, "Silver", "Finalist"}, {LeagueMetal::Bronze, "Bronze", "Semifinals"}};
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

bool PaintPageHeader(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const TourneyPageView page = LeagueTourneyPage(u.slug);
	const int right = PaintHeaderActions(p, l, {{Tr("Tournaments"), kBack, true, true}}, mx, my);
	const std::vector<TabSpec> tabs = {{Tr("Bracket"), kTourney, ArgOf(kSubPageTab, 0), u.pageTab == 0, true},
		{Tr("Entrants"), kTourney, ArgOf(kSubPageTab, 1), u.pageTab == 1, true}, {Tr("Rules"), kTourney, ArgOf(kSubPageTab, 2), u.pageTab == 2, true},
		{Tr("Log"), kTourney, ArgOf(kSubPageTab, 3), u.pageTab == 3, true}};
	p.raw = true;
	PaintScreenTitle(p, l, page.loaded ? page.detail.name : std::string("..."), tabs, right, mx, my);
	p.raw = false;
	if (!page.loaded) return true;
	const TourneyDetail &d = page.detail;
	int x = l.margin;
	if (d.official) x += PaintTag(p, x, l.infoY + 2, "Official", pal::red, pal::onRed) + 14;
	const TourneyStatus s = d.status;
	const char *status = s == TourneyStatus::Signup ? "Sign-up open" : s == TourneyStatus::Checkin ? "Check-in open" : s == TourneyStatus::Running ? "Live" : s == TourneyStatus::Finished ? "Over" : "Cancelled";
	x += PaintTag(p, x, l.infoY + 2, status, s == TourneyStatus::Running ? pal::redText : pal::edgeHot, s == TourneyStatus::Running ? pal::redText : pal::textDim, true) + 14;
	p.Font(0);
	p.raw = true;
	p.Text(x, l.infoY, l.w - l.margin - x, l.infoH, MetaText(d), pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	return true;
}

void PaintPageScreen(Painter &p, const State &st, const Ui &u, PaintOut &out)
{
	const TourneyPageView page = LeagueTourneyPage(u.slug);
	const TourneyMineView mine = LeagueTourneyMine();
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	if (!page.loaded) {
		PaintGlassCard(p, m, l.top, inner, bottom - l.top);
		p.Font(0);
		p.Text(m + 32, l.top + 32, inner - 64, 30,
			page.missing ? "This tournament is no longer published." : page.failed ? "Couldn't reach the league server. Check your connection and try again." : "Reading the tournament...",
			pal::textDim);
		return;
	}
	Ctx c{st, u, page.detail, mine};
	c.mx = st.mouseX;
	c.my = st.mouseY;
	if (const TourneyEntrant *e = TourneyEntrantBySteamId(page.detail, SteamOwnId())) c.me = e->entryId;
	if (mine.mine.next.present && mine.mine.next.tournamentId == page.detail.id) c.next = &mine.mine.next;
	int y = l.top - (l.roomy ? 14 : 8);
	if (PaintBanner(p, c, m, y, inner)) y += 64 + 20;
	else y += l.roomy ? 14 : 8;
	const int sideW = std::max(320, std::min(400, inner * 22 / 100)), mainW = inner - sideW - kCardGap, sx = m + mainW + kCardGap;
	PaintGlassCard(p, m, y, mainW, bottom - y);
	const int ix = m + 24, iy = y + 18, iw = mainW - 48, ih = bottom - y - 36;
	const int tab = page.detail.matches.empty() && u.pageTab == 0 ? 1 : u.pageTab;
	if (tab == 0) PaintBracket(p, c, ix, iy, iw, ih, out);
	else if (tab == 1) {
		if (page.detail.matches.empty() && u.pageTab == 0) {
			p.Font(3);
			p.Text(ix, iy, iw, 22, "The bracket is drawn when the check-in closes. Meanwhile, these are the players signed up.", pal::caption);
			PaintEntrants(p, c, ix, iy + 34, iw - 20, ih - 34, out);
		}
		else PaintEntrants(p, c, ix, iy, iw - 20, ih, out);
	}
	else if (tab == 2) PaintRules(p, c, ix, iy, iw, ih);
	else PaintLog(p, c, ix, iy, iw - 20, ih, out);

	int sy = y;
	p.Clip(sx, y, sideW, bottom - y);
	sy += PaintStatusCard(p, c, sx, sy, sideW) + 12;
	if (bottom - sy > 150) sy += PaintProgressCard(p, c, sx, sy, sideW) + 12;
	if (page.detail.official && bottom - sy > 120) PaintPrizesCard(p, sx, sy, sideW);
	p.Unclip();
	PatronsWatch(c.ids);
}

}
