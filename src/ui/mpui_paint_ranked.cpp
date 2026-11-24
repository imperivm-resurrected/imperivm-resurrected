// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/leagueteam.h"
#include "game/civs.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_seasons.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/leaguemastery.h"
#include "ui/leaguemodsprep.h"
#include "ui/leaguerewards.h"
#include "ui/leagueui.h"
#include "ui/patronui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kGap = 24, kBandH = 112;
const COLORREF kWinBack = RGB(0x24, 0x30, 0x20), kLossBack = RGB(0x3a, 0x18, 0x1a);

// The server's clock: the times of the league (the season, the search, the lock, the games) are its own.
int64_t EpochNow()
{
	return LeagueServerNowMs();
}

// "40 min ago", "3 h ago", "yesterday" or the date.
std::string AgoText(int64_t at, int64_t now)
{
	const int64_t minutes = std::max<int64_t>(0, (now - at) / 60000);
	if (minutes < 1) return Tr("just now");
	if (minutes < 60) return TrFmt("%s min ago", {std::to_string(minutes)});
	if (minutes < 24 * 60) return TrFmt("%s h ago", {std::to_string(minutes / 60)});
	if (minutes < 48 * 60) return Tr("yesterday");
	return DateText(LocalTime(at));
}

void Tile(Painter &p, int x, int y, int w, int h, const std::string &value, const std::string &label, COLORREF ink = pal::text)
{
	p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, pal::hairline);
	// A short tile, on a short screen, takes a smaller figure so the label keeps its line.
	Display(p, h < 70 ? 28 : 34);
	p.raw = true;
	p.Text(x, y + 4, w, h - 30, value, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	// A long label ("% de victorias") closes its letters, then takes the smaller type, so it keeps a margin inside the tile.
	const int room = w - 20;
	p.Font(4);
	int spacing = 2;
	if (p.CapsWidth(label, spacing) > room) spacing = 1;
	if (p.CapsWidth(label, spacing) > room) {
		p.Font(3);
		if (p.CapsWidth(label, 1) > room) SelectObject(p.dc, BrandFont(BrandFace::Ui, 11, p.s));
		p.TextCaps(x + 10, y + h - 26, room, 18, label, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		return;
	}
	p.TextCaps(x + 10, y + h - 26, room, 18, label, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, spacing);
}

// A chip as wide as its text and its margins.
int ChipWidth(Painter &p, const std::string &text, int minW = 0)
{
	p.Font(4);
	return std::max(minW, p.CapsWidth(text, 1) + 28);
}

std::string CivText(const std::string &id)
{
	return LeagueCivName(id);
}

// "Victory", "Defeat", "Void", "Pending" or "In dispute", with the ink and the back of its chip.
const char *ResultLabel(LeagueGameResult r, COLORREF &ink, COLORREF &back)
{
	switch (r) {
	case LeagueGameResult::Win:
		ink = pal::ok;
		back = kWinBack;
		return "Victory";
	case LeagueGameResult::Loss:
		ink = pal::bad;
		back = kLossBack;
		return "Defeat";
	case LeagueGameResult::Void:
		ink = pal::textDim;
		back = pal::hairline;
		return "Void";
	case LeagueGameResult::Disputed:
		ink = pal::warn;
		back = RGB(0x3a, 0x31, 0x18);
		return "In dispute";
	default:
		ink = pal::caption;
		back = pal::hairline;
		return "Pending";
	}
}

// What a game gave: the Glory, "Placed" for the one that ended the placement, nothing when it did not count (yet).
std::string DeltaText(const LeagueHistoryGame &g, COLORREF &ink)
{
	ink = pal::caption;
	if (g.placed) {
		ink = pal::text;
		return Tr("Placed");
	}
	if (!g.hasDelta) return g.result == LeagueGameResult::Pending ? std::string("\xE2\x80\xA6") : std::string("\xE2\x80\x94");
	ink = g.gloryDelta >= 0 ? pal::ok : pal::bad;
	return SignedText(g.gloryDelta);
}

// A game's result in its chip, at least `minW` wide and `chipH` tall, centred on the row; returns how wide it came out.
int PaintResultChip(Painter &p, LeagueGameResult result, int x, int ry, int rowH, int minW, int chipH)
{
	COLORREF ink = pal::text, back = pal::hairline;
	const char *label = ResultLabel(result, ink, back);
	const int w = std::max(minW, ChipWidth(p, label));
	Chip(p, x, ry + (rowH - chipH) / 2, w, chipH, label, ink, back);
	return w;
}

// What a game gave, after its chip.
void PaintDelta(Painter &p, const LeagueHistoryGame &g, int x, int y, int w, int h)
{
	COLORREF ink = pal::caption;
	const std::string delta = DeltaText(g, ink);
	if (g.placed) Caps(p, x, y, w, h, delta, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
	else {
		Display(p, 26);
		RawText(p, x, y, w, h, delta, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	}
}

// "Rome vs Carthage", a "?" for a civilization the reports did not give.
std::string CivsText(const LeagueHistoryGame &g)
{
	return (g.myCiv.empty() ? std::string("?") : CivText(g.myCiv)) + " " + Tr("vs") + " " + (g.opponentCiv.empty() ? std::string("?") : CivText(g.opponentCiv));
}

// "Livia and Brennus": both opponents of a 2v2 game.
std::string OpponentsText(const LeagueHistoryGame &g)
{
	auto name = [](const LeaguePlayerCard &c) { return c.name.empty() ? Tr("Opponent") : c.name; };
	if (g.opponents.size() < 2) return name(g.opponent);
	return TrFmt("%s and %s", {name(g.opponents[0]), name(g.opponents[1])});
}

// "Mountain passes · Rome vs Carthage · 24 min".
std::string GameFacts(const LeagueHistoryGame &g)
{
	std::string facts = g.map.name.empty() ? std::string() : LeagueMapText(g.map.name);
	if (!g.myCiv.empty() || !g.opponentCiv.empty()) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + CivsText(g);
	if (g.durationMs) facts += (facts.empty() ? "" : "  \xC2\xB7  ") + TrFmt("%s min", {std::to_string(g.durationMs / 60000)});
	return facts;
}

// A player's name with the patron's laurel on its left and, where asked, the badge on its right.
// A leaderboard's name, with the badge of the civilization of the player's highest mastery after their league badge, from Legionario
// on (everyone who played has a Tiro).
void PaintName(Painter &p, int x, int y, int w, int h, const std::string &name, uint64_t id, const LeagueBadge &badge, bool you, int mx, int my,
	const std::string &masteryCiv = std::string(), const LeagueMastery &mastery = LeagueMastery())
{
	const PatronInfo patron = id ? PatronOf(id) : PatronInfo();
	const int laurel = patron.tier != PatronTier::None ? 20 : 0;
	const int nameX = x + (laurel ? laurel + 4 : 0);
	const int youW = you ? 40 : 0;
	const int masterySize = mastery.present && mastery.level >= 1 && !masteryCiv.empty() ? 18 : 0, masteryRoom = masterySize ? masterySize + 6 : 0;
	const int nameW = std::max(20, w - (nameX - x) - LeagueBadgeRoom(badge) - masteryRoom - (youW ? youW + 8 : 0));
	p.Font(0);
	p.raw = true;
	const int textW = std::min(nameW, p.TextWidth(name));
	if (laurel) DrawPatron(p, x, y + (h - laurel) / 2, laurel, nameX, y, nameW, h, name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	else p.Text(nameX, y, nameW, h, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	SteamProfileHotspot(p, nameX, y, nameW, h, name, id, mx, my);
	DrawBadgeAfterName(p, nameX, y, nameW, h, name, badge, mx, my, id);
	p.raw = false;
	if (masterySize) {
		const int bx = nameX + textW + LeagueBadgeRoom(badge) + 6, by = y + (h - masterySize) / 2;
		DrawMasteryBadge(p, bx, by, masterySize, mastery.level);
		HoverMastery(p, bx, by, masterySize, masteryCiv, mastery, mx, my);
	}
	if (you) Chip(p, nameX + textW + LeagueBadgeRoom(badge) + masteryRoom + 8, y + (h - 20) / 2, youW, 20, "You", pal::textDim, pal::hairline);
}

// "Season peak: Centurion I · 1012 Glory · 42 games", the rank in its colour; before there is a peak, the season and its games.
void PaintPeakLine(Painter &p, const LeagueRanked &rk, int x, int y, int w)
{
	const int games = rk.wins + rk.losses;
	const std::string count = games == 1 ? Tr("1 game") : TrFmt("%s games", {std::to_string(games)});
	p.Font(3);
	if (!rk.peak.present || rk.peak.tier == LeagueTier::None) {
		RawText(p, x, y, w, 22, TrFmt("Season %s", {LeagueRoman(rk.season)}) + "  \xC2\xB7  " + count, pal::caption);
		return;
	}
	const std::string lead = Tr("Season peak:") + " ", rank = RankName(rk.peak.tier, rk.peak.division);
	const std::string best = rk.peak.place > 0 && !LeagueTierHasDivisions(rk.peak.tier) ? "#" + std::to_string(rk.peak.place) : TrFmt("%s Glory", {std::to_string(rk.peak.glory)});
	p.raw = true;
	const int leadW = std::min(w, p.TextWidth(lead)), rankW = std::min(w - leadW, p.TextWidth(rank));
	// The games go first, then the Glory, where the line is short.
	std::string rest = "  \xC2\xB7  " + best + "  \xC2\xB7  " + count;
	if (leadW + rankW + p.TextWidth(rest) > w) rest = "  \xC2\xB7  " + best;
	if (leadW + rankW + p.TextWidth(rest) > w) rest.clear();
	p.raw = false;
	RawText(p, x, y, leadW, 22, lead, pal::caption);
	RawText(p, x + leadW, y, rankW, 22, rank, Ink(rk.peak.tier));
	RawText(p, x + leadW + rankW, y, std::max(0, w - leadW - rankW), 22, rest, pal::caption);
}

struct HubLayout {
	int m = 64, top = 0, bottom = 0, cardY = 0, cardH = 0, leftW = 520, midX = 0, midW = 0, rightX = 0, rightW = 600, inner = 0;
};

HubLayout HubFor(const State &st)
{
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH, false);
	HubLayout h;
	h.m = l.margin;
	h.top = l.top;
	h.bottom = l.h - l.margin;
	h.inner = l.w - 2 * l.margin;
	h.cardY = h.top + kBandH + kGap;
	h.cardH = h.bottom - h.cardY;
	// The columns keep the mockup's shares of the width (520, 624 and 600 of 1744); on a narrow screen the side ones give up width
	// before the middle one goes under what its rows need.
	const int avail = h.inner - 2 * kGap, minSide = 400, minMid = 340;
	h.leftW = std::max(minSide, avail * 520 / 1744);
	h.rightW = std::max(minSide, avail * 600 / 1744);
	int missing = minMid - (avail - h.leftW - h.rightW);
	if (missing > 0) {
		const int fromRight = std::min(missing, std::max(0, h.rightW - minSide + 40));
		h.rightW -= fromRight;
		h.leftW -= std::min(missing - fromRight, std::max(0, h.leftW - minSide + 40));
	}
	h.midX = h.m + h.leftW + kGap;
	h.midW = avail - h.leftW - h.rightW;
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
		// While the season is read, its number and its days show as the shape they will take.
		// After a failure the retries show the failure, not the shape of a season that may never come.
		const bool waiting = v.seasonLoading && !v.seasonFailed;
		if (waiting) PaintSkeleton(p, x + 120, y + 17, 260, 12);
		else Caps(p, x + 120, y + 14, 700, 18, "Ranked season", pal::redText);
		Display(p, 52);
		p.TextCaps(x + 120, y + 34, w - 160, 52, v.noSeason ? "No season yet" : LeagueQueueIsTeam(v.queue) ? "Ranked 2 vs 2" : "Ranked 1 vs 1", pal::text,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
		if (waiting) {
			PaintSkeleton(p, x + 120, y + 89, 380, 12);
			return;
		}
		p.Font(0);
		p.Text(x + 120, y + 84, w - 160, 22, v.noSeason ? "The first season starts soon." : "Couldn't reach the league server. Check your connection and try again.",
			pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	const LeagueSeason &s = v.season;
	const int64_t now = EpochNow();
	const bool started = s.startsAt <= now, over = s.endsAt <= now;
	std::string state = TrFmt("Season %s", {LeagueRoman(s.number)}) + "  \xC2\xB7  " +
		(over ? Tr("finished") : started ? Tr("in progress") : TrFmt("starts on %s", {DateText(LocalTime(s.startsAt))}));
	p.raw = true;
	Caps(p, x + 120, y + 14, 700, 18, state, pal::redText);
	p.raw = false;
	int nx = x + 120;
	if (s.beta) {
		p.Font(4);
		const int cw = p.CapsWidth("Test season", 1) + 20;
		Chip(p, x + 120 + std::min(560, p.CapsWidth(state, 2)) + 16, y + 12, cw, 22, "Test season", pal::warn, RGB(0x3a, 0x31, 0x18));
	}
	// "How it works", left of the days.
	const int rx = x + w - 342;
	p.Font(4);
	const int guideW = p.CapsWidth("How it works", 2) + 40, guideX = rx - 32 - guideW;
	Ghost(p, guideX, y + (kBandH - 44) / 2 - 8, guideW, 44, "How it works", kRkGuide, true, st.mouseX, st.mouseY);
	Display(p, 52);
	p.raw = true;
	const std::string name = LeagueSeasonName(s, v.lang);
	const int nameW = std::max(0, std::min(guideX - 16 - nx, p.CapsWidth(name, 1) + 6));
	p.TextCaps(nx, y + 32, nameW, 52, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	nx += nameW + 16;
	if (!s.theme.empty() && s.theme != "beta") {
		p.Font(0);
		RawText(p, nx, y + 44, std::max(0, guideX - 16 - nx), 30, s.theme, pal::textDim);
	}
	const bool team = LeagueQueueIsTeam(v.queue);
	const size_t maps = team ? s.maps2v2.size() : s.maps.size();
	std::string facts = DateText(LocalTime(s.startsAt)) + " \xE2\x80\x93 " + DateText(LocalTime(s.endsAt)) + "  \xC2\xB7  " + Tr(team ? "Ranked 2 vs 2" : "Ranked 1 vs 1");
	if (maps) facts += "  \xC2\xB7  " + TrFmt("%s maps", {std::to_string(maps)});
	p.Font(0);
	RawText(p, x + 120, y + 84, w - 520, 22, facts, pal::caption);
	// The days left, with how much of the season has gone.
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
	RawText(p, rx, y + 88, 318, 20, TrFmt("Ends: %s", {DateText(LocalTime(s.endsAt), true)}), pal::caption);
}

// The lock out of the queue: until when (server time) and why.
LeagueBan BanOf(const LeagueRankedView &v, const LeagueMe &me)
{
	LeagueBan ban = me.ban;
	if (v.banUntil > ban.until) {
		ban.until = v.banUntil;
		ban.kind = v.banKind;
		ban.reason = v.banReason;
	}
	return ban;
}

// The line over the big button before searching: who searches and plays now, or why the queue is locked.
void PaintPopulation(Painter &p, const LeagueRankedView &v, const LeagueBan &ban, int x, int y, int w)
{
	p.Font(3);
	if (ban.until > EpochNow()) {
		const std::string why = ban.kind == "staff" && !ban.reason.empty() ? TrFmt("The league's team has locked your search: %s", {ban.reason}) : Tr(LeagueBanText(ban.kind));
		RawText(p, x, y, w, 22, why, pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	if (!v.statsLoaded) return;
	const LeagueQueueStats s = LeagueStatsOf(v.stats, v.queue);
	if (s.searching == 0) return;
	const std::string line = (s.searching == 1 ? Tr("1 searching") : TrFmt("%s searching", {std::to_string(s.searching)})) + "  \xC2\xB7  " +
		(s.playing == 1 ? Tr("1 game being played") : TrFmt("%s games being played", {std::to_string(s.playing)})) + "  \xC2\xB7  " +
		(s.gamesToday == 1 ? Tr("1 game today") : TrFmt("%s games today", {std::to_string(s.gamesToday)}));
	RawText(p, x, y, w, 22, line, pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// What the hub says under the big button: the lock after declining, the last thing that happened, or what the queue is.
void PaintQueueLine(Painter &p, const LeagueRankedView &v, const LeagueDuoView &duo, int64_t ban, int x, int y, int w)
{
	const int64_t now = EpochNow();
	p.Font(3);
	if (ban > now) {
		RawText(p, x, y, w, 22, TrFmt("You can search again at %s.", {TimeText(LocalTime(ban))}), pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	if (!v.notice.empty()) {
		p.Text(x, y, w, 22, v.notice, v.noticeBad ? pal::bad : pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	if (LeagueQueueIsTeam(v.queue)) {
		const std::string name = !duo.partner.name.empty() ? duo.partner.name : Tr("your friend");
		if (duo.role == LeagueDuoRole::Solo) return;
		std::string line;
		COLORREF ink = pal::caption;
		if (duo.role == LeagueDuoRole::Waiting) {
			line = TrFmt("Waiting for %s to accept your invitation.", {name});
			ink = pal::warn;
		}
		else if (duo.role == LeagueDuoRole::Leader) line = TrFmt("You search for your duo with %s.", {name});
		else if (duo.role == LeagueDuoRole::Member) line = TrFmt("%s leads your duo and searches for both of you.", {name});
		RawText(p, x, y, w, 22, line, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	p.Text(x, y, w, 22, "The league picks the map and the options of every match.", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// Whether the season plays the 2v2 too: then the queues are two segments, otherwise the duel's chip alone.
bool BothQueues(const LeagueRankedView &v)
{
	return (v.seasonLoaded && LeagueSeasonHasQueue(v.season, kLeagueQueueTeam)) || LeagueQueueIsTeam(v.queue);
}

int QueueSegmentsWidth(Painter &p, const State &st)
{
	const int w = std::max(ChipWidth(p, RankedQueueLabel(kLeagueQueueDuel), 96), ChipWidth(p, RankedQueueLabel(kLeagueQueueTeam), 96));
	return BothQueues(st.ranked.view) ? 2 * w - 1 : w;
}

// The two queues, side by side, the one picked lit, ending at `right`.
void QueueSegments(Painter &p, const State &st, int right, int y, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	const bool both = BothQueues(v);
	const bool team = LeagueQueueIsTeam(v.queue);
	const std::string duel = RankedQueueLabel(kLeagueQueueDuel), pair = RankedQueueLabel(kLeagueQueueTeam);
	const int w = std::max(ChipWidth(p, duel, 96), ChipWidth(p, pair, 96));
	if (!both) {
		p.raw = true;
		Chip(p, right - w, y, w, h, duel, pal::text, pal::selected);
		p.raw = false;
		p.Frame(right - w, y, w, h, pal::edgeHot);
		return;
	}
	const int x = right - 2 * w + 1;
	p.raw = true;
	Segment(p, x, y, w, h, duel, !team, kRkQueueDuel, st.mouseX, st.mouseY, 1);
	Segment(p, x + w - 1, y, w, h, pair, team, kRkQueueTeam, st.mouseX, st.mouseY, 1);
	p.raw = false;
}

// The opponents the league takes now (2-clasificatoria-sistema.md §2.4): the ranks at both ends of the Glory window, then every division
// on a bar, those inside the window lit and the player's own framed. Only the ranks when the bar does not fit above `bottom`. Returns the y
// after it.
int PaintSearchWindow(Painter &p, const LeagueSearchWindow &win, const LeagueRanked &rk, int x, int y, int w, int bottom)
{
	const int lo = win.from > 0 ? LeagueDivisionIndex(win.from) : 0;
	const int hi = win.to >= 0 ? LeagueDivisionIndex(win.to) : kLeagueDivisionCount - 1;
	const int first = std::min(lo, hi), last = std::max(lo, hi);
	Caps(p, x, y, w / 3, 22, "Opponents", pal::caption);
	// The two ends against the right edge, each in its rank's ink.
	Display(p, 22);
	p.raw = true;
	const UINT one = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	const int left = x + w / 3;
	if (first == 0 && last == kLeagueDivisionCount - 1) p.TextCaps(left, y, x + w - left, 22, Tr("Any rank"), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	else {
		int right = x + w;
		auto end = [&](int index) {
			const LeagueTier tier = LeagueIndexTier(index);
			const std::string name = LeagueRankLabel(tier, LeagueIndexDivision(index));
			const int nw = std::min(p.CapsWidth(name, 1) + 2, right - left);
			p.TextCaps(right - nw, y, nw, 22, name, LeagueTierInk(tier), one, 1);
			right -= nw;
		};
		end(last);
		if (last != first) {
			const std::string dash = " \xE2\x80\x93 ";
			const int dw = p.TextWidth(dash);
			p.Text(right - dw, y, dw, 22, dash, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			right -= dw;
			end(first);
		}
	}
	p.raw = false;
	const int n = kLeagueDivisionCount, gap = 2, segH = 16;
	const int segW = (w - (n - 1) * gap) / n, barW = n * segW + (n - 1) * gap, bx = x + (w - barW) / 2;
	const int barY = y + 22 + 16, labelsY = barY + segH + 5, labelsH = 16;
	if (segW < 6 || labelsY + labelsH > bottom) return y + 26;
	auto segX = [&](int i) { return bx + i * (segW + gap); };
	for (int i = 0; i < n; i++) PaintTint(p, segX(i), barY, segW, segH, LeagueTierInk(LeagueIndexTier(i)), i >= first && i <= last ? 255 : 72);
	// The window's bracket over the lit divisions.
	const int ax = segX(first), az = segX(last) + segW;
	p.Fill(ax, barY - 8, az - ax, 2, pal::text);
	p.Fill(ax, barY - 12, 2, 6, pal::text);
	p.Fill(az - 2, barY - 12, 2, 6, pal::text);
	// The player's own division, once placed.
	if (rk.present && rk.placementLeft <= 0) {
		const int ox = segX(LeagueDivisionIndex(rk.glory));
		p.Frame(ox - 2, barY - 2, segW + 4, segH + 4, pal::text);
		p.Frame(ox - 1, barY - 1, segW + 2, segH + 2, pal::text);
	}
	// Each rank's name under its three divisions, lit when the window reaches it.
	Display(p, 15);
	p.raw = true;
	for (int t = 0; t < n / 3; t++) {
		const LeagueTier tier = LeagueIndexTier(t * 3);
		const bool lit = t * 3 + 2 >= first && t * 3 <= last;
		p.TextCaps(segX(t * 3), labelsY, 3 * segW + 2 * gap, labelsH, Tr(LeagueTierName(tier)), lit ? LeagueTierInk(tier) : pal::disabled,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	}
	p.raw = false;
	return labelsY + labelsH;
}

// The content the search was let in with: the mounted profile, which the season's queue checked. The y after it.
int PaintSearchContent(Painter &p, int x, int y, int w)
{
	p.Fill(x, y, w, 1, pal::hairline);
	const int ry = y + 8, rh = 26;
	p.Font(0);
	p.Text(x, ry, w / 3, rh, "Content", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const std::string label = ModsProfileLabel(ModsMountedProfile().name);
	p.raw = true;
	const int lw = std::min(p.TextWidth(label) + 2, w - w / 3 - 20);
	p.Text(x + w - lw, ry, lw, rh, label, pal::ok, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	const int tx = x + w - lw - 18, ty = ry + rh / 2;
	p.Line(tx, ty, tx + 4, ty + 4, pal::ok, 2);
	p.Line(tx + 4, ty + 4, tx + 12, ty - 5, pal::ok, 2);
	return ry + rh;
}

// The player's card on the left. In 2v2 the duo goes in it too, over the search button, when it fits there; returns whether it did.
bool PaintRankCard(Painter &p, const State &st, const HubLayout &h, const LeagueMeView &me, const LeagueDuoView &duo)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const LeagueRanked &rk = LeagueMeRanked(me.me, v.queue);
	const bool team = LeagueQueueIsTeam(v.queue);
	const int x = h.m, y = h.cardY, w = h.leftW, cardH = h.cardH, ix = x + 24, iw = w - 48, cx = x + w / 2;
	PaintGlassCard(p, x, y, w, cardH);
	const bool searching = v.phase == LeagueRankedPhase::Joining || v.phase == LeagueRankedPhase::Searching;
	const bool matched = v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted || v.phase == LeagueRankedPhase::Ready ||
		v.phase == LeagueRankedPhase::Playing;
	const LeagueTier tier = rk.present ? rk.tier : LeagueTier::None;
	const int64_t now = EpochNow();

	if (searching) {
		Caps(p, ix, y + 20, iw, 20, v.partnerSearch ? "Your partner is searching" : LeagueQueueIsTeam(v.searchQueue) ? "Searching for a match  2 vs 2" : "Searching for a match",
			pal::redText);
		// The emblem inside its spinner, smaller on a short card (900 lines) so the lines under it keep their room.
		const bool tall = cardH >= 600;
		const int emblem = tall ? 160 : 120, ring = emblem * 108 / 160, ringY = y + 60 + ring;
		PaintSearchRing(p, static_cast<float>(cx), static_cast<float>(ringY), static_cast<float>(ring), 5.0f, pal::redText, GetTickCount());
		DrawLeagueEmblem(p, cx - emblem / 2, ringY - emblem / 2, emblem, tier);
		const int clockY = ringY + ring + 8, clockH = tall ? 84 : 70;
		Display(p, clockH);
		p.raw = true;
		p.Text(x, clockY, w, clockH, LeagueClockText(now - v.since), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		p.Font(0);
		std::string wait = v.phase == LeagueRankedPhase::Joining ? Tr("Entering the queue...")
															   : TrFmt("Estimated wait ~%s", {LeagueClockText(static_cast<int64_t>(v.estimateSec) * 1000)}) + "  \xC2\xB7  " +
				(v.searching == 1 ? Tr("1 searching now") : TrFmt("%s searching now", {std::to_string(v.searching)}));
		int ly = clockY + clockH;
		RawText(p, x, ly, w, 24, wait, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ly += 28;
		const int cancelY = y + cardH - 118, bottom = cancelY - 10;
		if (v.window.present) ly = PaintSearchWindow(p, v.window, LeagueMeRanked(me.me, v.searchQueue), ix, ly + 8, iw, bottom) + 8;
		p.Font(3);
		if (ly + 20 <= bottom) {
			p.Text(ix, ly, iw, 20, "The range widens the longer you wait: after 5 minutes, any rank.", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			ly += 30;
		}
		if (ly + 34 <= bottom) PaintSearchContent(p, ix, ly, iw);
		// The partner of a duo follows the leader's search: only the leader cancels it.
		if (v.partnerSearch) {
			p.Font(0);
			p.TextWrapped(ix, cancelY, iw, 48, "Only the leader of your duo can cancel the search.", pal::body);
		}
		else Ghost(p, ix, cancelY, iw, 48, "Cancel search", kRkCancel, v.phase == LeagueRankedPhase::Searching, mx, my);
		// The staging server offers a practice bot, to try a match alone: the game's AI plays it.
		if (v.practiceBot) Ghost(p, ix, cardH + y - 60, iw, 40, "Play against a bot (test)", kRkPracticeBot, true, mx, my);
		else {
			p.Font(3);
			p.TextWrapped(ix, cardH + y - 62, iw, 44, "You can keep looking at rooms: a sound plays and the window flashes when a match is found.", pal::caption);
		}
		return false;
	}

	if (matched) {
		Caps(p, ix, y + 20, iw, 20, "Your ranked match", pal::redText);
		const std::vector<LeagueMatchTeam> teams = MatchTeams(v);
		const std::string own = std::to_string(SteamOwnId());
		const bool teamMatch = LeagueQueueIsTeam(v.searchQueue) || (v.ticketLoaded && LeagueQueueIsTeam(v.ticket.queue)) || !v.found.teams.empty();
		if (teamMatch) {
			// A 2v2: the player's own rank, with whom and against whom.
			const LeagueRanked &mine = LeagueMeRanked(me.me, kLeagueQueueTeam);
			DrawLeagueEmblem(p, cx - 80, y + 54, 160, mine.present ? mine.tier : LeagueTier::None);
			const LeaguePlayerCard *partner = LeagueTeamPartner(teams, own);
			const std::vector<LeaguePlayerCard> others = LeagueTeamOpponents(teams, own);
			auto nameOf = [](const LeaguePlayerCard &c, const char *fallback) { return c.name.empty() ? Tr(fallback) : c.name; };
			Display(p, 40);
			p.raw = true;
			p.Text(x + 16, y + 222, w - 32, 46, TrFmt("With %s", {partner ? nameOf(*partner, "your partner") : Tr("your partner")}), pal::text,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
			p.Font(0);
			const std::string against = others.size() >= 2 ? TrFmt("Against %s and %s", {nameOf(others[0], "Opponent"), nameOf(others[1], "Opponent")}) : Tr("Against the other team");
			RawText(p, x + 16, y + 268, w - 32, 22, against, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		else {
			DrawLeagueEmblem(p, cx - 80, y + 54, 160, v.opponent.tier);
			Display(p, 44);
			p.raw = true;
			const std::string other = v.opponent.name.empty() ? Tr("Opponent") : v.opponent.name;
			p.Text(x + 16, y + 226, w - 32, 50, TrFmt("Against %s", {other}), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		p.Font(0);
		const bool inRoom = st.room.id != 0;
		std::string line = v.phase == LeagueRankedPhase::Found || v.phase == LeagueRankedPhase::Accepted
			? Tr(LeagueQueueIsTeam(v.searchQueue) ? "Waiting for the four players to accept..." : "Waiting for both players to accept...")
			: v.ticketLoaded && v.ticket.startedAt && !inRoom												   ? Tr("Your game is on: go back to it or give it up.")
			: inRoom																			   ? Tr("You are in the room: get ready and start.")
			: v.host																			   ? Tr("Creating the room...")
																										   : Tr("Waiting for the host's room...");
		p.Font(0);
		const int lineY = teamMatch ? y + 294 : y + 280;
		p.Text(x + 16, lineY, w - 32, 24, line, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (!v.ticket.map.name.empty()) {
			p.Font(3);
			RawText(p, x + 16, lineY + 28, w - 32, 22, TrFmt("Map: %s", {LeagueMapText(v.ticket.map.name)}), pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if ((v.phase == LeagueRankedPhase::Ready || v.phase == LeagueRankedPhase::Playing) && !inRoom) {
			p.Button(ix, y + cardH - 196, iw, 56, v.ticketLoaded && v.ticket.startedAt ? "Back to the game" : "Go to the room", kRankedAct, kRkRoom, mx, my, !st.connecting, true);
			// A 1v1 whose game started and closed on this player: they go back to it, or give it up (a loss, the opponent wins at once).
			const bool started = v.ticketLoaded && v.ticket.startedAt && !LeagueQueueIsTeam(v.ticket.queue);
			if (started) {
				Ghost(p, ix, y + cardH - 128, iw, 44, v.abandoning ? "Giving up..." : "Give up the match", kRkSurrender, !v.abandoning, mx, my);
				p.Font(3);
				p.TextWrapped(ix, y + cardH - 72, iw, 44, "If you can't go back to the game, give it up: it counts as a loss and your opponent wins.", pal::caption);
			}
			else {
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
		}
		return false;
	}

	Caps(p, ix, y + 20, iw / 3, 20, "Your rank", pal::redText);
	QueueSegments(p, st, x + w - 24, y + 14, 32);
	const bool placing = !rk.present || rk.placementLeft > 0;
	// Where the parts go: the mockup's places on a 1080 screen, closer together on a shorter one, so the button keeps its room.
	// In 2v2 the duo needs room over the button: the emblem is the small one, as on a short screen.
	const bool tight = cardH < 640 || team;
	const int emblem = tight ? 104 : 160, emblemY = y + (tight ? 50 : 54), nameY = emblemY + emblem + 2, nameH = tight ? 52 : 62;
	const int lineY = nameY + nameH, barY = lineY + 40, labelsY = barY + 16, tileH = tight ? 60 : 72;
	int tilesY = labelsY + (tight ? 32 : 40);
	DrawLeagueEmblem(p, cx - emblem / 2, emblemY, emblem, tier);
	Display(p, tight ? 50 : 60);
	p.raw = true;
	// While the standing is read, the shape of a rank; when it can't be read, that: never a "Placement" and a 0-0 that are not this player's.
	const bool waiting = !me.loaded && me.loading && !me.failed;
	if (me.loaded) {
		p.TextCaps(x, nameY, w, nameH, placing ? Tr("Placement") : RankName(tier, rk.division), Ink(placing ? LeagueTier::None : tier),
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	}
	else if (waiting) PaintSkeleton(p, cx - 120, nameY + nameH / 2 - 14, 240, 28);
	p.raw = false;
	p.Font(0);
	if (!me.loaded) {
		if (!waiting) {
			p.Text(x + 16, lineY, w - 32, 48, me.failed ? "Couldn't reach the league server. Check your connection and try again." : "Your rank will show here.",
				pal::caption, DT_CENTER | DT_WORDBREAK);
		}
	}
	else if (placing) {
		const int left = rk.present ? rk.placementLeft : 5, played = std::max(0, 5 - left);
		RawText(p, x, lineY, w, 24, left == 1 ? Tr("1 placement game left") : TrFmt("%s placement games left", {std::to_string(left)}), pal::body,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		// One box a game, green for a win and red for a loss, in the order they were played.
		const int box = 56, gap = 12, bx = cx - (5 * box + 4 * gap) / 2;
		for (int i = 0; i < 5; i++) {
			COLORREF fill = i < played ? pal::textDim : pal::bg;
			if (i < static_cast<int>(rk.placement.size())) {
				const LeaguePlacementMark mark = rk.placement[i];
				fill = mark == LeaguePlacementMark::Win ? pal::ok : mark == LeaguePlacementMark::Loss ? pal::bad : pal::disabled;
			}
			p.Fill(bx + i * (box + gap), barY, box, 12, fill);
			p.Frame(bx + i * (box + gap), barY, box, 12, pal::hairline);
		}
		p.Font(3);
		const char *about = "Your first five games place you, at most in Centurion I.";
		const int aboutH = p.Measure(iw, about);
		p.TextWrapped(ix, barY + 22, iw, aboutH + 2, about, pal::caption);
		tilesY = std::max(tilesY, barY + 22 + aboutH + 10);
	}
	else {
		const int total = st.ranked.view.top.total;
		std::string line = TrFmt("%s Glory", {std::to_string(rk.glory)});
		// The place only motivates in the top half; below it the bar says more.
		if (LeaguePlaceShown(rk.position, total)) line += "  \xC2\xB7  " + TrFmt("place %s of %s", {std::to_string(rk.position), std::to_string(total)});
		RawText(p, x, lineY, w, 24, line, pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		const LeagueGloryPlace gp = LeagueGloryPlaceOf(rk.glory);
		const bool elite = !LeagueTierHasDivisions(tier);
		p.Fill(ix, barY, iw, 10, pal::bg);
		const float frac = gp.capped && !elite ? gp.into / 100.0f : 1.0f;
		p.Fill(ix, barY, static_cast<int>(iw * frac), 10, Ink(tier));
		p.Font(0);
		if (elite || !gp.capped) {
			RawText(p, ix, labelsY, iw, 22, elite ? Tr("Elite rank: it goes by your place in the leaderboard.") : Tr("Top division: climb the leaderboard to the elite."),
				pal::caption);
		}
		else {
			// The Glory into the division on the left, and the wins to the next one with the rest of the line.
			const std::string into = std::to_string(gp.into) + " / 100 " + Tr("Glory");
			p.raw = true;
			const int intoW = std::min(iw / 2, p.TextWidth(into) + 4);
			p.raw = false;
			RawText(p, ix, labelsY, intoW, 22, into, pal::caption);
			LeagueTier nt = LeagueTier::None;
			int nd = 0;
			LeagueNextDivision(gp.tier, gp.division, nt, nd);
			if (nt != LeagueTier::None) {
				const int wins = LeagueWinsToNext(rk.glory);
				RawText(p, ix + intoW + 12, labelsY, iw - intoW - 12, 22,
					wins == 1 ? TrFmt("~1 win to %s", {LeagueRankLabel(nt, nd)}) : TrFmt("~%s wins to %s", {std::to_string(wins), LeagueRankLabel(nt, nd)}), Ink(nt),
					DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
		}
	}
	// Record, win rate and the streak (W3, L2).
	const int tw = (iw - 2 * 20) / 3;
	int peakY = tilesY + tileH + 16;
	if (waiting) {
		for (int k = 0; k < 3; k++) PaintSkeleton(p, ix + k * (tw + 20), tilesY, tw, tileH);
	}
	else if (me.loaded) {
		Tile(p, ix, tilesY, tw, tileH, std::to_string(rk.wins) + "\xE2\x80\x93" + std::to_string(rk.losses), "Record");
		Tile(p, ix + tw + 20, tilesY, tw, tileH, std::to_string(LeagueWinRate(rk.wins, rk.losses)) + " %", "Win rate");
		Tile(p, ix + 2 * (tw + 20), tilesY, tw, tileH, LeagueStreakText(rk.streak), "Streak", rk.streak > 0 ? pal::ok : rk.streak < 0 ? pal::bad : pal::text);
		if (rk.shield > 0) {
			p.Font(0);
			p.Fill(ix, tilesY + tileH + 10, iw, 28, RGB(0x22, 0x2c, 0x1e));
			RawText(p, ix + 12, tilesY + tileH + 10, iw - 24, 28, rk.shield == 1 ? Tr("Rank shield: 1 more game") : TrFmt("Rank shield: %s more games", {std::to_string(rk.shield)}),
				pal::ok);
			peakY += 28;
		}
		PaintPeakLine(p, rk, ix, peakY, iw);
	}
	const LeagueBan ban = BanOf(v, me.me);
	// In 2v2, alone or as the leader of a ready duo; the season has to play the queue.
	const LeagueDuoRole role = team ? duo.role : LeagueDuoRole::Solo;
	const bool queueOpen = !team || (v.seasonLoaded && LeagueSeasonHasQueue(v.season, kLeagueQueueTeam));
	// The gameplay mods the season is played with (a wrong_mods answer of the queue reads the season again).
	const LeagueModsRule &modsRule = v.season.mods;
	const bool modsRuled = v.seasonLoaded && v.season.active && modsRule.set;
	const LeagueModsCheck modsFit = modsRuled ? LeagueModsFitNow(modsRule) : LeagueModsCheck();
	const bool modsOk = modsFit.fit == LeagueModsFit::Ok;
	const LeagueModsPrepView prep = LeagueModsPrepState("ranked");
	const bool canSearch = v.linked && v.seasonLoaded && v.season.active && ban.until <= now && v.phase == LeagueRankedPhase::Idle && queueOpen &&
		(role == LeagueDuoRole::Solo || role == LeagueDuoRole::Leader) && modsOk;
	// In 2v2 the duo sits over the button, where the player looks before searching, if it fits under the figures.
	const int buttonY = y + cardH - 108, freeY = peakY + 30;
	bool duoHere = false;
	int popY = std::max(freeY, y + cardH - 138);
	if (team) {
		const int duoH = RankedDuoHeight(duo, buttonY - 12 - freeY);
		if (freeY + duoH <= buttonY - 12) {
			duoHere = true;
			const int duoY = buttonY - 12 - duoH;
			PaintRankedDuo(p, st, duo, x, duoY, w, duoH, true);
			popY = duoY - 30;
		}
	}
	// The season's mods take the line over the button, and who is around goes over them when there is room.
	bool modsShown = false;
	if (modsRuled) {
		const int modsY = popY;
		popY -= 26;
		if (modsY >= freeY - 8 && modsY + 22 <= buttonY - 4) {
			modsShown = true;
			std::string line = prep.line;
			COLORREF ink = prep.failed ? pal::bad : pal::ok;
			if (line.empty()) {
				line = modsOk ? LeagueModsRuleText(modsRule) : LeagueModsProblemText(modsRule, modsFit);
				ink = modsOk ? pal::body : pal::bad;
			}
			p.Font(3);
			RawText(p, ix, modsY, iw, 22, line, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	// Who is around, before searching.
	if (v.seasonLoaded && v.season.active && popY >= freeY - 8 && popY + 22 <= buttonY - 4) PaintPopulation(p, v, ban, ix, popY, iw);
	if (modsRuled && !modsOk) {
		// Gameplay mods are only mounted when the game starts: getting them right always restarts it.
		p.Button(ix, y + cardH - 108, iw, 64, "Prepare mods (restarts the game)", kRankedAct, kRkPrepareMods, mx, my, !prep.busy && v.phase == LeagueRankedPhase::Idle, true);
	}
	else {
		p.fonts[5] = BrandFont(BrandFace::Display, 34, p.s);
		p.Button(ix, y + cardH - 108, iw, 64, team ? "Search for a match  2 vs 2" : "Search for a match  1 vs 1", kRankedAct, kRkSearch, mx, my, canSearch, true, 5);
	}
	if (v.seasonLoaded && !v.season.active) {
		p.Font(3);
		p.Text(ix, y + cardH - 38, iw, 22, "The season is not being played now.", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else if (modsRuled && !modsShown && !prep.line.empty()) {
		// No room over the button (a short screen with the duo): the preparation's progress goes under it.
		p.Font(3);
		RawText(p, ix, y + cardH - 38, iw, 22, prep.line, prep.failed ? pal::bad : pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	else PaintQueueLine(p, v, duo, ban.until, ix, y + cardH - 38, iw);
	return duoHere;
}

const int kRecentRowH = 50, kRecentTop = 51;

// One ranked game in a row: result, Glory, the rival with their rank, the map and the civilizations, and when.
void PaintGameRow(Painter &p, const LeagueHistoryGame &g, int x, int ry, int w, int rowH, int64_t now, int mx, int my, int rightW)
{
	const int chipW = PaintResultChip(p, g.result, x, ry, rowH, 84, 24);
	const int deltaX = x + chipW + 12, deltaW = g.placed ? 90 : 56;
	PaintDelta(p, g, deltaX, ry, deltaW, rowH);
	// "When" takes its own width, and in a narrow row it moves down to the facts line so the name keeps the top one.
	const std::string ago = AgoText(g.playedAt, now);
	p.Font(3);
	p.raw = true;
	const int agoW = std::min(rightW, p.TextWidth(ago) + 12);
	p.raw = false;
	const int tx = deltaX + deltaW + 8, factsW = std::max(40, x + w - agoW - tx);
	// A narrow row has no room for "when" at its right: it leads the facts line instead.
	const bool stacked = factsW < 140;
	const int tw = stacked ? std::max(40, x + w - tx) : factsW;
	auto withAgo = [&](const std::string &facts) { return stacked ? (facts.empty() ? ago : ago + "  \xC2\xB7  " + facts) : facts; };
	if (g.opponents.size() >= 2) {
		p.Font(0);
		RawText(p, tx, ry + 3, tw, 26, OpponentsText(g), pal::text);
		std::string facts = TrFmt("with %s", {g.partner.name.empty() ? Tr("your partner") : g.partner.name});
		const std::string rest = GameFacts(g);
		if (!rest.empty()) facts += "  \xC2\xB7  " + rest;
		p.Font(3);
		RawText(p, tx, ry + 27, tw, 20, withAgo(facts), pal::caption);
	}
	else {
		const uint64_t id = _strtoui64(g.opponent.steamId.c_str(), nullptr, 10);
		PaintName(p, tx, ry + 3, tw, 26, g.opponent.name.empty() ? Tr("Opponent") : g.opponent.name, id, RankBadge(g.opponent.tier, g.opponent.division), false,
			mx, my);
		p.Font(3);
		RawText(p, tx, ry + 27, tw, 20, withAgo(GameFacts(g)), pal::caption);
	}
	p.Font(3);
	if (!stacked) RawText(p, x + w - agoW, ry, agoW, rowH, ago, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// The summary's mastery: every civilization the player rated a game with, the highest first, with its level, its bar to the next one
// (this season's wins in the lighter shade) and what the next level asks for.
void PaintMasteryList(Painter &p, const State &st, const LeagueMeView &me, int x, int y, int w, int cardH)
{
	const int mx = st.mouseX, my = st.mouseY, ix = x + 24, iw = w - 48;
	const std::vector<LeagueCivMastery> &list = me.me.mastery;
	const int rows = std::min<int>(static_cast<int>(list.size()), (cardH - kRecentTop - 4) / (kRecentRowH + 1));
	if (!rows) {
		p.Font(0);
		const char *text = !me.loaded ? "Reading your games..."
			: !me.me.masteryKnown	  ? "The league server does not count the mastery yet."
									  : "Win ranked games with a civilization to climb its mastery, from Tiro to Master.";
		p.TextWrapped(ix, y + 68, iw, 60, text, pal::caption);
		return;
	}
	for (int i = 0; i < rows; i++) {
		const LeagueCivMastery &c = list[i];
		const LeagueMastery &m = c.mastery;
		const COLORREF ink = LeagueMasteryInk(m.level);
		const int ry = y + kRecentTop + i * (kRecentRowH + 1), badge = 32, by = ry + (kRecentRowH - badge) / 2;
		DrawMasteryBadge(p, ix, by, badge, m.level);
		HoverMastery(p, ix, by, badge, c.civ, m, mx, my);
		const int tx = ix + badge + 14, tw = iw - badge - 14;
		// The civilization and its level on the first line, this season's wins at its end; the bar and the next level under them.
		const std::string season = c.seasonWins > 0 ? TrFmt("+%s this season", {std::to_string(c.seasonWins)}) : std::string();
		p.Font(3);
		p.raw = true;
		const int seasonW = season.empty() ? 0 : std::min(tw / 3, p.TextWidth(season) + 4);
		p.raw = false;
		p.Font(1);
		const std::string civ = LeagueCivName(c.civ);
		p.raw = true;
		const int civW = std::min(tw - seasonW - 12, p.TextWidth(civ) + 2);
		p.raw = false;
		RawText(p, tx, ry + 4, civW, 22, civ, pal::text);
		Caps(p, tx + civW + 10, ry + 4, std::max(0, tw - seasonW - civW - 22), 22, LeagueMasteryName(m.level), ink);
		if (seasonW) {
			p.Font(3);
			RawText(p, tx + tw - seasonW, ry + 4, seasonW, 22, season, pal::ok, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		const std::string next = LeagueMasteryNextText(m);
		p.Font(3);
		p.raw = true;
		const int nextW = std::min(tw / 2, p.TextWidth(next) + 4);
		p.raw = false;
		PaintMasteryBar(p, tx, ry + 34, tw - nextW - 12, 6, m, c.seasonWins);
		RawText(p, tx + tw - nextW, ry + 26, nextW, 22, next, m.Top() ? ink : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Fill(x + 1, ry + kRecentRowH, w - 2, 1, pal::hairline);
	}
	const int noteY = y + kRecentTop + rows * (kRecentRowH + 1) + 10;
	if (noteY + 40 <= y + cardH - 8) {
		p.Font(3);
		p.TextWrapped(ix, noteY, iw, 40, "The mastery counts your ranked wins with each civilization, in every season and queue.", pal::caption);
	}
}

void PaintRecent(Painter &p, const State &st, const HubLayout &h, const LeagueMeView &me, int y, int cardH)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const int x = h.midX, w = h.midW, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	// The card's two views under two titles, the one shown in red: the recent games, and the mastery of the civilizations.
	const bool mastery = st.ranked.hubMastery;
	p.Font(4);
	const int recentW = p.CapsWidth("Recent games", 2) + 4, masteryW = p.CapsWidth("Mastery", 2) + 4;
	auto title = [&](int tx, int tw, const char *label, bool on, int arg) {
		const bool hot = !on && p.Hover(tx - 6, y + 10, tw + 12, 34, mx, my);
		Caps(p, tx, y + 16, tw, 22, label, on ? pal::redText : hot ? pal::text : pal::caption);
		if (!on) p.AddHit(tx - 6, y + 10, tw + 12, 34, kRankedAct, arg);
	};
	title(ix, recentW, "Recent games", !mastery, kRkHubRecent);
	title(ix + recentW + 24, masteryW, "Mastery", mastery, kRkHubMastery);
	if (!mastery) {
		// "Full history →", a link on the title's line; only its arrow when the two titles leave no room for its words.
		p.Font(4);
		const int room = iw - recentW - 24 - masteryW - 16;
		const bool words = p.CapsWidth("Full history", 1) + 26 <= room;
		const int lw = words ? p.CapsWidth("Full history", 1) + 26 : 26, lx = ix + iw - lw;
		const bool hot = p.Hover(lx, y + 10, lw, 34, mx, my);
		if (words) p.TextCaps(lx, y + 16, lw - 20, 22, "Full history", hot ? pal::text : pal::redText, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 1);
		p.ArrowRight(lx + lw - 12, y + 21, 8, 12, hot ? pal::text : pal::redText);
		p.AddHit(lx, y + 10, lw, 34, kRankedAct, kRkHistory);
	}
	p.Fill(x + 1, y + 50, w - 2, 1, pal::hairline);
	if (mastery) {
		PaintMasteryList(p, st, me, x, y, w, cardH);
		return;
	}
	const int rows = std::min<int>({static_cast<int>(v.recent.size()), kLeagueRecentGames, (cardH - kRecentTop - 4) / (kRecentRowH + 1)});
	if (!rows) {
		p.Font(0);
		const char *text = !v.recentLoaded && v.recentFailed ? "Couldn't reach the league server. Check your connection and try again."
			: !v.recentLoaded									? "Reading your games..."
																: "Your ranked games will show here, with the Glory each one gave.";
		p.TextWrapped(ix, y + 68, iw, 60, text, pal::caption);
		return;
	}
	std::vector<uint64_t> ids;
	for (const LeagueHistoryGame &g : v.recent) ids.push_back(_strtoui64(g.opponent.steamId.c_str(), nullptr, 10));
	PatronsWatch(ids);
	const int64_t now = EpochNow();
	for (int i = 0; i < rows; i++) {
		const int ry = y + kRecentTop + i * (kRecentRowH + 1);
		PaintGameRow(p, v.recent[i], ix, ry, iw, kRecentRowH, now, mx, my, 110);
		p.Fill(x + 1, ry + kRecentRowH, w - 2, 1, pal::hairline);
	}
}

const int kRewardsMinH = 312;

// One of the three rewards of today, in a slot `w` wide: the picture on top, its name and what it is; dim while it is not won yet.
void PaintRewardSlot(Painter &p, int x, int y, int w, const std::string &name, const std::string &what, bool won)
{
	if (!won) PaintTint(p, x, y, w, 64, pal::raised, 170);
	// A narrow slot takes the smaller type before its name would be cut.
	Body(p, 15, true);
	p.raw = true;
	if (p.TextWidth(name) > w - 8) Body(p, 13, true);
	p.raw = false;
	RawText(p, x + 4, y + 66, w - 8, 20, name, won ? pal::text : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	RawText(p, x + 4, y + 86, w - 8, 18, what, won ? pal::caption : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// What the season would leave if it closed today: the games, the ranks lit up to the one of the rewards, the vexillum, the frame and the
// title, and the Glory to the next rank.
void PaintSeasonRewards(Painter &p, const State &st, const HubLayout &h, const LeagueMeView &me, int y, int cardH)
{
	const LeagueRankedView &v = st.ranked.view;
	const LeagueRanked &rk = LeagueMeRanked(me.me, v.queue);
	const LeagueRewardPreview today = LeagueRewardsToday(rk, v.seasonLoaded && v.season.beta);
	const int season = v.seasonLoaded ? v.season.number : rk.season;
	const int x = h.midX, w = h.midW, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	const std::string count = today.goal ? TrFmt("%s of %s games", {std::to_string(today.games), std::to_string(today.goal)}) + "  ·  " +
											 TrFmt("%s of %s wins", {std::to_string(today.wins), std::to_string(today.goalWins)})
										 : TrFmt("%s games: everything won", {std::to_string(today.games)});
	p.Font(3);
	p.raw = true;
	const int countW = std::min(iw / 2, p.TextWidth(count) + 4);
	p.raw = false;
	Caps(p, ix, y + 16, iw - countW - 12, 22, "Season rewards", pal::redText);
	RawText(p, ix + iw - countW, y + 16, countW, 22, count, today.goal ? pal::caption : pal::ok, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	const char *about = today.founder ? "This test season gives the Founder emblem to everyone who plays 10 games and wins 5 in it."
									   : "At the close you keep the rewards of the highest rank you reach.";
	// The line takes the smaller type before it would be cut.
	p.Font(0);
	if (p.TextWidth(about) > iw) p.Font(3);
	p.Text(ix, y + 42, iw, 24, about, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	// The ranks, lit up to the one of the rewards.
	const LeagueTier mine = today.tier;
	const int n = kLeagueTierCount, step = iw / n, track = y + 120, emblem = std::min(36, step - 4);
	// Too narrow for every rank's name: only the one of the rewards is written, wider, under its emblem.
	const bool allNames = step >= 56;
	const LeagueTier named = today.tier != LeagueTier::None ? today.tier : static_cast<LeagueTier>(1);
	p.Fill(ix + step / 2, track, step * (n - 1), 2, pal::lineStrong);
	if (mine != LeagueTier::None) p.Fill(ix + step / 2, track, step * (static_cast<int>(mine) - 1), 2, Ink(mine));
	for (int i = 0; i < n; i++) {
		const LeagueTier t = static_cast<LeagueTier>(i + 1);
		const int cx = ix + step * i + step / 2;
		const bool lit = mine != LeagueTier::None && static_cast<int>(t) <= static_cast<int>(mine);
		DrawLeagueEmblem(p, cx - emblem / 2, y + 74 + (36 - emblem) / 2, emblem, t, lit ? 255 : 110);
		p.Fill(cx - 6, track - 5, 12, 12, t == mine ? pal::bg : lit ? Ink(t) : pal::hairline);
		if (t == mine) p.Frame(cx - 6, track - 5, 12, 12, Ink(t));
		Display(p, step >= 70 ? 16 : 14);
		if (!allNames && t != named) continue;
		const int nameW = allNames ? step : std::min(iw, 4 * step), nameX = std::max(ix, std::min(ix + iw - nameW, cx - nameW / 2));
		p.TextCaps(nameX, y + 130, nameW, 18, LeagueTierName(t), lit || t == mine ? Ink(t) : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
			0);
	}

	// What today's standing gives: from 10 games and 5 wins the vexillum, from 25 games and 12 wins the frame and the title too.
	const int boxY = y + 156, boxH = 122, slotW = (iw - 16) / 3;
	p.Fill(ix, boxY, iw, boxH, pal::raised);
	p.Frame(ix, boxY, iw, boxH, pal::hairline);
	LeagueFrame frame;
	frame.season = season;
	frame.tier = today.tier;
	frame.seasonName = v.seasonLoaded ? LeagueSeasonName(v.season, v.lang) : std::string();
	// The test season's frame and title keep the rank: only its vexillum is the Founder's.
	frame.founder = false;
	const int sy = boxY + 8;
	DrawLeagueBadge(p, ix + (slotW - 60) / 2, sy, 60, LeagueVexillumBadge(today.tier, today.founder, season));
	const bool vexillum = today.vexillum && !today.placing;
	PaintRewardSlot(p, ix, sy, slotW, today.founder ? Tr("The Founder's vexillum") : Tr("Vexillum"),
		vexillum ? Tr("next to your name") : TrFmt("from %s games and %s wins", {std::to_string(kLeagueVexillumGames), std::to_string(kLeagueVexillumWins)}), vexillum);
	const int fx = ix + slotW + 8;
	PaintFramedAvatar(p, fx + (slotW - 56) / 2, sy, 56, SteamOwnId(), st.player, frame);
	const bool framed = today.frame && !today.placing;
	PaintRewardSlot(p, fx, sy, slotW, Tr("Avatar frame"), framed ? Tr("around your avatar") : TrFmt("from %s games and %s wins", {std::to_string(kLeagueFrameGames), std::to_string(kLeagueFrameWins)}),
		framed);
	const int tx = fx + slotW + 8;
	// The name over its title; a title too long for one line takes the name's place, on two lines.
	p.Font(3);
	const std::string title = today.placing ? TrFmt("Title of Season %s", {LeagueRoman(season)}) : LeagueTitleLabel(frame);
	p.raw = true;
	const bool twoLines = p.TextWidth(title) > slotW - 12;
	p.raw = false;
	if (twoLines) RawText(p, tx + 6, sy + 12, slotW - 12, 40, title, LeagueFrameInk(frame), DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
	else {
		Body(p, 15);
		RawText(p, tx + 6, sy + 6, slotW - 12, 24, st.player, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		RawText(p, tx + 6, sy + 32, slotW - 12, 20, title, LeagueFrameInk(frame), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	PaintRewardSlot(p, tx, sy, slotW, Tr("Season title"), framed ? Tr("under your name") : TrFmt("from %s games and %s wins", {std::to_string(kLeagueFrameGames), std::to_string(kLeagueFrameWins)}), framed);
	p.Fill(ix + slotW + 4, boxY + 12, 1, boxH - 24, pal::hairline);
	p.Fill(tx - 4, boxY + 12, 1, boxH - 24, pal::hairline);

	// How far the next rank is: the rewards go up with it.
	std::string next;
	COLORREF ink = pal::caption;
	if (today.placing) next = Tr("Your rewards follow your rank: finish your placement first.");
	else if (today.nextTier == LeagueTier::None) next = Tr("You are at the top: hold your place until the season closes.");
	else if (!LeagueTierHasDivisions(today.nextTier)) {
		next = Tr(today.nextTier == LeagueTier::Imperator ? "Imperator goes to the best 10 when the season closes."
														   : "Praetorian goes to the best 50 when the season closes.");
	}
	else {
		next = TrFmt("%s Glory to %s: your rewards go up with it.", {std::to_string(today.gloryToNext), Tr(LeagueTierName(today.nextTier))});
		ink = pal::textDim;
	}
	p.Font(3);
	RawText(p, ix, boxY + boxH + 6, iw, 22, next, ink);
}

const int kMiniBoardMinH = 330;

void PaintMiniBoard(Painter &p, const State &st, const HubLayout &h, int y, int cardH)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const int x = h.rightX, w = h.rightW, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, cardH);
	Caps(p, ix, y + 16, iw - 160, 22, LeagueQueueIsTeam(v.queue) ? "Leaderboard  \xC2\xB7  2 vs 2" : "Leaderboard  \xC2\xB7  1 vs 1", pal::redText);
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
		PaintName(p, ix + 40, ry, nameW, rowH, r.name.empty() ? Tr("Player") : r.name, id, RankBadge(r.tier, r.division), you, mx, my, r.masteryCiv, r.mastery);
		p.Font(1);
		RawText(p, ix + 40 + nameW + 16, ry, glW, rowH, std::to_string(r.glory), pal::text);
		p.Font(0);
		RawText(p, ix + iw - wlW, ry, wlW, rowH, std::to_string(r.wins) + "\xE2\x80\x93" + std::to_string(r.losses), pal::caption);
	};
	if (!v.boardLoaded && v.boardFailed) {
		p.Font(0);
		p.TextWrapped(ix, y + 84, iw, 60, "Couldn't reach the league server. Check your connection and try again.", pal::caption);
	}
	else if (!v.boardLoaded) {
		PaintSkeletonRows(p, ix, y + 80, iw, rowH, std::max(1, std::min(10, (cardH - 82 - 64) / rowH)),
			{{ix, 40}, {ix + 40, nameW}, {ix + 40 + nameW + 16, glW}, {ix + iw - wlW, wlW}});
	}
	else if (v.top.rows.empty()) {
		p.Font(0);
		p.TextWrapped(ix, y + 96, iw, 60, "Nobody has finished their placement games yet. Be the first on the leaderboard.", pal::caption);
	}
	bool listed = false;
	// The first ten, fewer on a short screen: the player's own row and the button keep their room under them.
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	const int room = cardH - 82 - 64;
	int shown = std::min(10, room / rowH);
	if (mine && mine->position > shown) shown = std::min(10, (room - 78) / rowH);
	// Under the duo's panel of a short screen the card is short: fewer rows before the player's own.
	shown = std::max(mine && mine->position > 3 ? 1 : 3, shown);
	for (size_t i = 0; i < v.top.rows.size() && i < static_cast<size_t>(shown); i++) {
		const LeagueBoardRow &r = v.top.rows[i];
		const bool you = r.steamId == own;
		listed = listed || you;
		const int ry = y + 82 + static_cast<int>(i) * rowH;
		if (you) {
			p.Fill(x + 1, ry, w - 2, rowH, pal::selected);
			p.Fill(x + 1, ry, 3, rowH, pal::red);
		}
		else if (p.Hover(x + 1, ry, w - 2, rowH, mx, my)) p.Fill(x + 1, ry, w - 2, rowH, pal::hairline);
		// A row opens the player's profile; the name's laurel and badge keep their own clicks, painted over it.
		p.AddHit(x + 1, ry, w - 2, rowH, kRankedAct, kRkProfileTop + static_cast<int>(i));
		row(r, ry, you);
	}
	// The player's own row stays pinned under the first ten.
	const int pinY = y + 82 + shown * rowH + 28;
	if (mine && !listed) {
		p.Font(0);
		p.Text(ix, pinY - 28, iw, 26, "\xC2\xB7 \xC2\xB7 \xC2\xB7", pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Fill(x + 1, pinY, w - 2, 50, pal::selected);
		p.Fill(x + 1, pinY, 3, 50, pal::red);
		p.AddHit(x + 1, pinY, w - 2, 50, kRankedAct, kRkProfileMe);
		row(*mine, pinY + 3, true);
	}
	Ghost(p, ix, y + cardH - 52, iw, 36, "Full leaderboard", kRkBoard, true, mx, my);
}

void PaintHub(Painter &p, const State &st)
{
	const HubLayout h = HubFor(st);
	// The player's standing and one copy of the duo for the whole frame.
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	const bool team = LeagueQueueIsTeam(st.ranked.view.queue);
	const LeagueDuoView duo = team ? LeagueDuoState() : LeagueDuoView();
	PaintSeasonBand(p, st, h);
	const bool duoInCard = PaintRankCard(p, st, h, me, duo);
	// The rewards keep their room; the recent games take the rest, five at most.
	const int recentMax = kRecentTop + kLeagueRecentGames * (kRecentRowH + 1) + 8;
	const int rewardsH = std::min(h.cardH - kGap - 120, std::max(h.cardH - kGap - recentMax, kRewardsMinH));
	const int recentH = h.cardH - kGap - rewardsH;
	PaintRecent(p, st, h, me, h.cardY, recentH);
	PaintSeasonRewards(p, st, h, me, h.cardY + recentH + kGap, rewardsH);
	if (team) LeagueDuoWatch();
	if (!team || duoInCard) {
		PaintMiniBoard(p, st, h, h.cardY, h.cardH);
		return;
	}
	// While searching, or on a screen too short for it in the card, the duo goes over the leaderboard, which keeps room for a few rows
	// and the player's own.
	const int duoH = RankedDuoHeight(duo, h.cardH - kGap - kMiniBoardMinH);
	PaintRankedDuo(p, st, duo, h.rightX, h.cardY, h.rightW, duoH);
	PaintMiniBoard(p, st, h, h.cardY + duoH + kGap, h.cardH - duoH - kGap);
}

// Everyone of the season being played, with no name looked for: the view the player's own row is pinned to.
bool DefaultBoard(const State &st)
{
	return st.ranked.boardSeason < 0 && !st.ranked.boardFriends && st.ranked.boardAsked.empty();
}

// "Season I · in progress", as the season's menu shows it, short.
std::string BoardSeasonText(const State &st)
{
	const auto seasonsView = LeagueSeasonsShared();
	const LeagueSeasonsView &sv = *seasonsView;
	const LeagueRankedView &v = st.ranked.view;
	const int season = st.ranked.boardSeason >= 0 ? st.ranked.boardSeason : v.seasonLoaded ? v.season.number : -1;
	if (season < 0) return Tr("Season being played");
	const LeagueSeasonInfo *s = LeagueSeasonById(sv.seasons, season);
	const bool closed = s ? s->closed : st.ranked.boardSeason >= 0;
	return TrFmt("Season %s", {LeagueRoman(season)}) + "  \xC2\xB7  " + Tr(closed ? "closed" : "in progress");
}

// The card of the player picked in the table: their frame, title and rank, their figures, the civilizations they play most and their last
// ten games, from their card on the league; their row says what it can while the card is read.
void PaintPlayerCard(Painter &p, const State &st, const std::string &seasonText, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const std::string own = std::to_string(SteamOwnId());
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + 24, iw = w - 48;
	std::string pick = st.ranked.boardPick;
	if (pick.empty() && DefaultBoard(st) && LeagueBoardFind(v.around, own)) pick = own;
	if (pick.empty()) {
		p.Font(0);
		p.TextWrapped(ix, y + 24, iw, 80, "Pick a player of the table to see their card.", pal::caption);
		return;
	}
	const int season = st.ranked.boardSeason;
	LeaguePlayerRequest(pick, season, false, v.queue);
	const LeaguePlayerView pv = LeaguePlayerState(pick, season, v.queue);
	const bool ready = pv.state == LeagueCardState::Ready;
	const LeaguePlayerProfile &c = pv.card;
	const LeagueBoardRow *row = LeagueBoardFind(v.page, pick);
	if (!row) row = LeagueBoardFind(v.around, pick);
	const uint64_t id = _strtoui64(pick.c_str(), nullptr, 10);
	const std::string name = ready && !c.name.empty() ? c.name : row && !row->name.empty() ? row->name : Tr("Player");
	const LeagueTier tier = ready ? c.tier : row ? row->tier : LeagueTier::None;
	const int division = ready ? c.division : row ? row->division : 0;
	const int glory = ready ? c.glory : row ? row->glory : 0;
	const int place = ready && c.place > 0 ? c.place : row ? row->position : 0;

	// The card's parts at the mockup's places on a 1080 screen, closer together on a shorter one.
	const bool tight = h < 700;
	const int av = tight ? 72 : 88, emblem = tight ? 88 : 120, rule = tight ? 104 : 128, tileH = tight ? 62 : 74;
	PaintFramedAvatar(p, ix, y + (tight ? 18 : 24), av, id, name, ready ? c.frame : LeagueFrame());
	const int tx = ix + av + 20, tw = iw - av - 20;
	const LeagueBadge badge = PlayerBadgeOf(id);
	// A patron's laurel before the name, which takes the colour of the honour roll.
	const PatronInfo patron = id ? PatronOf(id) : PatronInfo();
	const int laurel = patron.tier != PatronTier::None ? (tight ? 26 : 30) : 0;
	const int nameX = tx + (laurel ? laurel + 6 : 0), nameY = y + (tight ? 14 : 20);
	Body(p, tight ? 22 : 24);
	const int nameW = std::max(20, tw - (nameX - tx) - LeagueBadgeRoom(badge));
	p.raw = true;
	if (laurel) DrawPatron(p, tx, nameY + (34 - laurel) / 2, laurel, nameX, nameY, nameW, 34, name, patron.tier, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	else p.Text(nameX, nameY, nameW, 34, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	DrawBadgeAfterName(p, nameX, nameY, nameW, 34, name, badge, mx, my);
	p.raw = false;
	if (laurel) PatronHotspot(p, tx, nameY + (34 - laurel) / 2, laurel, patron, mx, my, kPatronLink);
	const std::string title = ready ? LeagueTitleLabel(c.title) : std::string();
	// The title of a calendar cup held now (the Crown of the month, Champion of a season), in gold after the season's title.
	const std::string cupTitle = ready && !c.cupTitles.empty() ? c.cupTitles.front().name : std::string();
	if (!title.empty() || !cupTitle.empty()) {
		p.Font(4);
		p.raw = true;
		int cx = tx;
		if (!title.empty()) {
			const int tw1 = cupTitle.empty() ? tw : std::min(tw / 2, p.CapsWidth(title, 2) + 4);
			p.TextCaps(cx, y + (tight ? 46 : 56), tw1, 20, title, LeagueFrameInk(c.title), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
			cx += tw1 + 12;
		}
		if (!cupTitle.empty()) {
			p.TextCaps(cx, y + (tight ? 46 : 56), tx + tw - cx, 20, cupTitle, RGB(0xd8, 0xae, 0x46), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		}
		p.raw = false;
	}
	p.Font(0);
	const bool you = pick == own;
	std::string facts = place > 0 ? TrFmt("Place %s", {std::to_string(place)}) + "  \xC2\xB7  " + RankedQueueLabel(v.queue) : RankedQueueLabel(v.queue);
	p.raw = true;
	if (p.TextWidth(facts + "  \xC2\xB7  " + seasonText) <= tw - (you ? 52 : 0)) facts += "  \xC2\xB7  " + seasonText;
	p.raw = false;
	const int factsY = y + (tight ? 66 : 80);
	RawText(p, tx, factsY, tw - (you ? 52 : 0), 24, facts, pal::caption);
	if (you) Chip(p, tx + tw - 44, factsY + 2, 44, 20, "You", pal::textDim, pal::hairline);
	p.Fill(ix, y + rule, iw, 1, pal::hairline);

	const int ey = y + rule + 12, rx = ix + emblem + 12, rw = iw - emblem - 12;
	DrawLeagueEmblem(p, ix - 4, ey, emblem, tier);
	Display(p, tight ? 42 : 50);
	p.raw = true;
	p.TextCaps(rx, ey + (tight ? 2 : 10), rw, tight ? 46 : 54, RankName(tier, division), Ink(tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	const int gloryY = ey + (tight ? 50 : 66);
	RawText(p, rx, gloryY, rw, 24, (place > 0 ? "#" + std::to_string(place) + "  \xC2\xB7  " : std::string()) + TrFmt("%s Glory", {std::to_string(glory)}), pal::body);
	// An elite player's peak by Glory sits below the rank the place gives them now: then the line says nothing new.
	const bool peakBelow = c.peak.tier != LeagueTier::None && static_cast<int>(c.peak.tier) < static_cast<int>(tier) && !LeagueTierHasDivisions(tier);
	if (ready && !peakBelow && c.peak.present && (c.peak.tier != LeagueTier::None || c.peak.place > 0)) {
		const bool byPlace = c.peak.place > 0 && (c.peak.tier == LeagueTier::None || !LeagueTierHasDivisions(c.peak.tier));
		const std::string peak = byPlace ? TrFmt("Season peak: #%s", {std::to_string(c.peak.place)})
										 : Tr("Season peak:") + " " + RankName(c.peak.tier, c.peak.division) + "  \xC2\xB7  " + TrFmt("%s Glory", {std::to_string(c.peak.glory)});
		p.Font(3);
		RawText(p, rx, gloryY + 26, rw, 22, peak, pal::caption);
	}
	const int bottom = y + h - (tight ? 76 : 92), buttonH = tight ? 48 : 52;
	p.Fill(ix, bottom, iw, 1, pal::hairline);
	// The player's profile on the league beside their Steam profile.
	const int halfW = (iw - 12) / 2, buttonY = bottom + (tight ? 12 : 16);
	p.Button(ix, buttonY, halfW, buttonH, "View profile", kRankedAct, kRkProfile, mx, my, id != 0, true);
	PaintSteamButton(p, ix + halfW + 12, buttonY, iw - halfW - 12, buttonH, "Steam profile", kRankedAct, kRkSteam, mx, my, id != 0, false);
	const int ty = ey + emblem + (tight ? 18 : 16);
	if (!ready) {
		p.Font(0);
		if (pv.state == LeagueCardState::Loading || pv.state == LeagueCardState::None) {
			// The four tiles the card will have, and the lines of its last games under them.
			const int tileGap = tight ? 8 : 14, tileW = (iw - 3 * tileGap) / 4;
			for (int k = 0; k < 4; k++) PaintSkeleton(p, ix + k * (tileW + tileGap), ty, tileW, tileH);
			const int linesY = ty + tileH + 24;
			for (int k = 0; k < 3 && linesY + k * 30 + 12 < bottom - 12; k++) PaintSkeleton(p, ix, linesY + k * 30, iw * (80 - k * 15) / 100, 12);
		}
		else {
			p.raw = true;
			p.TextWrapped(ix, ty + 12, iw, 60,
				pv.state == LeagueCardState::Missing ? TrFmt("%s has no ranked games in this season.", {name})
													 : Tr("Couldn't reach the league server. Check your connection and try again."),
				pal::caption);
			p.raw = false;
		}
		return;
	}

	// Games, win rate, streak and how long a game lasts.
	const int tileGap = tight ? 8 : 14, tileW = (iw - 3 * tileGap) / 4;
	Tile(p, ix, ty, tileW, tileH, std::to_string(c.games), "Games");
	Tile(p, ix + tileW + tileGap, ty, tileW, tileH, std::to_string(LeagueWinRate(c.wins, c.losses)) + " %", "Win rate");
	Tile(p, ix + 2 * (tileW + tileGap), ty, tileW, tileH, LeagueStreakText(c.streak), "Streak", c.streak > 0 ? pal::ok : c.streak < 0 ? pal::bad : pal::text);
	Tile(p, ix + 3 * (tileW + tileGap), ty, tileW, tileH, c.avgDurationMs >= 60000 ? TrFmt("%s min", {std::to_string(c.avgDurationMs / 60000)}) : std::string("\xE2\x80\x94"),
		"Average");

	// The three civilizations played most: the badge of each one's mastery (of every season and queue), its level over the bar to the
	// next one, and the games and win rate of this season with it. The season being played puts its wins in the lighter part of the bar.
	// From a server before the mastery, the share of the games whose civilization is known (not of all of them: the reports of some
	// games may name none).
	const int civY = ty + tileH + (tight ? 12 : 18), civStep = tight ? 28 : 32;
	Caps(p, ix, civY, iw, 20, "Civilizations", pal::caption);
	if (c.civs.empty()) {
		p.Font(3);
		p.Text(ix, civY + 26, iw, 24, "No games in this season yet.", pal::caption);
	}
	const bool liveSeason = st.ranked.boardSeason < 0 || (v.seasonLoaded && v.season.active && c.season == v.season.number);
	for (size_t i = 0; i < c.civs.size(); i++) {
		const LeagueCivShare &civ = c.civs[i];
		const LeagueMastery &m = civ.mastery;
		const int ry = civY + 24 + static_cast<int>(i) * civStep, rowH = civStep - 4, badge = tight ? 18 : 20, by = ry + (rowH - badge) / 2;
		const int nameX = ix + badge + 8, nameW = tight ? 100 : 116;
		if (m.present) {
			DrawMasteryBadge(p, ix, by, badge, m.level);
			HoverMastery(p, ix, by, badge, civ.civ, m, mx, my);
		}
		p.Font(0);
		RawText(p, m.present ? nameX : ix, ry, m.present ? nameW : nameW + badge + 8, rowH, CivText(civ.civ), pal::body);
		const std::string figures = (civ.games == 1 ? Tr("1 game") : TrFmt("%s games", {std::to_string(civ.games)})) + "  \xC2\xB7  " +
			std::to_string(LeagueWinRate(civ.wins, civ.games - civ.wins)) + " %";
		p.Font(3);
		p.raw = true;
		const int figW = std::min(iw / 3, p.TextWidth(figures) + 4);
		p.raw = false;
		RawText(p, ix + iw - figW, ry, figW, rowH, figures, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int barX = nameX + nameW + 8, barW = std::max(24, ix + iw - figW - 12 - barX);
		if (m.present) {
			const COLORREF ink = LeagueMasteryInk(m.level);
			// Towards the next level, its wins; at Maestro, all the wins, which keep counting.
			const std::string level = LeagueMasteryLabel(m.level),
							  count = m.Top() ? TrFmt("%s wins", {std::to_string(m.wins)}) : std::to_string(m.wins) + " / " + std::to_string(m.nextWins);
			p.Font(4);
			p.raw = true;
			const int levelW = p.CapsWidth(level, 1);
			p.Font(3);
			const int countW = count.empty() ? 0 : p.TextWidth(count);
			// On a narrow card the wins give way to the level.
			const bool counted = countW && levelW + 8 + countW <= barW;
			p.Font(4);
			p.TextCaps(barX, ry - 1, counted ? barW - countW - 8 : barW, rowH - 9, level, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
			p.raw = false;
			if (counted) {
				p.Font(3);
				RawText(p, barX + barW - countW, ry - 1, countW, rowH - 9, count, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			}
			PaintMasteryBar(p, barX, ry + rowH - 7, barW, 5, m, liveSeason ? civ.wins : 0);
		}
		else {
			const int share = c.civGames > 0 ? civ.games * 100 / c.civGames : 0;
			p.Fill(barX, ry + rowH / 2 - 4, barW, 8, pal::bg);
			p.Fill(barX, ry + rowH / 2 - 4, barW * std::min(100, share) / 100, 8, pal::textDim);
		}
	}

	// The last ten games, the newest first: a win, a loss, or a dash for one that did not count.
	const int lastY = civY + 24 + 3 * civStep + (tight ? 6 : 10), chipH = tight ? 26 : 28;
	Caps(p, ix, lastY, iw, 20, "Last 10 games", pal::caption);
	const int chipGap = 6, chipW = (iw - 9 * chipGap) / 10;
	for (int i = 0; i < 10; i++) {
		const int cx = ix + i * (chipW + chipGap), cy = lastY + 26;
		if (i >= static_cast<int>(c.recent.size())) {
			p.Frame(cx, cy, chipW, chipH, pal::hairline);
			continue;
		}
		const LeagueGameResult r = c.recent[i].result;
		const bool won = r == LeagueGameResult::Win, lost = r == LeagueGameResult::Loss;
		p.Fill(cx, cy, chipW, chipH, won ? kWinBack : lost ? kLossBack : pal::hairline);
		p.Font(4);
		if (won || lost) p.TextCaps(cx, cy, chipW, chipH, won ? "W" : "L", won ? pal::ok : pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		else RawText(p, cx, cy, chipW, chipH, "\xE2\x80\x93", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	if (c.recent.empty()) {
		p.Font(3);
		p.Text(ix, lastY + 26 + chipH + 6, iw, 22, "No games in this season yet.", pal::caption);
	}

	// Under the games, where the card has room: the record against the player looking at it, and the civilizations this player beats
	// most and least (from 3 games each).
	int ly = lastY + 26 + chipH + (c.recent.empty() ? 32 : 12);
	if (!you && own != "0") {
		LeagueHeadToHeadRequest(pick, own);
		const LeagueHeadToHeadView hv = LeagueHeadToHeadState();
		if (hv.loaded && hv.a == pick && hv.b == own && hv.h2h.games > 0 && ly + 22 <= bottom - 8) {
			p.Font(3);
			RawText(p, ix, ly, iw, 22, TrFmt("Against you: %s wins, %s losses", {std::to_string(hv.h2h.wins), std::to_string(hv.h2h.losses)}), pal::body);
			ly += 24;
		}
	}
	LeagueCivShare best, worst;
	if (LeagueVsCivExtremes(c, best, worst) && ly + 22 <= bottom - 8) {
		auto record = [](const LeagueCivShare &s) { return std::to_string(s.wins) + "\xE2\x80\x93" + std::to_string(s.games - s.wins); };
		std::string line = TrFmt("Best against %s (%s)", {CivText(best.civ), record(best)});
		if (worst.civ != best.civ) line += "  \xC2\xB7  " + TrFmt("worst against %s (%s)", {CivText(worst.civ), record(worst)});
		p.Font(3);
		RawText(p, ix, ly, iw, 22, line, pal::caption);
	}
}

// The full leaderboard: the season, everyone or the Steam friends and a name to look for; a page of the table, the player's row pinned
// under it, and the card of the player picked.
void PaintBoard(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH, false);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m;
	const RankedBoardFrame frame = RankedBoardFrameFor(st.viewW, st.viewH);
	const int detailW = frame.detailW, tableW = frame.tableW;
	const LeagueBoard &b = v.page;
	const int rows = RankedBoardRowsFor(st.viewW, st.viewH);
	const int pages = std::max(1, (b.total + rows - 1) / rows), page = b.offset / rows + 1;
	const bool defaultView = DefaultBoard(st);

	// The tool bar: back to the summary, the queue, the season, everyone or the friends, the own place and the pages. When the table is
	// narrow, "Summary" keeps only its arrow and the season's menu narrows; if that is not enough, everyone or the friends, the own
	// place and the pages go to a second row, so the pages can always be turned.
	const int queueW = QueueSegmentsWidth(p, st);
	const int segW = std::max(ChipWidth(p, "All", 96), ChipWidth(p, "Friends", 96));
	const int meW = ChipWidth(p, "My place", 130), pagerW = 2 * 48 + 8;
	const bool twoRows = frame.secondRow > 0;
	int summaryW = 150, seasonW = std::max(220, std::min(300, tableW * 22 / 100));
	const auto need = [&] { return summaryW + 16 + queueW + 16 + seasonW + (twoRows ? 0 : 16 + 2 * segW + 16 + meW + 12 + pagerW); };
	if (need() > tableW) summaryW = 56;
	if (need() > tableW) seasonW = std::max(170, seasonW - (need() - tableW));
	if (twoRows) seasonW = std::max(seasonW, std::min(300, tableW - (summaryW + 16 + queueW + 16)));
	SummaryButton(p, m, top, summaryW, mx, my);
	int tx = m + summaryW + 16;
	QueueSegments(p, st, tx + queueW, top, 44);
	tx += queueW + 16;
	const std::string seasonText = BoardSeasonText(st);
	UseSharedFonts(p);
	p.raw = true;
	Combo(p, st, tx, top, seasonW, 44, seasonText, kDropRankedSeason, 0, true);
	p.raw = false;
	UseScreenFonts(p);
	tx += seasonW + 16;
	const int barY = twoRows ? top + frame.secondRow : top;
	if (twoRows) tx = m;
	Segment(p, tx, barY, segW, 44, "All", !st.ranked.boardFriends, kRkBoardAll, mx, my);
	Segment(p, tx + segW - 1, barY, segW, 44, "Friends", st.ranked.boardFriends, kRkBoardFriends, mx, my);
	tx += 2 * segW + 16;
	const int right = m + tableW;
	if (tx + meW + 12 + pagerW <= right) {
		Ghost(p, tx, barY, meW, 44, "My place", kRkBoardMe, v.around.me > 0, mx, my);
		tx += meW + 12;
	}
	// The pages go last and never leave: at worst they take the own place's room.
	tx = std::min(tx, right - pagerW);
	Ghost(p, tx, barY, 48, 44, "\xE2\x80\xB9", kRkBoardPrev, v.pageLoaded && b.offset > 0, mx, my);
	Ghost(p, tx + 56, barY, 48, 44, "\xE2\x80\xBA", kRkBoardNext, v.pageLoaded && page < pages, mx, my);
	tx += pagerW + 12;
	if (v.pageLoaded && right - tx >= 90) {
		p.Font(3);
		RawText(p, tx, barY, right - tx, 44, TrFmt("Page %s of %s", {std::to_string(page), std::to_string(pages)}), pal::caption);
	}
	// The name to look for, over the player's card.
	const int dx = m + tableW + 32;
	{
		const bool focused = st.focus == kRankedField && !st.ranked.disputeMatch;
		UseSharedFonts(p);
		PaintField(p, st, dx, top, detailW, 44, st.ranked.boardSearch, Tr("Search a player by name..."), focused, kFieldRanked);
		UseScreenFonts(p);
		if (!st.ranked.boardSearch.empty()) {
			const int cx = dx + detailW - 36;
			const bool hot = p.Hover(cx, top, 36, 44, mx, my);
			p.Line(cx + 12, top + 16, cx + 24, top + 28, hot ? pal::text : pal::textDim, 2);
			p.Line(cx + 12, top + 28, cx + 24, top + 16, hot ? pal::text : pal::textDim, 2);
			p.AddHit(cx, top, 36, 44, kRankedAct, kRkBoardClear);
		}
	}

	const int y0 = top + 72 + frame.secondRow, h = l.h - m - y0;
	PaintGlassCard(p, m, y0, tableW, h);
	// #, the Glory of the last 7 days, the player, the rank, the Glory, the games, the record, the share won and the streak; on a narrow
	// table the games and the share give their room to the name first.
	struct Col {
		const char *label;
		int x = 0, w = 0;
		bool shown = true;
	};
	Col cols[] = {{"#", 0, 48}, {"\xC2\xB1" "7 days", 0, 72}, {"Player", 0, 0}, {"Rank", 0, 196}, {"Glory", 0, 84}, {"Games", 0, 92}, {"W\xE2\x80\x93L", 0, 96},
		{"%", 0, 60}, {"Streak", 0, 76}};
	constexpr int kCols = sizeof(cols) / sizeof(cols[0]), kName = 2, kGames = 5, kRate = 7, colGap = 12;
	auto layout = [&]() {
		int fixed = 40;
		for (int i = 0; i < kCols; i++) {
			if (i != kName && cols[i].shown) fixed += cols[i].w + colGap;
		}
		return tableW - fixed;
	};
	if (layout() < 240) cols[kGames].shown = false;
	if (layout() < 240) cols[kRate].shown = false;
	// Still narrow: the rank's name gives up some width (it ends in an ellipsis), then the week's Glory goes, before the name is cut short.
	constexpr int kRank = 3, kWeek = 1, kNameMin = 140;
	if (layout() < kNameMin) cols[kRank].w = std::max(130, cols[kRank].w - (kNameMin - layout()));
	if (layout() < kNameMin) cols[kWeek].shown = false;
	cols[kName].w = std::max(80, layout());
	int cx = 20;
	for (Col &c : cols) {
		if (!c.shown) continue;
		c.x = cx;
		cx += c.w + colGap;
	}
	for (int i = 0; i < kCols; i++) {
		if (cols[i].shown) Caps(p, m + cols[i].x, y0, cols[i].w, 44, cols[i].label, i == 4 ? pal::redText : pal::caption);
	}
	p.Fill(m + 1, y0 + 43, tableW - 2, 1, pal::lineStrong);
	const std::string own = std::to_string(SteamOwnId());
	const int rowH = 46;
	auto paintRow = [&](const LeagueBoardRow &r, int ry, int rh, bool you, bool selected, int index) {
		if (selected || you) p.Fill(m + 1, ry, tableW - 2, rh, selected ? pal::raised : pal::selected);
		if (you) p.Fill(m + 1, ry, 3, rh, pal::red);
		const bool hot = index >= 0 && p.Hover(m, ry, tableW, rh, mx, my);
		if (hot && !selected && !you) p.Fill(m + 1, ry, tableW - 2, rh, pal::raised);
		if (index >= 0) p.AddHit(m, ry, tableW, rh, kRankedAct, kRkRow + index);
		p.Font(r.position <= 10 || you ? 1 : 0);
		RawText(p, m + cols[0].x, ry, cols[0].w, rh, std::to_string(r.position), r.position <= 10 || you ? pal::text : pal::caption);
		p.Font(4);
		if (!cols[1].shown) {}
		else if (r.hasDelta7) {
			RawText(p, m + cols[1].x, ry, cols[1].w, rh, SignedText(r.delta7), r.delta7 > 0 ? pal::ok : r.delta7 < 0 ? pal::bad : pal::caption);
		}
		else RawText(p, m + cols[1].x, ry, cols[1].w, rh, "\xE2\x80\x93", pal::disabled);
		const uint64_t id = _strtoui64(r.steamId.c_str(), nullptr, 10);
		const int av = 32;
		if (!PaintAvatar(p, m + cols[kName].x, ry + (rh - av) / 2, av, id)) p.Fill(m + cols[kName].x, ry + (rh - av) / 2, av, av, pal::hairline);
		PaintName(p, m + cols[kName].x + av + 12, ry, cols[kName].w - av - 12, rh, r.name.empty() ? Tr("Player") : r.name, id, LeagueBadge(), you, mx, my,
			r.masteryCiv, r.mastery);
		DrawLeagueBadge(p, m + cols[3].x, ry + (rh - kLeagueBadgeSize) / 2, kLeagueBadgeSize, RankBadge(r.tier, r.division));
		Display(p, 22);
		p.raw = true;
		p.TextCaps(m + cols[3].x + 28, ry, cols[3].w - 28, rh, RankName(r.tier, r.division), Ink(r.tier), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		p.Font(1);
		RawText(p, m + cols[4].x, ry, cols[4].w, rh, std::to_string(r.glory), pal::text);
		p.Font(0);
		if (cols[kGames].shown) RawText(p, m + cols[kGames].x, ry, cols[kGames].w, rh, std::to_string(r.wins + r.losses), pal::body);
		RawText(p, m + cols[6].x, ry, cols[6].w, rh, std::to_string(r.wins) + "\xE2\x80\x93" + std::to_string(r.losses), pal::body);
		if (cols[kRate].shown) RawText(p, m + cols[kRate].x, ry, cols[kRate].w, rh, std::to_string(LeagueWinRate(r.wins, r.losses)) + " %", pal::body);
		p.Font(4);
		RawText(p, m + cols[8].x, ry, cols[8].w, rh, LeagueStreakText(r.streak), r.streak > 0 ? pal::ok : r.streak < 0 ? pal::bad : pal::caption);
	};
	std::vector<uint64_t> ids;
	for (const LeagueBoardRow &r : b.rows) ids.push_back(_strtoui64(r.steamId.c_str(), nullptr, 10));
	if (!ids.empty()) PatronsWatch(ids);
	int ry = y0 + 44;
	bool listed = false;
	if (!v.pageLoaded && v.pageError.empty()) {
		std::vector<std::pair<int, int>> bars;
		for (int i = 0; i < kCols; i++) {
			if (cols[i].shown) bars.push_back({m + cols[i].x, cols[i].w});
		}
		PaintSkeletonRows(p, m + 1, ry, tableW - 2, rowH, std::max(1, std::min(rows, (y0 + h - ry) / rowH)), bars);
	}
	else if (!v.pageLoaded) {
		p.Font(0);
		p.Text(m + 24, ry + 18, tableW - 48, 28, v.pageError, pal::bad);
	}
	else if (b.rows.empty()) {
		const LeagueBoardQuery &q = v.pageQuery;
		const std::string text = !q.name.empty() ? TrFmt("Nobody called \"%s\" is on this leaderboard.", {q.name})
			: q.friends								 ? Tr("None of your Steam friends is ranked in this season yet.")
			: q.season >= 0							 ? Tr("Nobody was ranked in this season.")
													 : Tr("Nobody has finished their placement games yet. Be the first on the leaderboard.");
		p.Font(0);
		p.raw = true;
		p.TextWrapped(m + 24, ry + 18, tableW - 48, 60, text, pal::caption);
		p.raw = false;
	}
	if (v.pageLoading && v.pageLoaded) p.Spinner(m + tableW - 24, y0 + 22, 7, 3, GetTickCount());
	for (size_t i = 0; i < b.rows.size() && i < static_cast<size_t>(rows); i++) {
		const LeagueBoardRow &r = b.rows[i];
		const bool you = r.steamId == own;
		listed = listed || you;
		paintRow(r, ry, rowH, you, !st.ranked.boardPick.empty() && st.ranked.boardPick == r.steamId, static_cast<int>(i));
		ry += rowH;
		// The line under the last Imperator, before the first one who is not, with the Glory it takes: what the best ones play for.
		if (LeagueImperatorLineAfter(r, i + 1 < b.rows.size() ? &b.rows[i + 1] : nullptr)) {
			p.Fill(m + 1, ry, tableW - 2, 28, RGB(0x26, 0x12, 0x13));
			p.Fill(m + 24, ry + 14, 120, 1, RGB(0x5a, 0x2a, 0x2c));
			p.Fill(m + tableW - 144, ry + 14, 120, 1, RGB(0x5a, 0x2a, 0x2c));
			p.raw = true;
			Caps(p, m + 150, ry, tableW - 300, 28, TrFmt("Imperator line  \xC2\xB7  %s Glory  \xC2\xB7  the best ten", {std::to_string(r.glory)}), pal::redText,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
			ry += 28;
		}
	}
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	if (mine && !listed && v.pageLoaded && defaultView) {
		const int py = y0 + h - 52;
		p.Fill(m + 1, py - 2, tableW - 2, 2, pal::lineStrong);
		paintRow(*mine, py, 51, true, st.ranked.boardPick == own, -1);
		p.AddHit(m, py, tableW, 51, kRankedAct, kRkBoardMe);
	}
	PaintPlayerCard(p, st, seasonText, dx, y0, detailW, h);
}

// The player's ranked games, ten a page, newest first: what each gave, against whom, where and with which civilizations, and
// "Report a problem" for a day after each.
// The page of one game: what the chart can show, by kRkDetailMetric's argument.
struct DetailMetric {
	StatMetric metric;
	const char *label;
};
const DetailMetric kDetailMetrics[] = {
	{kStatArmyStrength, "Army"}, {kStatScore, "Score"}, {kStatUnitsAlive, "Units"}, {kStatKilled, "Killed"}, {kStatGoldTotal, "Gold"},
};
// One colour a player, in the order the page lists them.
const COLORREF kDetailLines[] = {RGB(86, 156, 214), RGB(214, 92, 80), RGB(110, 190, 110), RGB(222, 180, 70), RGB(170, 120, 210), RGB(90, 200, 200),
	RGB(230, 140, 60), RGB(200, 200, 200)};

LeagueGameResult DetailResult(const std::string &r)
{
	if (r == "win") return LeagueGameResult::Win;
	if (r == "loss") return LeagueGameResult::Loss;
	if (r == "void") return LeagueGameResult::Void;
	return LeagueGameResult::Pending;
}

std::string DetailCheckText(const std::string &check, COLORREF &ink)
{
	ink = pal::caption;
	if (check == "verified") {
		ink = pal::ok;
		return Tr("Both players' games gave the same numbers");
	}
	if (check == "mismatch") {
		ink = pal::warn;
		return Tr("The two players' games gave different numbers: they may not be exact");
	}
	if (check == "single") return Tr("Only one player's game sent numbers");
	return Tr("No numbers were sent for this game");
}

void PaintMatchDetail(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH, false);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m;
	Ghost(p, m, top, 190, 44, "Back to list", kRkDetailClose, true, mx, my);
	const bool ready = v.detailLoaded && v.detailId == st.ranked.detailMatch;
	const LeagueMatchDetail &d = v.detail;
	if (ready) {
		std::string head = d.mapName.empty() ? std::string("\xE2\x80\x94") : LeagueMapText(d.mapName);
		if (!d.mapSize.empty()) head += "  \xC2\xB7  " + LeagueMapText(d.mapSize);
		if (d.durationMs) head += "  \xC2\xB7  " + TrFmt("%s min", {std::to_string(d.durationMs / 60000)});
		if (d.playedAt) head += "  \xC2\xB7  " + DateText(LocalTime(d.playedAt), true);
		p.Font(0);
		RawText(p, m + 190 + 24, top, inner - 214, 44, head, pal::text);
	}
	const int y0 = top + 72, h = l.h - m - y0;
	PaintGlassCard(p, m, y0, inner, h);
	if (!ready) {
		p.Font(0);
		if (!v.detailLoading && !v.detailError.empty()) p.Text(m + 24, y0 + 24, inner - 48, 28, v.detailError, pal::bad);
		else PaintSkeletonRows(p, m + 1, y0 + 24, inner - 2, 56, 4, {{m + 24, inner / 3}, {m + inner / 2, inner / 3}});
		return;
	}
	COLORREF checkInk = pal::caption;
	const std::string check = DetailCheckText(d.statsCheck, checkInk);
	p.Font(3);
	RawText(p, m + 24, y0 + 12, inner - 48, 28, check, checkInk);

	// The table: one row a player, with what the game counted by its end.
	struct Col {
		const char *label;
		int w;
		StatMetric metric;
	};
	const int nameW = std::max(220, inner / 4);
	const Col cols[] = {{"Score", 0, kStatScore}, {"Killed", 0, kStatKilled}, {"Lost", 0, kStatLost}, {"Units made", 0, kStatUnitsBuilt},
		{"Largest army", 0, kStatUnitsMax}, {"Gold", 0, kStatGoldTotal}};
	const int resultW = 190, n = static_cast<int>(sizeof(cols) / sizeof(cols[0]));
	const int colW = std::max(80, (inner - 48 - nameW - resultW - 16) / n);
	int ty = y0 + 48;
	Caps(p, m + 24, ty, nameW, 36, "Player", pal::caption);
	Caps(p, m + 24 + nameW, ty, resultW, 36, "Result", pal::caption);
	for (int i = 0; i < n; i++) Caps(p, m + 24 + nameW + resultW + i * colW, ty, colW - 8, 36, cols[i].label, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	ty += 36;
	p.Fill(m + 1, ty - 1, inner - 2, 1, pal::lineStrong);
	const int rowH = 52;
	std::vector<std::pair<const LeagueDetailStats *, COLORREF>> lines;
	for (size_t i = 0; i < d.players.size(); i++) {
		const LeagueDetailPlayer &pl = d.players[i];
		const COLORREF color = kDetailLines[i % (sizeof(kDetailLines) / sizeof(kDetailLines[0]))];
		const LeagueDetailStats *s = LeagueDetailStatsOf(d, pl);
		if (s) lines.push_back({s, color});
		p.Fill(m + 24, ty + rowH / 2 - 6, 12, 12, color);
		p.Font(0);
		RawText(p, m + 44, ty + 4, nameW - 28, 26, pl.name.empty() ? Tr("Player") : pl.name, pal::text);
		p.Font(3);
		RawText(p, m + 44, ty + 28, nameW - 28, 20, pl.civ.empty() ? std::string("\xE2\x80\x94") : CivText(pl.civ), pal::caption);
		const int chip = PaintResultChip(p, DetailResult(pl.result), m + 24 + nameW, ty, rowH, 90, 26);
		if (pl.hasDelta) {
			p.Font(0);
			RawText(p, m + 24 + nameW + chip + 10, ty, resultW - chip - 10, rowH, SignedText(pl.gloryDelta), pl.gloryDelta >= 0 ? pal::ok : pal::bad);
		}
		p.Font(0);
		for (int c = 0; c < n; c++) {
			const std::string value = s ? StatsFormatShort(s->points.back().values[cols[c].metric]) : std::string("\xE2\x80\x94");
			p.Text(m + 24 + nameW + resultW + c * colW, ty, colW - 8, rowH, value, pal::body, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		ty += rowH;
		p.Fill(m + 1, ty - 1, inner - 2, 1, pal::hairline);
	}
	if (lines.empty()) return;

	// The chart: the chosen number of every player through the game.
	const int metricIndex = std::max(0, std::min(st.ranked.detailMetric, static_cast<int>(sizeof(kDetailMetrics) / sizeof(kDetailMetrics[0])) - 1));
	const StatMetric metric = kDetailMetrics[metricIndex].metric;
	int cx = m + 24;
	const int chipsY = ty + 20;
	for (int i = 0; i < static_cast<int>(sizeof(kDetailMetrics) / sizeof(kDetailMetrics[0])); i++) {
		p.Font(3);
		const int w = p.TextWidth(Tr(kDetailMetrics[i].label)) + 32;
		const bool on = i == metricIndex;
		if (on) p.Fill(cx, chipsY, w, 34, pal::selected);
		else if (p.Hover(cx, chipsY, w, 34, mx, my)) p.Fill(cx, chipsY, w, 34, pal::raised);
		p.Text(cx, chipsY, w, 34, Tr(kDetailMetrics[i].label), on ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(cx, chipsY, w, 34, kRankedAct, kRkDetailMetric + i);
		cx += w + 8;
	}
	const int gx = m + 84, gy = chipsY + 56, gw = inner - 84 - 32, gh = y0 + h - 40 - gy;
	if (gh < 80) return;
	uint32_t lastMs = 1;
	int maximum = 1;
	for (const auto &line : lines) {
		for (const LeagueStatsPoint &pt : line.first->points) {
			lastMs = std::max(lastMs, pt.ms);
			maximum = std::max(maximum, static_cast<int>(pt.values[metric]));
		}
	}
	const StatsAxis axis = StatsNiceAxis(maximum);
	p.Font(3);
	for (int v2 = 0; v2 <= axis.top; v2 += axis.step) {
		const int y = gy + gh - static_cast<int>(static_cast<int64_t>(gh) * v2 / axis.top);
		p.Fill(gx, y, gw, 1, v2 ? pal::hairline : pal::lineStrong);
		p.Text(m + 8, y - 10, gx - m - 16, 20, StatsFormatShort(v2), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	for (int k = 0; k <= 4; k++) {
		const uint32_t at = static_cast<uint32_t>(static_cast<uint64_t>(lastMs) * k / 4);
		p.Text(gx + gw * k / 4 - 40, gy + gh + 6, 80, 20, StatsFormatTime(at), pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	for (const auto &line : lines) {
		int px = -1, py = -1;
		for (const LeagueStatsPoint &pt : line.first->points) {
			const int x = gx + static_cast<int>(static_cast<int64_t>(gw) * pt.ms / lastMs);
			const int y = gy + gh - static_cast<int>(static_cast<int64_t>(gh) * std::max(0, static_cast<int>(pt.values[metric])) / axis.top);
			if (px >= 0) p.Line(px, py, x, y, line.second, 2);
			px = x;
			py = y;
		}
	}
}

void PaintHistory(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	if (st.ranked.detailMatch) {
		PaintMatchDetail(p, st);
		return;
	}
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH, false);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m;
	const int page = static_cast<int>(st.ranked.historyBack.size()) + 1;

	SummaryButton(p, m, top, 150, mx, my);
	int tx = m + 150 + 16;
	const int chipW = QueueSegmentsWidth(p, st);
	QueueSegments(p, st, tx + chipW, top, 44);
	tx += chipW + 16;
	Ghost(p, tx, top, 150, 44, "Newest", kRkHistoryNewest, v.historyLoaded && page > 1, mx, my);
	tx += 166;
	Ghost(p, tx, top, 52, 44, "\xE2\x80\xB9", kRkHistoryPrev, page > 1 && !v.historyLoading, mx, my);
	Ghost(p, tx + 60, top, 52, 44, "\xE2\x80\xBA", kRkHistoryNext, v.historyLoaded && v.history.more && !v.historyLoading, mx, my);
	p.Font(0);
	if (v.historyLoaded) RawText(p, tx + 128, top, m + inner - tx - 128, 44, TrFmt("Page %s", {std::to_string(page)}), pal::caption);

	const int y0 = top + 72, h = l.h - m - y0;
	PaintGlassCard(p, m, y0, inner, h);
	struct Col {
		const char *label;
		int x, w;
	};
	// The columns as wide as a 1920 screen gives them; on a narrower one each closes towards the least its content needs (measured in
	// this language), all in the same share, and the opponent takes what is left.
	p.Font(3);
	const int reportMin = std::max(GhostButtonWidth(p, "Report", false), p.TextWidth("Problem reported") + 8);
	p.Font(0);
	const int lengthMin = std::max(70, p.TextWidth(TrFmt("%s min", {"999"})) + 8);
	struct Fit {
		int min, pref;
	};
	const Fit fits[] = {{150, 200}, {120, 230}, {150, 300}, {lengthMin, std::max(90, lengthMin)}, {110, 170}, {reportMin, std::max(230, reportMin)}};
	const int opponentMin = 160, room = inner - 48 - 6 * 16;
	int sumMin = 0, sumPref = 0;
	for (const Fit &f : fits) {
		sumMin += f.min;
		sumPref += f.pref;
	}
	const float share = sumPref > sumMin ? std::max(0.0f, std::min(1.0f, (room - 200 - sumMin) / static_cast<float>(sumPref - sumMin))) : 1.0f;
	auto fitted = [share](const Fit &f) { return f.min + static_cast<int>((f.pref - f.min) * share); };
	const int resultW = fitted(fits[0]), mapW = fitted(fits[1]), civsW = fitted(fits[2]), durationW = fitted(fits[3]), playedW = fitted(fits[4]),
			  reportW = fitted(fits[5]);
	const int opponentW = std::max(opponentMin, room - resultW - mapW - civsW - durationW - playedW - reportW);
	int cx = 24;
	auto col = [&cx](const char *label, int w) {
		const Col c = {label, cx, w};
		cx += w + 16;
		return c;
	};
	const Col cols[] = {col("Result", resultW), col(LeagueQueueIsTeam(v.queue) ? "Opponents" : "Opponent", opponentW), col("Map", mapW), col("Civilizations", civsW),
		col("Length", durationW), col("Played", playedW), col("", reportW)};
	for (const Col &c : cols) {
		if (*c.label) Caps(p, m + c.x, y0, c.w, 44, c.label, pal::caption);
	}
	p.Fill(m + 1, y0 + 43, inner - 2, 1, pal::lineStrong);
	const int rowH = 56;
	int ry = y0 + 44;
	if (!v.historyLoaded || v.historyLoading) {
		const bool failed = !v.historyLoading && !v.historyError.empty();
		if (failed) {
			p.Font(0);
			p.Text(m + 24, ry + 18, inner - 48, 28, v.historyError, pal::bad);
			return;
		}
		std::vector<std::pair<int, int>> bars;
		for (const Col &c : cols) {
			if (*c.label) bars.push_back({m + c.x, c.w});
		}
		PaintSkeletonRows(p, m + 1, ry, inner - 2, rowH, std::max(1, std::min(10, (y0 + h - ry) / rowH)), bars);
		return;
	}
	if (v.history.games.empty()) {
		p.Font(0);
		p.TextWrapped(m + 24, ry + 18, inner - 48, 60, "Your ranked games will show here, with the Glory each one gave.", pal::caption);
		return;
	}
	std::vector<uint64_t> ids;
	for (const LeagueHistoryGame &g : v.history.games) ids.push_back(_strtoui64(g.opponent.steamId.c_str(), nullptr, 10));
	PatronsWatch(ids);
	const int64_t now = EpochNow();
	for (size_t i = 0; i < v.history.games.size() && ry + rowH <= y0 + h; i++) {
		const LeagueHistoryGame &g = v.history.games[i];
		if (p.Hover(m, ry, inner, rowH, mx, my)) p.Fill(m + 1, ry, inner - 2, rowH, pal::raised);
		// The whole row opens the game's page; the name's badge and "Report" over it keep their own clicks.
		p.AddHit(m, ry, inner, rowH, kRankedAct, kRkDetailOpen + static_cast<int>(i));
		const int chipW2 = PaintResultChip(p, g.result, m + cols[0].x, ry, rowH, 96, 26);
		PaintDelta(p, g, m + cols[0].x + chipW2 + 12, ry, cols[0].w - chipW2 - 12, rowH);
		const uint64_t id = _strtoui64(g.opponent.steamId.c_str(), nullptr, 10);
		const int av = 32;
		if (!PaintAvatar(p, m + cols[1].x, ry + (rowH - av) / 2, av, id)) p.Fill(m + cols[1].x, ry + (rowH - av) / 2, av, av, pal::hairline);
		if (g.opponents.size() >= 2) {
			p.Font(0);
			RawText(p, m + cols[1].x + av + 12, ry + 6, cols[1].w - av - 12, 24, OpponentsText(g), pal::text);
			p.Font(3);
			RawText(p, m + cols[1].x + av + 12, ry + 30, cols[1].w - av - 12, 20, TrFmt("with %s", {g.partner.name.empty() ? Tr("your partner") : g.partner.name}),
				pal::caption);
		}
		else {
			PaintName(p, m + cols[1].x + av + 12, ry, cols[1].w - av - 12, rowH, g.opponent.name.empty() ? Tr("Opponent") : g.opponent.name, id,
				RankBadge(g.opponent.tier, g.opponent.division), false, mx, my);
		}
		p.Font(0);
		std::string map = g.map.name.empty() ? std::string("\xE2\x80\x94") : LeagueMapText(g.map.name);
		if (!g.map.size.empty()) map += "  \xC2\xB7  " + LeagueMapText(g.map.size);
		RawText(p, m + cols[2].x, ry, cols[2].w, rowH, map, pal::body);
		const std::string civs = g.myCiv.empty() && g.opponentCiv.empty() ? std::string("\xE2\x80\x94") : CivsText(g);
		RawText(p, m + cols[3].x, ry, cols[3].w, rowH, civs, pal::body);
		RawText(p, m + cols[4].x, ry, cols[4].w, rowH, g.durationMs ? TrFmt("%s min", {std::to_string(g.durationMs / 60000)}) : std::string("\xE2\x80\x94"), pal::body);
		p.Font(3);
		RawText(p, m + cols[5].x, ry + 6, cols[5].w, 22, DateText(LocalTime(g.playedAt), true), pal::body);
		RawText(p, m + cols[5].x, ry + 28, cols[5].w, 20, AgoText(g.playedAt, now), pal::caption);
		const bool reported = std::find(v.disputed.begin(), v.disputed.end(), g.id) != v.disputed.end();
		if (reported) {
			p.Font(3);
			p.Text(m + cols[6].x, ry, cols[6].w, rowH, "Problem reported", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		else if (LeagueDisputeOpen(g.playedAt, now)) {
			Ghost(p, m + cols[6].x, ry + 10, cols[6].w, rowH - 20, "Report", kRkDisputeHistory + static_cast<int>(i), !v.disputeBusy, mx, my);
		}
		ry += rowH;
		p.Fill(m + 1, ry - 1, inner - 2, 1, pal::hairline);
	}
}

}

bool PaintRankedTab(Painter &p, const State &st)
{
	const bool dialog = st.ranked.disputeMatch != 0 || st.ranked.contentOpen || st.ranked.inviteOpen;
	if (dialog) p.noHover = true;
	if (st.ranked.resultOpen) PaintRankedResult(p, st);
	else if (!st.ranked.view.linked) return false;
	else if (st.ranked.ceremonyOpen) PaintCeremony(p, st);
	else {
		LeagueSeasonRefresh();
		LeagueBoardRefresh();
		LeagueMeRefresh();
		LeagueRecentRefresh();
		if (st.ranked.view.phase == LeagueRankedPhase::Idle) LeagueStatsRefresh();
		PatronsWatch({SteamOwnId()});
		if (st.ranked.sub == kRankedBoard) PaintBoard(p, st);
		else if (st.ranked.sub == kRankedHistory) PaintHistory(p, st);
		else if (st.ranked.sub == kRankedGuide) PaintRankedGuide(p, st);
		else PaintHub(p, st);
	}
	if (dialog) {
		UseSharedFonts(p);
		if (st.ranked.disputeMatch) PaintRankedDispute(p, st);
		else if (st.ranked.contentOpen) PaintRankedContent(p, st);
		else PaintRankedInvite(p, st);
		UseScreenFonts(p);
	}
	return true;
}

}
