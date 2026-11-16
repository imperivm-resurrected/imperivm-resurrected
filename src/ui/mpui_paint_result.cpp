// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/league.h"
#include "core/leagueseasons.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "game/civs.h"
#include "game/profiles.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/steamnet.h"
#include "ui/leaguemapthumb.h"
#include "ui/leagueui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

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

// An outlined button that is always on, its label centred with the arrow of the buttons that go back before it when `arrow`.
void ArrowGhost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool arrow, int mx, int my)
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
	case LeagueResultKind::Promotion: {
		std::string text = TrFmt("%s + %s = %s: you go up with %s Glory to spare.", {std::to_string(before.into), std::to_string(r.delta),
			std::to_string(before.into + r.delta), std::to_string(after.into)});
		if (after.capped && nt != LeagueTier::None) text += " " + TrFmt("%s Glory left to %s.", {std::to_string(100 - after.into), LeagueRankLabel(nt, nd)});
		return text;
	}
	case LeagueResultKind::Demotion:
		return TrFmt("With 0 Glory in %s and no shield, you drop to %s with %s Glory.", {RankName(r.tierBefore, r.divisionBefore), RankName(r.tierAfter, r.divisionAfter),
			std::to_string(after.into)});
	default:
		if (!after.capped || nt == LeagueTier::None) return TrFmt("%s Glory in the top division.", {std::to_string(r.gloryAfter)});
		if (r.delta < 0 && r.gloryAfter == r.gloryBefore + r.delta) {
			return TrFmt("%s %s = %s. %s to %s.", {std::to_string(before.into), SignedText(r.delta), std::to_string(after.into), std::to_string(100 - after.into),
				LeagueRankLabel(nt, nd)});
		}
		if (r.delta < 0) return TrFmt("The loss leaves you at %s Glory: you never drop below the floor of your division at once.", {std::to_string(after.into)});
		return TrFmt("%s + %s = %s. %s to %s.", {std::to_string(before.into), std::to_string(r.delta), std::to_string(after.into), std::to_string(100 - after.into),
			LeagueRankLabel(nt, nd)});
	}
}

// Where the stage puts each thing. A card of the mockup's 800 lines gets its places; a shorter one (900 lines of the 1280 x 720 screen
// leave it about 670) gets smaller emblems and type, and shares what is left over between three gaps.
struct StageLayout {
	int section, emblemY, emblem, rays, sideY, side, sideFont, sideLabelY, sideSubY, arrowY;
	int titleY, titleH, titleFont, titleFontPlain, deltaY, deltaH, deltaFont;
	int barY, shieldY, shieldH, buttonsY, buttonsH, buttonFont, footerY;
};

StageLayout StageFor(int h)
{
	if (h >= 790) return {28, 70, 220, 260, 135, 110, 22, 250, 276, 184, 312, 100, 100, 72, 414, 56, 56, 512, 584, 46, 666, 66, 36, 746};
	StageLayout s = {22, 52, 160, 200, 87, 90, 20, 184, 208, 124, 228, 80, 80, 60, 306, 44, 44, 388, 446, 40, 500, 58, 32, 572};
	const int extra = std::max(0, h - 640) / 3;
	s.barY += extra;
	s.shieldY += 2 * extra;
	s.buttonsY += 3 * extra;
	s.footerY += 3 * extra;
	return s;
}

// The line under the buttons once the game is rated: the run of wins the player is on, when the league already counts this game.
std::string StreakLine(const LeagueRankedResult &r, const LeagueMeView &me, const std::string &queue)
{
	const LeagueRanked &rk = LeagueMeRanked(me.me, queue);
	if (!me.loaded || !rk.present || rk.glory != r.gloryAfter || rk.streak < 2) return std::string();
	return TrFmt("%s wins in a row", {std::to_string(rk.streak)});
}

// A 2v2 lost while the partner had left it for good: the league takes only half the Glory from whoever stayed.
bool HalfLoss(const LeagueRankedGame &g)
{
	return LeagueQueueIsTeam(g.queue) && !g.won && !g.left && g.partnerLeft;
}

// Text wrapped in the box, each line centred, at most two lines of `lineH`. The text is already translated.
void Centered(Painter &p, int x, int y, int w, int lineH, const std::string &text, COLORREF c)
{
	const std::vector<std::wstring> lines = p.WrapLines(text, 0, w);
	SetTextColor(p.dc, c);
	for (size_t i = 0; i < lines.size() && i < 2; i++) {
		RECT r = p.R(x, y + static_cast<int>(i) * lineH, w, lineH);
		DrawTextW(p.dc, lines[i].c_str(), static_cast<int>(lines[i].size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
}

void PaintStage(Painter &p, const State &st, const LeagueMeView &me, int x, int y, int w, int h, Verdict verdict, uint32_t elapsed)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueRankedOutcome &o = st.ranked.view.result;
	const LeagueRankedResult &r = o.answer.ranked;
	const int cx = x + w / 2;
	const StageLayout L = StageFor(h);
	PaintGlassCard(p, x, y, w, h);
	p.AddHit(x, y, w, h, kRankedAct, kRkSkip);
	const bool rated = verdict == Verdict::Rated;
	const LeagueResultFrame f = rated ? LeagueResultAt(r, elapsed) : LeagueResultFrame();
	const bool team = LeagueQueueIsTeam(o.game.queue);
	const LeagueRanked &rk = LeagueMeRanked(me.me, o.game.queue);
	const LeagueTier nowTier = rated ? f.tier : me.loaded ? rk.tier : LeagueTier::None;
	const int nowDivision = rated ? f.division : me.loaded ? rk.division : 0;

	// The rays come only with a new rank.
	if (rated && f.rays > 0) PaintRays(p, cx, y + L.emblemY + L.emblem / 2, L.rays, Ink(f.tier), f.rays);
	const char *section = !rated ? "Ranked result"
		: f.kind == LeagueResultKind::Promotion ? (r.tierAfter != r.tierBefore ? "Rank promotion" : "Division promotion")
		: f.kind == LeagueResultKind::Demotion	? "Rank demotion"
		: f.kind == LeagueResultKind::Placement || f.kind == LeagueResultKind::PlacementDone ? "Placement"
																							: "Ranked result";
	Caps(p, x, y + L.section, w, 20, section, pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	// Where the player comes from, where they are and where they go.
	if (rated && f.kind != LeagueResultKind::Placement) {
		LeagueTier nt = LeagueTier::None;
		int nd = 0;
		LeagueNextDivision(r.tierAfter, r.divisionAfter, nt, nd);
		const int reach = std::min(300, w / 2 - 130), labelW = std::min(240, reach + 40);
		DrawLeagueEmblem(p, cx - reach - L.side / 2, y + L.sideY, L.side, r.tierBefore, 110);
		Display(p, L.sideFont);
		p.raw = true;
		p.TextCaps(cx - reach - labelW / 2, y + L.sideLabelY, labelW, 24, RankName(r.tierBefore, r.divisionBefore), Ink(r.tierBefore),
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		p.Font(3);
		RawText(p, cx - reach - labelW / 2, y + L.sideSubY, labelW, 20,
			r.tierBefore == LeagueTier::None ? Tr("placement") : TrFmt("%s Glory before", {std::to_string(r.gloryBefore)}), pal::caption,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (nt != LeagueTier::None) {
			DrawLeagueEmblem(p, cx + reach - L.side / 2, y + L.sideY, L.side, nt, 60);
			Display(p, L.sideFont);
			p.raw = true;
			p.TextCaps(cx + reach - labelW / 2, y + L.sideLabelY, labelW, 24, LeagueRankLabel(nt, nd), Ink(nt), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
			p.raw = false;
			p.Font(3);
			p.Text(cx + reach - labelW / 2, y + L.sideSubY, labelW, 20, "next division", pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		const int arrow = (reach - L.side / 2 + L.emblem / 2) / 2;
		p.ArrowRight(cx - arrow - 6, y + L.arrowY, 12, 16, pal::edgeHot);
		p.ArrowRight(cx + arrow - 6, y + L.arrowY, 12, 16, pal::edgeHot);
	}
	// The emblem of now: the old one fading, then the new one coming in a little smaller and growing to its size.
	const int big = L.emblem;
	if (!rated || !f.switched) DrawLeagueEmblem(p, cx - big / 2, y + L.emblemY, big, rated ? r.tierBefore : nowTier, rated ? f.oldAlpha : 255);
	else {
		// A few sizes only, so the emblem's pictures stay a handful.
		const int scale = f.newScale >= 100 ? 100 : 85 + (f.newScale - 85) / 4 * 4;
		const int size = big * scale / 100;
		DrawLeagueEmblem(p, cx - size / 2, y + L.emblemY + (big - size) / 2, size, f.tier, f.newAlpha);
	}

	const LeagueGloryPlace place = LeagueGloryPlaceOf(rated ? f.glory : 0);
	Display(p, L.titleFont);
	p.raw = true;
	std::string title;
	COLORREF titleInk = Ink(nowTier);
	switch (verdict) {
	case Verdict::Sending: title = Tr("Sending the result"); break;
	case Verdict::Awaiting: title = Tr(team ? "Waiting for the other players" : "Waiting for your opponent"); break;
	case Verdict::Void: title = Tr("Match void"); break;
	case Verdict::Disputed: title = Tr("In dispute"); break;
	default: title = f.kind == LeagueResultKind::Placement ? Tr("Placement") : RankName(nowTier, nowDivision); break;
	}
	if (verdict != Verdict::Rated) titleInk = pal::text;
	if (verdict != Verdict::Rated) Display(p, L.titleFontPlain);
	p.TextCaps(x, y + L.titleY, w, L.titleH, title, titleInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	if (rated && f.deltaShown) {
		Display(p, L.deltaFont);
		p.raw = true;
		// The game that ends the placement gives a rank, not Glory won or lost.
		if (f.kind == LeagueResultKind::PlacementDone) {
			p.TextCaps(x, y + L.deltaY, w, L.deltaH, TrFmt("Placed in %s", {RankName(r.tierAfter, r.divisionAfter)}), Ink(r.tierAfter),
				DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		}
		else p.Text(x, y + L.deltaY, w, L.deltaH, SignedText(f.delta) + " " + Tr("Glory"), r.delta >= 0 ? pal::ok : pal::bad, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}

	// The bar of the division, with what is being won (or what was lost) faint.
	const int barHalf = std::min(400, w / 2 - 50), bx = cx - barHalf, bw = 2 * barHalf, by = y + L.barY;
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
			RawText(p, bx, by + 24, bw, 22, Explanation(r, f.kind), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	else if (!rated) {
		p.Fill(bx, by, bw, 14, pal::bg);
		if (verdict == Verdict::Sending || verdict == Verdict::Awaiting) PaintBusyBar(p, bx, by + 3, bw, 8, GetTickCount());
		p.Font(0);
		const char *text = verdict == Verdict::Sending ? "Your report is on its way to the league. If you are offline it will be sent later."
			: verdict == Verdict::Awaiting && team	  ? "The league waits for the other players' reports. If they do not come, yours counts after 15 minutes."
			: verdict == Verdict::Awaiting			  ? "The league waits for your opponent's report. If it does not come, yours counts after 15 minutes."
			: verdict == Verdict::Disputed			  ? "The two reports disagree: the game does not count until the league reviews it."
													  : "The game ended too early or did not count: no Glory changes.";
		Centered(p, bx, by + 24, bw, 22, Tr(text), pal::textDim);
	}
	else {
		p.Font(0);
		RawText(p, bx, by + 24, bw, 22, Explanation(r, f.kind), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	if (rated && (f.shieldShown || (f.done && r.shield > 0))) {
		const int sw = std::min(600, w - 60), sx = cx - sw / 2, sy = y + L.shieldY;
		p.Fill(sx, sy, sw, L.shieldH, RGB(0x22, 0x2c, 0x1e));
		p.Font(1);
		RawText(p, sx, sy, sw, L.shieldH,
			(r.shield == 1 ? Tr("Rank shield: 1 more game") : TrFmt("Rank shield: %s more games", {std::to_string(r.shield)})) + "  \xC2\xB7  " +
				Tr("a loss does not take you down"),
			pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	if (!rated || f.done) {
		p.fonts[5] = BrandFont(BrandFace::Display, L.buttonFont, p.s);
		p.Button(cx - 354, y + L.buttonsY, 440, L.buttonsH, "Search another match", kRankedAct, kRkAgain, mx, my, st.ranked.view.phase == LeagueRankedPhase::Idle, true, 5);
		ArrowGhost(p, cx + 102, y + L.buttonsY, 252, L.buttonsH, "See the leaderboard", kRkResultBoard, false, mx, my);
	}
	p.Font(3);
	// Whoever left the game is locked out of the queue for a while: they read it here, with when they can search again.
	if (o.game.left) {
		const LeagueBan &ban = me.me.ban;
		const int64_t now = LeagueServerNowMs();
		std::string line = Tr("You left the game before it ended: the league locks your search for a while.");
		if (ban.until > now) line += " " + TrFmt("You can search again at %s.", {TimeText(LocalTime(ban.until))});
		Centered(p, x + 20, y + L.footerY, w - 40, 22, line, pal::bad);
		return;
	}
	// A 2v2 lost after the partner left it: only half the Glory goes.
	if (HalfLoss(o.game)) {
		Centered(p, x + 20, y + L.footerY, w - 40, 22,
			TrFmt("%s left the game: you only lose half the Glory.", {o.game.partner.empty() ? Tr("Your partner") : o.game.partner}), pal::warn);
		return;
	}
	// A rated game is confirmed in the summary; here the run of wins, once the league counts the game.
	if (rated) {
		if (f.done) RawText(p, x + 20, y + L.footerY, w - 40, 22, StreakLine(r, me, o.game.queue), pal::ok, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		return;
	}
	Centered(p, x + 20, y + L.footerY, w - 40, 22,
		Tr(team ? "If a player leaves before minute 3 the game does not count, and whoever left gets the penalty."
				: "If your opponent leaves before minute 3 the game does not count, and whoever left gets the penalty."),
		pal::caption);
}

std::string Grouped(uint32_t n)
{
	std::string s = std::to_string(n);
	// A no-break space: neither a comma nor a dot reads as a decimal point in any of the three languages.
	for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, "\xC2\xA0");
	return s;
}

// A caption in capitals that fits its box: the letters close up before anything is cut.
void FitCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c)
{
	p.Font(4);
	int spacing = 2;
	while (spacing > 0 && p.CapsWidth(text, spacing) > w) spacing--;
	p.TextCaps(x, y, w, h, text, c, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, spacing);
}

// What the game recorded of this player, as the profile keeps it: three to a row, the best unit last.
void PaintStatsGrid(Painter &p, const LeagueRankedGame &g, int x, int y, int w, int h)
{
	const int colW = w / 3, rowH = std::max(50, std::min(64, h / 2));
	struct Cell {
		const char *label;
		std::string value;
	};
	const Cell cells[] = {{"Units killed", Grouped(g.killed)}, {"Units lost", Grouped(g.lost)}, {"Peak army", Grouped(g.armyMax)}, {"Gold gathered", Grouped(g.gold)},
		{"Units produced", Grouped(g.produced)}};
	for (int i = 0; i < 5; i++) {
		const int cx = x + (i % 3) * colW, cy = y + (i / 3) * rowH;
		FitCaps(p, cx, cy, colW - 12, 16, cells[i].label, pal::caption);
		Display(p, 32);
		p.raw = true;
		p.Text(cx, cy + 18, colW - 12, 34, cells[i].value, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}
	const int bx = x + 2 * colW, by = y + rowH, bw = w - 2 * colW - 12;
	FitCaps(p, bx, by, bw, 16, "Best unit", pal::caption);
	// Its name and level, or the level alone when the game kept no name.
	std::string best = "\xE2\x80\x94";
	if (g.bestLevel > 0) {
		const std::string name = g.bestUnit.empty() ? std::string() : ProfileUnitName(g.bestUnit);
		best = name.empty() ? TrFmt("Level %s", {std::to_string(g.bestLevel)}) : TrFmt("%s \xC2\xB7 level %s", {name, std::to_string(g.bestLevel)});
	}
	p.Font(1);
	p.raw = true;
	if (p.TextWidth(best) > bw) p.Font(3);
	p.raw = false;
	RawText(p, bx, by + 18, bw, 34, best, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// A player's civilization in the game, by the name the game shows; empty when the report did not say.
std::string CivOfPlayer(const LeagueRankedGame &g, const std::string &steamId)
{
	for (const auto &c : g.civs) {
		if (c.first != steamId) continue;
		if (const Civ *civ = CivByName(c.second)) return CivDisplayName(*civ);
	}
	return std::string();
}

void PaintSummary(Painter &p, const State &st, const LeagueMeView &me, int x, int y, int w, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	const LeagueRankedGame &g = v.result.game;
	PaintGlassCard(p, x, y, w, h);
	const int ix = x + 24, iw = w - 48;
	Caps(p, ix, y + 16, iw, 22, "Game summary", pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	PaintLeagueMapThumb(p, ix, y + 52, 88, g.map);
	Display(p, 38);
	p.raw = true;
	p.TextCaps(ix + 108, y + 56, iw - 108, 40, g.map.empty() ? Tr("Ranked match") : LeagueMapText(g.map), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	const bool team = LeagueQueueIsTeam(g.queue);
	std::string facts = RankedQueueLabel(g.queue);
	if (g.durationMs) facts += "  \xC2\xB7  " + TrFmt("%s min", {std::to_string(g.durationMs / 60000)});
	if (v.seasonLoaded) facts += "  \xC2\xB7  " + TrFmt("Season %s", {LeagueRoman(v.season.number)});
	RawText(p, ix + 108, y + 98, iw - 108, 22, facts, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Fill(ix, y + 160, iw, 1, pal::hairline);
	const int half = (iw - 60) / 2, py = y + 172;
	int gridY = y + 250;
	if (team) {
		// Each team under its result: this player with the partner, and the two opponents.
		// The name, then what happened to them or, failing that, their civilization.
		auto member = [&](int sx, int ry, uint64_t id, const std::string &name, const char *mark) {
			if (!PaintAvatar(p, sx, ry + 2, 22, id)) p.Fill(sx, ry + 2, 22, 22, pal::hairline);
			const std::string civ = mark || !id ? std::string() : CivOfPlayer(g, std::to_string(id));
			p.Font(3);
			p.raw = true;
			const std::string right = mark ? Tr(mark) : civ;
			const int rightW = right.empty() ? 0 : std::min(half / 2, p.TextWidth(right) + 12);
			p.raw = false;
			p.Font(0);
			RawText(p, sx + 30, ry, half - 30 - rightW, 26, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			if (rightW) {
				p.Font(3);
				RawText(p, sx + half - rightW, ry, rightW, 26, right, mark ? pal::bad : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			}
		};
		auto heading = [&](int sx, const char *label, bool won) {
			FitCaps(p, sx, py, half, 18, Tr(label) + "  \xC2\xB7  " + Tr(won ? "Winner" : "Defeated"), won ? pal::ok : pal::caption);
		};
		heading(ix, "Your team", g.won);
		member(ix, py + 20, SteamOwnId(), st.player, g.left ? "left the game" : nullptr);
		member(ix, py + 46, _strtoui64(g.partnerId.c_str(), nullptr, 10), g.partner.empty() ? Tr("Your partner") : g.partner, g.partnerLeft ? "left the game" : nullptr);
		Display(p, 24);
		p.TextCaps(ix + half, py + 18, 60, 52, "VS", pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		heading(ix + half + 60, "The other team", !g.won);
		for (size_t i = 0; i < 2; i++) {
			const bool known = i < g.opponents.size();
			member(ix + half + 60, py + 20 + static_cast<int>(i) * 26, known ? _strtoui64(g.opponents[i].first.c_str(), nullptr, 10) : 0,
				known && !g.opponents[i].second.empty() ? g.opponents[i].second : Tr("Opponent"), nullptr);
		}
		p.Fill(ix, y + 244, iw, 1, pal::hairline);
		gridY = y + 256;
	}
	else {
		// The name, then the outcome and the civilization played.
		auto side = [&](int sx, uint64_t id, const std::string &name, bool won) {
			if (!PaintAvatar(p, sx, py + 8, 36, id)) p.Fill(sx, py + 8, 36, 36, pal::hairline);
			p.Font(1);
			RawText(p, sx + 48, py + 2, half - 48, 26, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(3);
			const std::string outcome = Tr(won ? "Winner" : "Defeated");
			p.raw = true;
			const int outcomeW = std::min(half - 48, p.TextWidth(outcome) + 2);
			p.raw = false;
			RawText(p, sx + 48, py + 28, outcomeW, 20, outcome, won ? pal::ok : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			const std::string civ = id ? CivOfPlayer(g, std::to_string(id)) : std::string();
			if (!civ.empty()) RawText(p, sx + 48 + outcomeW, py + 28, half - 48 - outcomeW, 20, "  \xC2\xB7  " + civ, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		};
		side(ix, SteamOwnId(), st.player, g.won);
		Display(p, 24);
		p.TextCaps(ix + half, py, 60, 52, "VS", pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		side(ix + half + 60, _strtoui64(g.opponentId.c_str(), nullptr, 10), g.opponent.empty() ? Tr("Opponent") : g.opponent, !g.won);
		p.Fill(ix, y + 236, iw, 1, pal::hairline);
	}

	// The verdict and, under it, the game's number in the season and "Report a problem", all at the foot of the card.
	const int footY = y + h - 66, lineY = footY + 30;
	if (g.stats) PaintStatsGrid(p, g, ix, gridY, iw, footY - 8 - gridY);
	else {
		p.Font(3);
		p.TextWrapped(ix, gridY + 2, iw, std::max(20, footY - 8 - (gridY + 2)),
			team ? "The league checks that the reports describe the same game before it gives any Glory."
				 : "The league checks that both reports describe the same game before it gives any Glory.",
			pal::caption);
	}
	const Verdict verdict = VerdictOf(v.result);
	const bool confirmed = verdict == Verdict::Rated && v.result.answer.status == "confirmed";
	const char *line = verdict == Verdict::Sending ? "Sending your report to the league..."
		: verdict == Verdict::Awaiting && team	   ? "Pending: waiting for the other players' reports."
		: verdict == Verdict::Awaiting			   ? "Pending: waiting for your opponent's report."
		: verdict == Verdict::Disputed && team	   ? "In dispute: the players' reports disagree."
		: verdict == Verdict::Disputed			   ? "In dispute: the reports of the two players disagree."
		: verdict == Verdict::Void				   ? "Void: the game does not count."
		: confirmed && team						   ? "Result confirmed by the players."
		: confirmed								   ? "Result confirmed by both players."
												   : "The league settled the game with the reports it got.";
	// The number counts once the league's standing includes this game: the player's Glory is the one the game left.
	std::string number;
	const LeagueRanked &rk = LeagueMeRanked(me.me, g.queue);
	if (verdict == Verdict::Rated && me.loaded && rk.present && rk.glory == v.result.answer.ranked.gloryAfter && rk.wins + rk.losses > 0) {
		number = TrFmt("Game no. %s of the season", {std::to_string(rk.wins + rk.losses)});
	}
	const bool reported = g.matchId > 0 && std::find(v.disputed.begin(), v.disputed.end(), g.matchId) != v.disputed.end();
	const bool canReport = g.matchId > 0 && !reported && v.result.state != LeagueResultState::Sending && LeagueDisputeOpen(g.at, LeagueServerNowMs());
	int rightW = 0;
	if (canReport) {
		p.Font(4);
		rightW = p.CapsWidth("Report a problem", 2) + 40;
	}
	else if (reported) {
		p.Font(3);
		rightW = p.TextWidth("Problem reported") + 4;
	}
	p.Font(0);
	p.raw = true;
	const std::string verdictText = Tr(line);
	const int verdictW = p.TextWidth(verdictText);
	p.raw = false;
	p.Font(3);
	p.raw = true;
	const int numberW = number.empty() ? 0 : p.TextWidth(number);
	p.raw = false;
	// The number goes under the verdict when it fits beside the button; otherwise after the verdict, or not at all.
	const bool numberBelow = !number.empty() && numberW + 16 + rightW <= iw;
	const bool numberAfter = !number.empty() && !numberBelow && verdictW + numberW + 40 <= iw;
	p.Font(0);
	RawText(p, ix, footY, iw, 26, verdictText, confirmed ? pal::ok : pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	if (numberAfter) RawText(p, ix + verdictW, footY, iw - verdictW, 26, "  \xC2\xB7  " + number, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	if (numberBelow) RawText(p, ix, lineY, iw - rightW - 16, 30, number, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	// "Report a problem": the result stays, and the league's team reviews the game.
	if (canReport) ArrowGhost(p, ix + iw - rightW, lineY, rightW, 30, "Report a problem", kRkDisputeResult, false, st.mouseX, st.mouseY);
	else if (reported) p.Text(ix + iw - rightW, lineY, rightW, 30, "Problem reported", pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void PaintPlace(Painter &p, const State &st, int x, int y, int w, int h)
{
	const LeagueRankedView &v = st.ranked.view;
	PaintGlassCard(p, x, y, w, h);
	Caps(p, x + 24, y + 16, w - 48, 22, LeagueQueueIsTeam(v.result.game.queue) ? "Your place  \xC2\xB7  2 vs 2" : "Your place  \xC2\xB7  1 vs 1", pal::redText,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	const std::string own = std::to_string(SteamOwnId());
	const LeagueBoardRow *mine = LeagueBoardFind(v.around, own);
	if (!mine) {
		p.Font(0);
		p.TextWrapped(x + 24, y + 60, w - 48, 60, "You will be on the leaderboard once your placement games are over.", pal::caption);
		return;
	}
	// Three rows: the one above, the player and the one below, as tall as the card lets them be.
	const int rowH = std::max(40, std::min(56, (h - 64) / 3)), avatar = rowH >= 50 ? 32 : 28;
	int ry = y + 56;
	for (const LeagueBoardRow &r : v.around.rows) {
		if (r.position < mine->position - 1 || r.position > mine->position + 1) continue;
		const bool you = r.steamId == own;
		if (you) {
			p.Fill(x + 1, ry, w - 2, rowH, pal::selected);
			p.Fill(x + 1, ry, 3, rowH, pal::red);
		}
		p.Font(you ? 1 : 0);
		RawText(p, x + 24, ry, 44, rowH, std::to_string(r.position), you ? pal::text : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		const int ay = ry + (rowH - avatar) / 2;
		if (!PaintAvatar(p, x + 72, ay, avatar, _strtoui64(r.steamId.c_str(), nullptr, 10))) p.Fill(x + 72, ay, avatar, avatar, pal::hairline);
		p.Font(0);
		RawText(p, x + 116, ry, w - 116 - 184, rowH, r.name.empty() ? Tr("Player") : r.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(1);
		RawText(p, x + w - 174, ry, 150, rowH, TrFmt("%s Glory", {std::to_string(r.glory)}), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		ry += rowH;
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
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;

	// The header of the screen: what happened in big and where it happened under it.
	{
		p.Font(4);
		const int bw = p.CapsWidth("Back to the lobby", 2) + 66;
		ArrowGhost(p, l.w - l.margin - bw, l.margin, bw, kHeaderButtonH, "Back to the lobby", kRkResultClose, true, mx, my);
		SelectObject(p.dc, BrandFont(BrandFace::Display, l.titleSize, p.s));
		const char *title = verdict == Verdict::Rated || verdict == Verdict::Awaiting || verdict == Verdict::Sending ? (o.game.won ? "Victory" : "Defeat")
																												  : "Ranked result";
		p.TextCaps(l.margin, l.titleY, l.w - 2 * l.margin - bw - 32, l.titleH, title, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		const bool team = LeagueQueueIsTeam(o.game.queue);
		std::string info = Tr(team ? "Ranked 2 vs 2" : "Ranked 1 vs 1");
		if (v.seasonLoaded) info += "  \xC2\xB7  " + TrFmt("Season %s", {LeagueRoman(v.season.number)});
		if (!o.game.map.empty()) info += "  \xC2\xB7  " + LeagueMapText(o.game.map);
		if (o.game.durationMs) info += "  \xC2\xB7  " + TrFmt("%s min", {std::to_string(o.game.durationMs / 60000)});
		if (team && !o.game.partner.empty()) info += "  \xC2\xB7  " + TrFmt("with %s", {o.game.partner});
		else if (!team && !o.game.opponent.empty()) info += "  \xC2\xB7  " + TrFmt("against %s", {o.game.opponent});
		p.raw = true;
		PaintScreenInfo(p, l, info);
		p.raw = false;
	}
	const int inner = l.w - 2 * l.margin, rightW = std::min(668, std::max(420, inner * 668 / 1792)), leftW = inner - rightW - 24;
	const int y = l.top, h = l.h - l.margin - y;
	PaintStage(p, st, me, l.margin, y, leftW, h, verdict, elapsed);
	const int rx = l.margin + leftW + 24, topH = (h - 24) * 520 / 776;
	PaintSummary(p, st, me, rx, y, rightW, topH);
	PaintPlace(p, st, rx, y + topH + 24, rightW, h - topH - 24);
}

}
