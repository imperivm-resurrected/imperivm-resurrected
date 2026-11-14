// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string>

#include "core/league.h"
#include "core/leagueranked.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leagueui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

void Display(Painter &p, int size)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
}

void Caps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE, int spacing = 2)
{
	p.Font(4);
	p.TextCaps(x, y, w, h, text, c, fmt | DT_END_ELLIPSIS, spacing);
}

void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

COLORREF Ink(LeagueTier t)
{
	return t == LeagueTier::None ? pal::caption : LeagueTierInk(t);
}

std::string RankName(LeagueTier t, int division)
{
	return t == LeagueTier::None ? Tr("Unranked") : LeagueRankLabel(t, division);
}

std::string Signed(int v)
{
	return v > 0 ? "+" + std::to_string(v) : v < 0 ? "\xE2\x88\x92" + std::to_string(-v) : "0";
}

// How a finished ranked game came out on the server, for the words of the screen.
enum class Verdict { Sending, Awaiting, Rated, Void, Disputed };

Verdict VerdictOf(const LeagueRankedOutcome &o)
{
	if (o.state == LeagueResultState::Sending) return Verdict::Sending;
	if (o.state == LeagueResultState::Awaiting) return Verdict::Awaiting;
	if (o.answer.status == "disputed" || o.answer.outcome == "disputed") return Verdict::Disputed;
	if (!o.answer.ranked.present) return Verdict::Void;
	return Verdict::Rated;
}

void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool arrow, int mx, int my)
{
	const bool hot = p.Hover(x, y, w, h, mx, my);
	p.Frame(x, y, w, h, hot ? pal::edgeHot : pal::edge);
	const COLORREF ink = hot ? pal::text : pal::textDim;
	p.Font(4);
	const int tw = p.CapsWidth(label, 2), tx = x + (w - tw - (arrow ? kBackArrowW : 0)) / 2;
	if (arrow) PaintBackArrow(p, tx, y + h / 2, ink);
	p.TextCaps(tx + (arrow ? kBackArrowW : 0), y, tw + 2, h, label, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
	p.AddHit(x, y, w, h, kRankedAct, arg);
}

// The explanation under the bar, with the numbers of this game.
std::string Explanation(const LeagueRankedResult &r, LeagueResultKind kind)
{
	const LeagueGloryPlace before = LeagueGloryPlaceOf(r.gloryBefore), after = LeagueGloryPlaceOf(r.gloryAfter);
	LeagueTier nt = LeagueTier::None;
	int nd = 0;
	LeagueNextDivision(after.tier, after.division, nt, nd);
	switch (kind) {
	case LeagueResultKind::Placement:
		return r.placementLeft == 1 ? Tr("1 placement game left: then you get your rank.")
									: TrFmt("%s placement games left: then you get your rank.", {std::to_string(r.placementLeft)});
	case LeagueResultKind::PlacementDone: return TrFmt("Your placement games put you in %s with %s Glory.", {RankName(r.tierAfter, r.divisionAfter), std::to_string(r.gloryAfter)});
	case LeagueResultKind::Promotion:
		return TrFmt("%s + %s = %s: you go up with %s Glory to spare.", {std::to_string(before.into), std::to_string(r.delta), std::to_string(before.into + r.delta),
			std::to_string(after.into)});
	case LeagueResultKind::Demotion:
		return TrFmt("With 0 Glory in %s and no shield, you drop to %s with %s Glory.", {RankName(r.tierBefore, r.divisionBefore), RankName(r.tierAfter, r.divisionAfter),
			std::to_string(after.into)});
	default:
		if (!after.capped || nt == LeagueTier::None) return TrFmt("%s Glory in the top division.", {std::to_string(r.gloryAfter)});
		if (r.delta < 0 && r.gloryAfter == r.gloryBefore + r.delta) {
			return TrFmt("%s %s = %s. %s to %s.", {std::to_string(before.into), Signed(r.delta), std::to_string(after.into), std::to_string(100 - after.into),
				LeagueRankLabel(nt, nd)});
		}
		if (r.delta < 0) return TrFmt("The loss leaves you at %s Glory: you never drop below the floor of your division at once.", {std::to_string(after.into)});
		return TrFmt("%s + %s = %s. %s to %s.", {std::to_string(before.into), std::to_string(r.delta), std::to_string(after.into), std::to_string(100 - after.into),
			LeagueRankLabel(nt, nd)});
	}
}

void PaintStage(Painter &p, const State &st, int x, int y, int w, int h, Verdict verdict, uint32_t elapsed)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedOutcome &o = st.ranked.view.result;
	const LeagueRankedResult &r = o.answer.ranked;
	const int cx = x + w / 2;
	PaintGlassCard(p, x, y, w, h);
	p.AddHit(x, y, w, h, kRankedAct, kRkSkip);
	const bool rated = verdict == Verdict::Rated;
	const LeagueResultFrame f = rated ? LeagueResultAt(r, elapsed) : LeagueResultFrame();
	const LeagueMeView me = LeagueMeState();
	const LeagueTier nowTier = rated ? f.tier : me.loaded ? me.me.ranked.tier : LeagueTier::None;
	const int nowDivision = rated ? f.division : me.loaded ? me.me.ranked.division : 0;

	// The rays come only with a new rank.
	if (rated && f.rays > 0) PaintRays(p, cx, y + 180, 260, Ink(f.tier), f.rays);
	const char *section = !rated ? "Ranked result"
		: f.kind == LeagueResultKind::Promotion ? (r.tierAfter != r.tierBefore ? "Rank promotion" : "Division promotion")
		: f.kind == LeagueResultKind::Demotion	? "Rank demotion"
		: f.kind == LeagueResultKind::Placement || f.kind == LeagueResultKind::PlacementDone ? "Placement"
																							: "Ranked result";
	Caps(p, x, y + 28, w, 20, section, pal::redText);

	// Where the player comes from, where they are and where they go.
	if (rated && f.kind != LeagueResultKind::Placement) {
		LeagueTier nt = LeagueTier::None;
		int nd = 0;
		LeagueNextDivision(r.tierAfter, r.divisionAfter, nt, nd);
		const int reach = std::min(300, w / 2 - 130), labelW = std::min(240, reach + 40);
		DrawLeagueEmblem(p, cx - reach - 55, y + 135, 110, r.tierBefore, 110);
		Display(p, 22);
		p.raw = true;
		p.TextCaps(cx - reach - labelW / 2, y + 250, labelW, 24, RankName(r.tierBefore, r.divisionBefore), Ink(r.tierBefore),
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		p.Font(3);
		RawText(p, cx - reach - labelW / 2, y + 276, labelW, 20,
			r.tierBefore == LeagueTier::None ? Tr("placement") : TrFmt("%s Glory before", {std::to_string(r.gloryBefore)}), pal::caption);
		if (nt != LeagueTier::None) {
			DrawLeagueEmblem(p, cx + reach - 55, y + 135, 110, nt, 60);
			Display(p, 22);
			p.raw = true;
			p.TextCaps(cx + reach - labelW / 2, y + 250, labelW, 24, LeagueRankLabel(nt, nd), Ink(nt), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
			p.raw = false;
			p.Font(3);
			p.Text(cx + reach - labelW / 2, y + 276, labelW, 20, "next division", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		const int arrow = (reach - 55 + 110) / 2;
		p.ArrowRight(cx - arrow - 6, y + 184, 12, 16, pal::edgeHot);
		p.ArrowRight(cx + arrow - 6, y + 184, 12, 16, pal::edgeHot);
	}
	// The emblem of now: the old one fading, then the new one coming in a little smaller and growing to its size.
	const int big = 220;
	if (!rated || !f.switched) DrawLeagueEmblem(p, cx - big / 2, y + 70, big, rated ? r.tierBefore : nowTier, rated ? f.oldAlpha : 255);
	else {
		// A few sizes only, so the emblem's pictures stay a handful.
		const int scale = f.newScale >= 100 ? 100 : 85 + (f.newScale - 85) / 4 * 4;
		const int size = big * scale / 100;
		DrawLeagueEmblem(p, cx - size / 2, y + 70 + (big - size) / 2, size, f.tier, f.newAlpha);
	}

	const LeagueGloryPlace place = LeagueGloryPlaceOf(rated ? f.glory : 0);
	Display(p, 100);
	p.raw = true;
	std::string title;
	COLORREF titleInk = Ink(nowTier);
	switch (verdict) {
	case Verdict::Sending: title = Tr("Sending the result"); break;
	case Verdict::Awaiting: title = Tr("Waiting for your opponent"); break;
	case Verdict::Void: title = Tr("Match void"); break;
	case Verdict::Disputed: title = Tr("In dispute"); break;
	default: title = f.kind == LeagueResultKind::Placement ? Tr("Placement") : RankName(nowTier, nowDivision); break;
	}
	if (verdict != Verdict::Rated) titleInk = pal::text;
	if (verdict != Verdict::Rated) Display(p, 72);
	p.TextCaps(x, y + 312, w, 100, title, titleInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	if (rated && f.deltaShown) {
		Display(p, 56);
		p.raw = true;
		p.Text(x, y + 414, w, 56, Signed(r.delta) + " " + Tr("Glory"), r.delta >= 0 ? pal::ok : pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}

	// The bar of the division, with what is being won (or what was lost) faint.
	const int barHalf = std::min(400, w / 2 - 50), bx = cx - barHalf, bw = 2 * barHalf, by = y + 512;
	if (rated && f.kind != LeagueResultKind::Placement) {
		LeagueTier nt = LeagueTier::None;
		int nd = 0;
		LeagueNextDivision(f.tier, f.division, nt, nd);
		const COLORREF ink = Ink(f.tier);
		Caps(p, bx, by - 26, barHalf, 22, RankName(f.tier, f.division), ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (nt != LeagueTier::None) Caps(p, bx + barHalf, by - 26, barHalf, 22, LeagueRankLabel(nt, nd) + "  \xC2\xB7  100 " + Tr("Glory"), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Fill(bx, by, bw, 14, pal::bg);
		if (f.ghostTo > f.ghostFrom) {
			PaintTint(p, bx + static_cast<int>(bw * f.ghostFrom), by, static_cast<int>(bw * (f.ghostTo - f.ghostFrom)), 14, f.ghostLoss ? pal::red : ink, 90);
		}
		p.Fill(bx, by, static_cast<int>(bw * f.bar), 14, ink);
		if (f.flash > 0) PaintTint(p, bx, by - 3, bw, 20, RGB(255, 255, 255), f.flash);
		if (place.capped) p.Fill(bx + static_cast<int>(bw * f.bar) - 1, by - 4, 3, 22, pal::text);
		if (f.done) {
			p.Font(0);
			RawText(p, bx, by + 24, bw, 22, Explanation(r, f.kind), pal::textDim);
		}
	}
	else if (!rated) {
		p.Fill(bx, by, bw, 14, pal::bg);
		if (verdict == Verdict::Sending || verdict == Verdict::Awaiting) PaintBusyBar(p, bx, by + 3, bw, 8, GetTickCount());
		p.Font(0);
		const char *text = verdict == Verdict::Sending ? "Your report is on its way to the league. If you are offline it will be sent later."
			: verdict == Verdict::Awaiting			  ? "The league waits for your opponent's report. If it does not come, yours counts after 15 minutes."
			: verdict == Verdict::Disputed			  ? "The two reports disagree: the game does not count until the league reviews it."
													  : "The game ended too early or did not count: no Glory changes.";
		p.Text(bx, by + 24, bw, 22, text, pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	else {
		p.Font(0);
		RawText(p, bx, by + 24, bw, 22, Explanation(r, f.kind), pal::textDim);
	}

	if (rated && (f.shieldShown || (f.done && r.shield > 0))) {
		const int sw = std::min(600, w - 60), sx = cx - sw / 2, sy = y + 584;
		p.Fill(sx, sy, sw, 46, RGB(0x22, 0x2c, 0x1e));
		p.Font(1);
		RawText(p, sx, sy, sw, 46,
			(r.shield == 1 ? Tr("Rank shield: 1 more game") : TrFmt("Rank shield: %s more games", {std::to_string(r.shield)})) + "  \xC2\xB7  " +
				Tr("a loss does not take you down"),
			pal::ok);
	}

	if (!rated || f.done) {
		p.fonts[5] = BrandFont(BrandFace::Display, 36, p.s);
		p.Button(cx - 354, y + 666, 440, 66, "Search another match", kRankedAct, kRkAgain, mx, my, st.ranked.view.phase == LeagueRankedPhase::Idle, true, 5);
		Ghost(p, cx + 102, y + 666, 252, 66, "See the leaderboard", kRkResultBoard, false, mx, my);
	}
	p.Font(3);
	const char *footer = verdict == Verdict::Rated && o.answer.status == "confirmed" ? "Result confirmed by both players."
		: verdict == Verdict::Rated ? "The league settled the game with the reports it got."
		: f.done || !rated			? "If your opponent leaves before minute 3 the game does not count, and whoever left gets the penalty."
									: "";
	p.Text(x, y + 746, w, 22, footer, verdict == Verdict::Rated && o.answer.status == "confirmed" ? pal::ok : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void PaintSummary(Painter &p, const State &st, int x, int y, int w, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	const LeagueRankedGame &g = v.result.game;
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + 24, iw = w - 48;
	Caps(p, ix, y + 16, iw, 22, "Game summary", pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	PaintIsotype(p, ix + 44, y + 96, 80, 160);
	Display(p, 38);
	p.raw = true;
	p.TextCaps(ix + 108, y + 56, iw - 108, 40, g.map.empty() ? Tr("Ranked match") : g.map, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	std::string facts = Tr("1 vs 1");
	if (g.durationMs) facts += "  \xC2\xB7  " + TrFmt("%s min", {std::to_string(g.durationMs / 60000)});
	if (v.seasonLoaded) facts += "  \xC2\xB7  " + TrFmt("Season %s", {std::to_string(v.season.number)});
	RawText(p, ix + 108, y + 98, iw - 108, 22, facts, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Fill(ix, y + 160, iw, 1, pal::hairline);
	const int half = (iw - 60) / 2, py = y + 172;
	auto side = [&](int sx, uint64_t id, const std::string &name, bool won) {
		if (!PaintAvatar(p, sx, py + 8, 36, id)) p.Fill(sx, py + 8, 36, 36, pal::hairline);
		p.Font(1);
		RawText(p, sx + 48, py + 2, half - 48, 26, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(sx + 48, py + 28, half - 48, 20, won ? "Winner" : "Defeated", won ? pal::ok : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	};
	side(ix, SteamOwnId(), st.player, g.won);
	Display(p, 24);
	p.TextCaps(ix + half, py, 60, 52, "VS", pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	side(ix + half + 60, _strtoui64(g.opponentId.c_str(), nullptr, 10), g.opponent.empty() ? Tr("Opponent") : g.opponent, !g.won);
	p.Fill(ix, y + 236, iw, 1, pal::hairline);
	p.Font(0);
	const Verdict verdict = VerdictOf(v.result);
	const char *line = verdict == Verdict::Sending ? "Sending your report to the league..."
		: verdict == Verdict::Awaiting			   ? "Pending: waiting for your opponent's report."
		: verdict == Verdict::Disputed			   ? "In dispute: the reports of the two players disagree."
		: verdict == Verdict::Void				   ? "Void: the game does not count."
		: v.result.answer.status == "confirmed"	   ? "Result confirmed by both players."
												   : "The league settled the game with the reports it got.";
	p.Text(ix, y + 252, iw, 26, line, verdict == Verdict::Rated && v.result.answer.status == "confirmed" ? pal::ok : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	p.Font(3);
	p.TextWrapped(ix, y + 290, iw, 60, "The league checks that both reports describe the same game before it gives any Glory.", pal::caption);
}

void PaintPlace(Painter &p, const State &st, int x, int y, int w, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	PaintGlassCard(p, x, y, w, h);
	Caps(p, x + 24, y + 16, w - 48, 22, "Your place  \xC2\xB7  1 vs 1", pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	const std::string own = std::to_string(SteamOwnId());
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	if (!mine) {
		p.Font(0);
		p.TextWrapped(x + 24, y + 60, w - 48, 60, "You will be on the leaderboard once your placement games are over.", pal::caption);
		return;
	}
	int ry = y + 56;
	for (const LeagueBoardRow &r : v.around.rows) {
		if (r.position < mine->position - 1 || r.position > mine->position + 1) continue;
		const bool you = r.steamId == own;
		if (you) {
			p.Fill(x + 1, ry, w - 2, 56, pal::selected);
			p.Fill(x + 1, ry, 3, 56, pal::red);
		}
		p.Font(you ? 1 : 0);
		RawText(p, x + 24, ry, 44, 56, std::to_string(r.position), you ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (!PaintAvatar(p, x + 72, ry + 12, 32, _strtoui64(r.steamId.c_str(), nullptr, 10))) p.Fill(x + 72, ry + 12, 32, 32, pal::hairline);
		p.Font(0);
		RawText(p, x + 116, ry, w - 116 - 120, 56, r.name.empty() ? Tr("Player") : r.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(1);
		RawText(p, x + w - 120, ry, 96, 56, TrFmt("%s Glory", {std::to_string(r.glory)}), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		ry += 56;
	}
}

}

void PaintRankedResult(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedView &v = st.ranked.view;
	const LeagueRankedOutcome &o = v.result;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const Verdict verdict = VerdictOf(o);
	const uint32_t elapsed = static_cast<uint32_t>(GetTickCount() - st.ranked.resultAt);

	// The header of the screen: what happened in big and where it happened under it.
	{
		p.Font(4);
		const int bw = p.CapsWidth("Back to the lobby", 2) + 66;
		Ghost(p, l.w - l.margin - bw, l.margin, bw, kHeaderButtonH, "Back to the lobby", kRkResultClose, true, mx, my);
		SelectObject(p.dc, BrandFont(BrandFace::Display, l.titleSize, p.s));
		const char *title = verdict == Verdict::Rated || verdict == Verdict::Awaiting || verdict == Verdict::Sending ? (o.game.won ? "Victory" : "Defeat")
																												  : "Ranked result";
		p.TextCaps(l.margin, l.titleY, l.w - 2 * l.margin - bw - 32, l.titleH, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		std::string info = Tr("Ranked 1 vs 1");
		if (v.seasonLoaded) info += "  \xC2\xB7  " + TrFmt("Season %s", {std::to_string(v.season.number)});
		if (!o.game.map.empty()) info += "  \xC2\xB7  " + o.game.map;
		if (o.game.durationMs) info += "  \xC2\xB7  " + TrFmt("%s min", {std::to_string(o.game.durationMs / 60000)});
		if (!o.game.opponent.empty()) info += "  \xC2\xB7  " + TrFmt("against %s", {o.game.opponent});
		p.raw = true;
		PaintScreenInfo(p, l, info);
		p.raw = false;
	}
	const int inner = l.w - 2 * l.margin, rightW = std::min(668, std::max(420, inner * 668 / 1792)), leftW = inner - rightW - 24;
	const int y = l.top, h = l.h - l.margin - y;
	PaintStage(p, st, l.margin, y, leftW, h, verdict, elapsed);
	const int rx = l.margin + leftW + 24, topH = (h - 24) * 520 / 776;
	PaintSummary(p, st, rx, y, rightW, topH);
	PaintPlace(p, st, rx, y + topH + 24, rightW, h - topH - 24);
}

}
