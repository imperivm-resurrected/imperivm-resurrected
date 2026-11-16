// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string>

#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/leagueteam.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leaguerewards.h"
#include "ui/leagueui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

// "How it works": the ranks of the league and what a season leaves, then the way from the queue to the result, the Glory and the fair play
// rules. The numbers are the server's (glicko.ts, ranked.ts, league.ts of the league).
namespace mpui_impl {
namespace {

const int kGap = 24, kPad = 24;

void Display(Painter &p, int size)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
}

void Caps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
	p.Font(4);
	p.TextCaps(x, y, w, h, text, c, fmt | DT_END_ELLIPSIS, 2);
}

void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

// Wrapped text at `y`, translated; the height it took.
int Para(Painter &p, int x, int y, int w, const char *text, COLORREF c)
{
	const int h = p.Measure(w, text);
	p.TextWrapped(x, y, w, h + 2, text, c);
	return h;
}

int ParaRaw(Painter &p, int x, int y, int w, const std::string &text, COLORREF c)
{
	p.raw = true;
	const int h = p.Measure(w, text);
	p.TextWrapped(x, y, w, h + 2, text, c);
	p.raw = false;
	return h;
}

// A tab of the bar: lit while it is the one shown.
void Tab(Painter &p, int x, int y, int w, int h, const char *label, bool on, int arg, int mx, int my)
{
	const bool hot = !on && p.Hover(x, y, w, h, mx, my);
	if (on) p.Fill(x, y, w, h, pal::selected);
	p.Frame(x, y, w, h, on || hot ? pal::edgeHot : pal::edge);
	Caps(p, x, y, w, h, label, on || hot ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	if (!on) p.AddHit(x, y, w, h, kRankedAct, arg);
}

// The Glory a rank asks for: its hundreds for the six with divisions, the least Glory of the elite for Praetorian and Imperator.
std::string RangeText(LeagueTier tier)
{
	const int from = LeagueTierGloryFrom(tier);
	if (tier == LeagueTier::Legate) return TrFmt("From %s Glory", {std::to_string(from)});
	if (from >= 0) return TrFmt("%s to %s Glory", {std::to_string(from), std::to_string(from + 299)});
	return TrFmt("%s Glory or more", {tier == LeagueTier::Imperator ? "1700" : "1500"});
}

// One thing a season leaves: its picture on the left, its name and what it asks for.
int PaintKeep(Painter &p, int x, int y, int w, const char *name, const char *text)
{
	p.Font(1);
	p.Text(x, y, w, 26, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	return 30 + Para(p, x, y + 30, w, text, pal::textDim);
}

void PaintKeeps(Painter &p, const State &st, int x, int y, int w, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	const LeagueMeView me = LeagueMeState();
	const LeagueRanked &rk = LeagueMeRanked(me.me, v.queue);
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kPad, iw = w - 2 * kPad;
	PaintSectionLabel(p, ix, y + 16, iw, 22, "When the season closes");
	p.Font(3);
	int ty = y + 50;
	ty += Para(p, ix, ty, iw, "You keep the rewards of the highest rank you reached, with the season's number, for good.", pal::textDim) + 18;
	// The pictures show the player's own rank, or Centurion before they have one.
	const LeagueTier tier = rk.present && rk.tier != LeagueTier::None ? rk.tier : LeagueTier::Centurion;
	const int season = v.seasonLoaded ? v.season.number : rk.season;
	LeagueFrame frame;
	frame.season = season;
	frame.tier = tier;
	frame.seasonName = v.seasonLoaded ? LeagueSeasonName(v.season, v.lang) : std::string();
	const int pic = 64, tx = ix + pic + 20, tw = iw - pic - 20;
	DrawLeagueBadge(p, ix, ty, pic, LeagueVexillumBadge(tier, false, season));
	ty += std::max(pic, PaintKeep(p, tx, ty, tw, "Vexillum", "Next to your name, wherever it shows. From 10 games in the season.")) + 18;
	PaintFramedAvatar(p, ix + 4, ty, pic - 8, SteamOwnId(), st.player, frame);
	ty += std::max(pic, PaintKeep(p, tx, ty, tw, "Avatar frame", "The metal of your rank around your avatar. From 25 games.")) + 18;
	p.Fill(ix, ty + 8, pic, pic - 16, pal::raised);
	p.Font(3);
	RawText(p, ix, ty + 8, pic, pic - 16, LeagueFramePlate(Tr(LeagueTierName(tier)), season), LeagueFrameInk(frame), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	const int titleH = PaintKeep(p, tx, ty, tw, "Season title", "Your rank and the season under your name, as in the leaderboard. From 25 games.");
	p.Font(3);
	p.raw = true;
	p.Text(tx, ty + titleH + 2, tw, 20, LeagueTitleLabel(frame), LeagueFrameInk(frame), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	ty += std::max(pic, titleH + 24) + 22;
	// The small print of the elite and of the Glory lost by not playing.
	if (ty + 60 > y + h) return;
	p.Fill(ix, ty, iw, 1, pal::hairline);
	ty += 14;
	p.Font(3);
	ty += Para(p, ix, ty, iw,
			  "Praetorian and Imperator go by the place in the leaderboard when the season closes, with 25 games in the season and one in its last "
			  "10 days.",
			  pal::caption) +
		10;
	if (ty + 40 <= y + h) Para(p, ix, ty, iw, "From 1500 Glory up, every day after 10 without playing costs 25 Glory, never below 1500.", pal::caption);
}

// One step of the way, with its numeral.
int PaintStep(Painter &p, int x, int y, int w, const char *numeral, const char *name, const char *text)
{
	p.Frame(x, y, 40, 40, pal::edge);
	Display(p, 22);
	p.TextCaps(x, y, 40, 40, numeral, pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
	const int tx = x + 56, tw = w - 56;
	p.Font(1);
	p.Text(tx, y, tw, 24, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	return std::max(40, 26 + Para(p, tx, y + 26, tw, text, pal::textDim));
}

void PaintWay(Painter &p, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kPad, iw = w - 2 * kPad;
	PaintSectionLabel(p, ix, y + 16, iw, 22, "From the queue to the result");
	struct Step {
		const char *numeral, *name, *text;
	};
	static const Step kSteps[] = {
		{"I", "Search", "Pick 1 vs 1 or 2 vs 2, alone or with your duo. The league picks the map and the options of the season."},
		{"II", "Pairing", "The league looks for players of your level and widens the range the longer you wait: after 5 minutes, anyone."},
		{"III", "Accept", "You have 20 seconds to accept. The game makes the room with the season's options, locked."},
		{"IV", "Play", "If someone leaves before minute 3 the game is void: no Glory for anyone, but whoever left is still locked out of the queue. Later, leaving is a loss."},
		{"V", "Result", "Both games report the result and the league checks that they agree. If they don't, nothing counts until the team reviews the game."},
	};
	int ty = y + 54;
	const int gap = std::max(10, std::min(22, (y + h - ty - 5 * 80) / 5));
	for (const Step &s : kSteps) {
		if (ty + 40 > y + h - 8) break;
		ty += PaintStep(p, ix, ty, iw, s.numeral, s.name, s.text) + gap;
	}
}

// A rule: its name on the left, what it does on the right.
int PaintRule(Painter &p, int x, int y, int w, const char *name, const char *text)
{
	const int nameW = std::min(260, w * 2 / 5);
	p.Font(3);
	const int nh = Para(p, x, y, nameW - 16, name, pal::text);
	const int th = Para(p, x + nameW, y, w - nameW, text, pal::textDim);
	return std::max(nh, th);
}

void PaintGlory(Painter &p, const State &st, int x, int y, int w, int h)
{
	const LeagueMeView me = LeagueMeState();
	const LeagueRanked &rk = LeagueMeRanked(me.me, st.ranked.view.queue);
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kPad, iw = w - 2 * kPad;
	PaintSectionLabel(p, ix, y + 16, iw, 22, "Glory");
	int ty = y + 52;
	struct Rule {
		const char *name, *text;
	};
	static const Rule kRules[] = {
		{"Each game", "Between 5 and 40 Glory: about 20 against a rival of your level, more for beating a stronger one, less for a weaker one."},
		{"Going up", "At 100 Glory you go up a division, with no series. A promotion gives a shield: you don't drop for 3 games."},
		{"Going down", "A loss leaves you at 0 in your division at most; losing again at 0 drops you to the one below with 75."},
		{"Placement", "Your first 5 games of a season. You start on the cautious side of your level, at most Centurion I, and climb faster from there."},
		{"Same rivals", "Within a day, the 3rd and 4th games against the same players count half, and from the 5th on nothing."},
	};
	for (const Rule &r : kRules) {
		if (ty + 20 > y + h - 8) break;
		ty += PaintRule(p, ix, ty, iw, r.name, r.text) + 12;
	}
	// Where the player stands, when they have a rank.
	if (rk.present && rk.placementLeft <= 0 && rk.tier != LeagueTier::None && ty + 30 <= y + h - 8) {
		p.Fill(ix, ty, iw, 1, pal::hairline);
		p.Font(3);
		const std::string own = TrFmt("You: %s with %s Glory.", {LeagueRankLabel(rk.tier, rk.division), std::to_string(rk.glory)}) +
			(rk.shield > 0 ? " " + TrFmt("Shield for %s more games.", {std::to_string(rk.shield)}) : std::string());
		ParaRaw(p, ix, ty + 10, iw, own, LeagueTierInk(rk.tier));
	}
}

void PaintFairPlay(Painter &p, int x, int y, int w, int h)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kPad, iw = w - 2 * kPad;
	PaintSectionLabel(p, ix, y + 16, iw, 22, "Fair play");
	int ty = y + 52;
	struct Rule {
		const char *name, *text;
	};
	static const Rule kRules[] = {
		{"Declining a match", "Or letting the 20 seconds go by: 5 minutes out of the queue, twice as long for each other time that day."},
		{"Leaving a game", "A loss (or a void game before minute 3), and 10 minutes, 30 minutes, 2 hours and then a day out of the queue, by your abandons of the last 30 days."},
		{"Reports that disagree", "No Glory for anyone until the league's team reviews the game. You can report a problem from the result."},
	};
	for (const Rule &r : kRules) {
		if (ty + 20 > y + h - 8) break;
		ty += PaintRule(p, ix, ty, iw, r.name, r.text) + 12;
	}
}

}

void PaintRankLadder(Painter &p, int x, int y, int w, int h, LeagueTier mine)
{
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + kPad, iw = w - 2 * kPad;
	PaintSectionLabel(p, ix, y + 16, iw, 22, "The ranks of the league");
	p.Font(3);
	const int introH = Para(p, ix, y + 48, iw,
		"Six ranks of three divisions, III to I, of 100 Glory each. Above them, Praetorian and Imperator go by the place in the leaderboard.",
		pal::textDim);
	const int listY = y + 48 + introH + 14;
	const int rowH = std::max(40, std::min(80, (y + h - 12 - listY) / kLeagueTierCount));
	const int emblem = rowH - 8, rangeW = std::min(260, iw / 3);
	for (int i = 0; i < kLeagueTierCount; i++) {
		const LeagueTier tier = static_cast<LeagueTier>(i + 1);
		const int ry = listY + i * rowH;
		const bool own = tier == mine;
		if (own) p.Fill(x + 1, ry, w - 2, rowH, pal::selected);
		if (i && !own && static_cast<LeagueTier>(i) != mine) p.Fill(ix, ry, iw, 1, pal::hairline);
		DrawLeagueEmblem(p, ix, ry + 4, emblem, tier);
		const int tx = ix + emblem + 16, tw = iw - emblem - 16 - rangeW - 12;
		Display(p, rowH >= 60 ? 30 : 24);
		p.TextCaps(tx, ry + 2, tw, rowH / 2 + 4, LeagueTierName(tier), LeagueTierInk(tier), DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.Font(3);
		const std::string sub = LeagueTierHasDivisions(tier) ? "III  \xC2\xB7  II  \xC2\xB7  I"
															 : Tr(tier == LeagueTier::Imperator ? "Places 1 to 10" : "Places 11 to 50");
		p.raw = true;
		p.Text(tx, ry + rowH / 2 + 8, tw, 20, sub, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		p.Font(0);
		p.raw = true;
		p.Text(ix + iw - rangeW, ry, rangeW, own ? rowH / 2 + 6 : rowH, RangeText(tier), own ? pal::text : pal::textDim,
			DT_RIGHT | (own ? DT_BOTTOM : DT_VCENTER) | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (own) Caps(p, ix + iw - rangeW, ry + rowH / 2 + 8, rangeW, 18, "You are here", LeagueTierInk(tier), DT_RIGHT | DT_TOP | DT_SINGLELINE);
	}
}

void PaintRankedGuide(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m;
	int tx = m;
	{
		const int w = 150;
		const bool hot = p.Hover(tx, top, w, 44, mx, my);
		p.Frame(tx, top, w, 44, hot ? pal::edgeHot : pal::edge);
		PaintBackArrow(p, tx + 18, top + 22, hot ? pal::text : pal::textDim);
		Caps(p, tx + 18 + kBackArrowW, top, w - 26 - kBackArrowW, 44, "Summary", hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.AddHit(tx, top, w, 44, kRankedAct, kRkHub);
		tx += w + 16;
	}
	p.Font(4);
	const int tabW = std::max({220, p.CapsWidth("Ranks and rewards", 2) + 40, p.CapsWidth("How it works", 2) + 40});
	Tab(p, tx, top, tabW, 44, "Ranks and rewards", st.ranked.guideTab == 0, kRkGuideRanks, mx, my);
	Tab(p, tx + tabW - 1, top, tabW, 44, "How it works", st.ranked.guideTab == 1, kRkGuideRules, mx, my);

	const int y0 = top + 72, h = l.h - m - y0;
	const LeagueMeView me = LeagueMeState();
	const LeagueRanked &rk = LeagueMeRanked(me.me, st.ranked.view.queue);
	if (st.ranked.guideTab == 0) {
		const int leftW = (inner - kGap) * 56 / 100;
		PaintRankLadder(p, m, y0, leftW, h, rk.present && rk.placementLeft <= 0 ? rk.tier : LeagueTier::None);
		PaintKeeps(p, st, m + leftW + kGap, y0, inner - leftW - kGap, h);
		return;
	}
	const int leftW = (inner - kGap) / 2, rx = m + leftW + kGap, rw = inner - leftW - kGap;
	PaintWay(p, m, y0, leftW, h);
	const int gloryH = (h - kGap) * 60 / 100;
	PaintGlory(p, st, rx, y0, rw, gloryH);
	PaintFairPlay(p, rx, y0 + gloryH + kGap, rw, h - gloryH - kGap);
}

}
