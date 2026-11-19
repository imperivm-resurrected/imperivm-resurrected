// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "game/civs.h"
#include "game/gameroom.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_tourney.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/colorwheel.h"
#include "ui/fullscreen.h"
#include "ui/leagueui.h"
#include "ui/mpui_tourney_internal.h"
#include "ui/textedit.h"

namespace mpui_tourney_impl {
namespace {

const COLORREF kGold = RGB(0xe0, 0xb5, 0x4a), kSilver = RGB(0xc9, 0xce, 0xd4), kBronze = RGB(0xcf, 0x8a, 0x55);
const COLORREF kWarnBack = RGB(0x2a, 0x24, 0x16), kWarnEdge = RGB(0x5c, 0x4c, 0x24), kOkBack = RGB(0x1c, 0x26, 0x18), kOkEdge = RGB(0x3c, 0x5a, 0x32);

// A rounded state under a player: in the room or not yet.
void PaintPill(Painter &p, int cx, int y, const std::string &text, bool ok)
{
	UseFace(p, BrandFace::UiStrong, 15);
	const int w = p.TextWidth(text) + 44;
	const int x = cx - w / 2;
	p.Fill(x, y, w, 34, ok ? kOkBack : kWarnBack);
	p.Frame(x, y, w, 34, ok ? kOkEdge : kWarnEdge);
	p.Shape(x + 14, y + 13, 8, 8, InUnitEllipse, ok ? pal::ok : pal::warn);
	p.Text(x + 28, y, w - 34, 34, text, ok ? pal::ok : pal::warn, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// The player's match before this one, as a line: "Quarterfinal: won against Furio 1-0".
std::string PreviousText(const TourneyDetail &d, int64_t entry, int round)
{
	const TourneyMatch *before = nullptr;
	for (const TourneyMatch *m : TourneyPathOf(d, entry)) {
		if (m->round < round) before = m;
	}
	if (!before) return std::string();
	const std::string stage = StageText(before->stage, before->round);
	if (before->status == TourneyMatchStatus::Bye) return TrFmt("%s: bye", {stage});
	const bool sideB = before->b.entryId == entry;
	const TourneyCard &other = sideB ? before->a : before->b;
	if (before->status == TourneyMatchStatus::Walkover) return TrFmt("%s: %s did not come", {stage, other.name});
	const int mine = sideB ? before->scoreB : before->scoreA, theirs = sideB ? before->scoreA : before->scoreB;
	return TrFmt("%s: won against %s %s", {stage, other.name, std::to_string(mine) + "\xE2\x80\x93" + std::to_string(theirs)});
}

namespace {
RECT g_colorAnchor = {};
}

// The civilization the player takes into the room, as a menu: the mod puts it in their slot there and gets them ready.
void PaintCivMenu(Painter &p, const State &st, int x, int y, int w)
{
	const int race = MatchCiv(g_tourney);
	std::string name = Tr("The one the room gives you");
	for (const auto &c : MatchCivChoices()) {
		if (c.first == race) name = c.second;
	}
	const bool open = st.dropOpen && st.dropKind == kDropTourney && st.dropIndex == kCupMenuCiv;
	const bool raw = p.raw;
	p.raw = true;
	PaintCombo(p, x, y, w, 34, Tr("Civilization") + ": " + name, open, true, st.mouseX, st.mouseY, kTourney, ArgOf(kSubCivMenu, 0), &g_dropAnchor);
	p.raw = raw;
	// The colour under it, as the room's: the one chosen, and its menu on a click.
	const COLORREF ink = MatchColorRef(g_tourney);
	const int cy = y + 34 + 8;
	p.raw = true;
	PaintCombo(p, x, cy, w, 34, Tr("Colour") + ": " + (ink == CLR_INVALID ? Tr("the room's own") : std::string()), g_tourney.colorOpen, true, st.mouseX, st.mouseY, kTourney,
		ArgOf(kSubColorOpen, 0), &g_colorAnchor);
	p.raw = raw;
	if (ink != CLR_INVALID) {
		p.Fill(x + 92, cy + 8, w - 92 - 40, 18, ink);
		p.Frame(x + 92, cy + 8, w - 92 - 40, 18, pal::bg);
	}
}

int PaintPlayerColumn(Painter &p, int cx, int y, int h, uint64_t id, const std::string &name, bool me, int seed, const std::string &before,
	const std::string &state, bool stateOk, int extraH = 0)
{
	const int disc = h >= 400 ? 132 : 108;
	int cy = y + (h - (disc + 216 + extraH)) / 2;
	PaintPlayerDisc(p, cx - disc / 2, cy, disc, id, name, me ? pal::redText : pal::edgeHot);
	cy += disc + 22;
	PaintCaps(p, cx - 150, cy, 300, 20, me ? "You" : "Opponent", me ? pal::redText : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	cy += 24;
	UseDisplay(p, h >= 400 ? 64 : 52);
	p.raw = true;
	const int nameW = std::min(560, p.CapsWidth(name, 1) + 4);
	const LeagueBadge badge = id ? PlayerBadgeOf(id) : LeagueBadge();
	const int room = LeagueBadgeRoom(badge);
	p.TextCaps(cx - (nameW + room) / 2, cy, nameW, 66, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	if (badge.Shown()) DrawLeagueBadge(p, cx - (nameW + room) / 2 + nameW + kLeagueBadgeGap, cy + 23, kLeagueBadgeSize, badge);
	cy += 72;
	p.Font(0);
	p.raw = true;
	p.Text(cx - 260, cy, 520, 24, seed ? TrFmt("Seed %s", {std::to_string(seed)}) : std::string(), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Font(3);
	p.Text(cx - 260, cy + 26, 520, 22, before, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (!state.empty()) {
		p.raw = true;
		PaintPill(p, cx, cy + 62, state, stateOk);
		p.raw = false;
	}
	return cy + 62 + 34;
}

// A team's side of the match: its name, its seed and how it got here, and its two players, each with their picture and whether they are
// in the room (not while the game is played).
void PaintTeamColumn(Painter &p, const State &st, const TourneyNext &n, int cx, int y, int w, int h, const std::string &name, int seed,
	const std::vector<TourneyMember> &members, bool mine, const std::string &before, bool live)
{
	const bool roomy = h >= 400;
	const int rowH = roomy ? 76 : 62, disc = roomy ? 56 : 46, nameH = roomy ? 58 : 48;
	const int blockH = 20 + 6 + nameH + 4 + 22 + 14 + 2 * rowH;
	int cy = y + std::max(12, (h - blockH) / 2);
	const UINT center = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	PaintCaps(p, cx - w / 2, cy, w, 20, mine ? "Your team" : "Opponents", mine ? pal::redText : pal::caption, center);
	cy += 26;
	const std::string title = !name.empty() ? name : MembersText(members);
	int size = roomy ? 58 : 48;
	UseDisplay(p, size);
	p.raw = true;
	while (size > 30 && p.CapsWidth(title, 1) > w) {
		size -= 4;
		UseDisplay(p, size);
	}
	p.TextCaps(cx - w / 2, cy, w, nameH, title, pal::text, center, 1);
	cy += nameH + 4;
	std::string line = seed ? TrFmt("Seed %s", {std::to_string(seed)}) : std::string();
	if (!before.empty()) line += (line.empty() ? "" : " \xC2\xB7 ") + before;
	p.Font(3);
	p.Text(cx - w / 2, cy, w, 22, line, pal::caption, center);
	p.raw = false;
	cy += 22 + 14;
	const int rowW = std::min(w, 420), rx = cx - rowW / 2;
	for (const TourneyMember &m : members) {
		const bool me = m.steamId && m.steamId == SteamOwnId();
		const int dy = cy + (rowH - disc) / 2;
		PaintPlayerDisc(p, rx, dy, disc, m.steamId, m.name, me ? pal::redText : pal::edgeHot);
		const int tx = rx + disc + 16, tw = rx + rowW - tx;
		const int youW = me ? TagWidth(p, "You") + 10 : 0;
		const std::string who = m.name.empty() ? Tr("A player") : m.name;
		UseFace(p, me ? BrandFace::UiStrong : BrandFace::Body, roomy ? 22 : 19);
		p.raw = true;
		const int nameW = std::min(tw - youW, p.TextWidth(who) + 4);
		const int top = live ? cy + (rowH - 28) / 2 : cy + rowH / 2 - 27;
		p.Text(tx, top, nameW, 28, who, me ? pal::redText : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (me) PaintTag(p, tx + nameW + 8, top + 3, "You", pal::red, pal::onRed);
		if (!live) {
			const bool sideIn = me ? n.me.inRoom : mine ? n.team.inRoom : n.opponent.inRoom;
			const bool in = MemberInRoom(st, n.match, m, sideIn);
			p.Shape(tx, cy + rowH / 2 + 8, 8, 8, InUnitEllipse, in ? pal::ok : pal::warn);
			p.Font(3);
			p.raw = true;
			p.Text(tx + 14, cy + rowH / 2 + 2, tw - 14, 20, in ? Tr("In the room") : Tr("Not in the room yet"), in ? pal::ok : pal::warn,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
		cy += rowH;
	}
}

// The map of the game to play, as its name and the line under it: "Mountainous" over "Random map · small · 2 players".
void MapLines(const TourneyNext &n, const TourneyMap &map, std::string &name, std::string &sub)
{
	if (n.pick.present) {
		name = n.MyPick() ? Tr("You choose it") : TrFmt("%s chooses it", {n.opponent.name.empty() ? Tr("Your opponent") : n.opponent.name});
		sub = TrFmt("%s left to choose", {TourneyClock(std::max<int64_t>(0, n.pick.until - NowMs()))});
		return;
	}
	TourneyMap mapOnly = map;
	mapOnly.size.clear();
	name = map.Known() ? MapText(mapOnly) : Tr("Chosen in the room");
	// The size alone, as MapText writes it after the name.
	const std::string full = MapText(map);
	(void)full;
	sub.clear();
}

// The games of a series, one a row: its number, its map and how it went for this player.
void PaintSeriesRows(Painter &p, const TourneyNext &n, const TourneyPageView &page, int x, int y, int w, int rowH)
{
	const bool pool = page.loaded && !page.detail.seriesPool.empty();
	const bool over = TourneySeriesOver(n.games, n.bestOf);
	for (int no = 1; no <= n.bestOf; no++) {
		const int ry = y + (no - 1) * rowH;
		const TourneyGame *g = TourneyGameOf(n.games, no);
		const bool now = no == n.game && !over;
		const bool won = g && !g->winner.empty() && g->winner == n.me.side, lost = g && !g->winner.empty() && !won;
		if (now) p.Fill(x, ry, w, rowH - 2, pal::selected);
		const int badge = rowH - 6;
		p.Fill(x + 4, ry + 2, badge, badge - 2, now ? pal::red : pal::raised);
		UseFace(p, BrandFace::UiStrong, 12);
		p.Text(x + 4, ry + 2, badge, badge - 2, std::to_string(no), now ? pal::onRed : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		std::string map, result;
		COLORREF mapInk = pal::body, resultInk = pal::caption;
		if (g && g->map.Known()) map = MapText(g->map);
		else if (now && n.pick.present) {
			map = Tr("To be chosen");
			mapInk = pal::warn;
		}
		else if (now) map = MapText(TourneyGameMap(n));
		else if (no > n.game && pool) {
			map = Tr("The loser chooses it");
			mapInk = pal::caption;
		}
		else {
			map = MapText(n.map);
			mapInk = pal::caption;
		}
		if (won) {
			result = Tr("Game won");
			resultInk = pal::ok;
		}
		else if (lost) {
			result = Tr("Game lost");
			resultInk = pal::bad;
		}
		else if (now) {
			result = Tr(n.status == TourneyMatchStatus::Live ? "Being played" : "Up next");
			resultInk = pal::redText;
		}
		else result = Tr("If needed");
		p.Font(3);
		const int resultW = std::min(w / 3, p.TextWidth(result) + 8), mx = x + 4 + badge + 10;
		p.Text(x + w - resultW - 4, ry, resultW, rowH - 2, result, resultInk, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(mx, ry, x + w - resultW - 12 - mx, rowH - 2, map, mapInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
}

// The middle of the match's card: the stage, VS and the map of the game, with the scenario's preview or the drawing of a random map's land;
// in a series, the map beside its name and the series' games under them.
void PaintMatchCenter(Painter &p, const TourneyNext &n, const TourneyPageView &page, const TourneyMap &map, int x, int y, int w, int h, bool roomy)
{
	std::string name, sub;
	MapLines(n, map, name, sub);
	const std::string caption = BracketStageText(n.bracket, n.stage, n.round) + " \xC2\xB7 " + TrFmt("Bo%s", {std::to_string(n.bestOf)});
	p.raw = true;
	if (n.bestOf <= 1) {
		const int pic = std::max(72, std::min(roomy ? 150 : 118, h - 32 - 210));
		int at = y + (h - (20 + 124 + 8 + pic + 10 + 46)) / 2;
		PaintCaps(p, x, at, w, 20, caption, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		UseDisplay(p, 120);
		p.TextCaps(x, at + 20, w, 124, Tr("Vs"), pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		at += 152;
		PaintMapPicture(p, map, x + (w - pic) / 2, at, pic);
		at += pic + 10;
		UseFace(p, BrandFace::UiStrong, 17);
		p.Text(x, at, w, 24, name, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(x, at + 26, w, 20, sub, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		return;
	}
	const int pad = 16, rowH = roomy ? 28 : 24, pic = roomy ? 104 : 80, vsH = roomy ? 96 : 70;
	int at = y + (h - (20 + vsH + 8 + pic + 12 + n.bestOf * rowH)) / 2;
	PaintCaps(p, x, at, w, 20, caption, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	UseDisplay(p, vsH - 4);
	p.TextCaps(x, at + 20, w, vsH, Tr("Vs"), pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	at += 20 + vsH + 8;
	PaintMapPicture(p, map, x + pad, at, pic);
	const int tx = x + pad + pic + 14, tw = x + w - pad - tx;
	PaintCaps(p, tx, at + pic / 2 - 38, tw, 18, TrFmt("Game %s of %s", {std::to_string(n.game), std::to_string(n.bestOf)}), n.pick.present ? pal::warn : pal::redText);
	UseFace(p, BrandFace::UiStrong, 17);
	p.Text(tx, at + pic / 2 - 18, tw, 24, name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(3);
	p.Text(tx, at + pic / 2 + 8, tw, 20, sub, n.pick.present ? pal::warn : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	at += pic + 12;
	PaintSeriesRows(p, n, page, x + pad, at, w - 2 * pad, rowH);
	p.raw = false;
}

// The rival's Steam chat, beside the button to the room, with the by-rounds pace: the players agree on a time between them.
int ChatButtonWidth(Painter &p)
{
	return SecondButtonWidth(p, Tr("Write on Steam"));
}

// The round's map on this computer, in a line of the checks before the room.
std::string MapCheckText(const TourneyMap &m, const CupMapState &s)
{
	if (!m.Scenario()) return Tr("Random map: nothing to download");
	switch (s.status) {
	case CupMapStatus::Ready: return Tr("The round's scenario is here, the same as everyone's");
	case CupMapStatus::Checking: return Tr("Checking your copy of the round's scenario...");
	case CupMapStatus::Downloading: return Tr("Downloading the round's scenario...");
	case CupMapStatus::Missing: return Tr("You don't have the round's scenario");
	case CupMapStatus::Different: return Tr("Your copy of the round's scenario is not the game's");
	default: return Tr("The round's scenario could not be downloaded");
	}
}

std::string PlaceText(int placement, int rounds)
{
	if (placement == 1) return Tr("Champion");
	if (placement == 2) return Tr("Finalist");
	if (placement == 3) return Tr("Semifinals \xC2\xB7 3rd place");
	if (placement <= 0) return std::string();
	// 5 in the quarterfinals, 9 in the round of 16...: the round counted from the final.
	int round = rounds, place = 2;
	while (place < placement && round > 1) {
		round--;
		place = (1 << (rounds - round)) + 1;
	}
	return TrFmt("%s \xC2\xB7 places %s to %s", {StageText(TourneyStageOf(round, rounds), round), std::to_string(placement), std::to_string(2 * placement - 2)});
}

}

void PaintPickPanel(Painter &p, const State &st, const TourneyNext &n, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const bool mine = n.MyPick(), busy = LeagueTourneyBusy(TourneyOp::Pick, n.match);
	const int64_t left = n.pick.until - NowMs();
	const bool open = mine && !busy && left > 0;
	const bool raw = p.raw;
	p.raw = true;
	PaintGlassCard(p, x, y, w, h);
	p.Fill(x, y, 4, h, mine ? pal::red : pal::edgeHot);
	// Below 170 the title and the clock go to the left of the options; otherwise above them.
	const bool beside = h < 170;
	const int pad = beside ? 18 : 24, headW = beside ? std::min(400, w * 30 / 100) : w - 2 * pad;
	const std::string game = std::to_string(n.game);
	const std::string title = mine ? TrFmt("Choose the map of game %s", {game}) : TrFmt("%s is choosing the map of game %s", {n.opponent.name.empty() ? Tr("Your opponent") : n.opponent.name, game});
	// Beside the options there is room for the short line only.
	std::string sub = mine ? Tr("You lost the last game: the next map is yours, among those not played yet.") : Tr("The loser of the last game chooses among the maps not played yet.");
	if (busy) sub = Tr("Sending your choice...");
	else if (left <= 0) sub = Tr("Time is up: the map is being drawn.");
	else if (beside) sub = Tr("If time runs out, it is drawn.");
	else if (mine) sub += " " + Tr("If time runs out, it is drawn.");
	const std::string clock = TourneyClock(std::max<int64_t>(0, left));
	const COLORREF clockInk = left < 15000 ? pal::bad : mine ? pal::warn : pal::text;
	const int clockH = beside ? 42 : 52;
	UseDisplay(p, clockH);
	const int clockW = p.CapsWidth(clock, 0) + 8;
	int tilesX = x + pad, tilesY = y + pad, tilesW = w - 2 * pad, tilesH = h - 2 * pad;
	if (beside) {
		PaintCaps(p, x + pad, y + pad, headW, 20, title, mine ? pal::redText : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		UseDisplay(p, clockH);
		p.Text(x + pad, y + pad + 22, headW, clockH, clock, clockInk, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		const int subY = y + pad + 22 + clockH + 4;
		p.TextWrapped(x + pad, subY, headW, std::max(0, y + h - pad - subY), sub, pal::caption);
		tilesX = x + pad + headW + 16;
		tilesW = x + w - pad - tilesX;
	}
	else {
		PaintCaps(p, x + pad, y + pad, headW - clockW - 16, 20, title, mine ? pal::redText : pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		UseDisplay(p, clockH);
		p.Text(x + w - pad - clockW, y + pad - 8, clockW, clockH, clock, clockInk, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(x + pad, y + pad + 24, headW - clockW - 16, 20, sub, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		tilesY = y + pad + 56;
		tilesH = y + h - pad - tilesY;
	}
	const int count = static_cast<int>(n.pick.options.size()), gap = 12;
	if (count <= 0 || tilesW <= 0 || tilesH <= 20) {
		p.raw = raw;
		return;
	}
	const int tileW = (tilesW - gap * (count - 1)) / count;
	for (int i = 0; i < count; i++) {
		const TourneyMap &m = n.pick.options[static_cast<size_t>(i)];
		const int tx = tilesX + i * (tileW + gap);
		const bool hot = open && p.Hover(tx, tilesY, tileW, tilesH, mx, my);
		p.Fill(tx, tilesY, tileW, tilesH, hot ? pal::selected : pal::raised);
		p.Frame(tx, tilesY, tileW, tilesH, hot ? pal::red : open ? pal::edgeHot : pal::edge);
		const int pic = std::max(24, std::min(tilesH - 16, tileW * 45 / 100));
		PaintMapPicture(p, m, tx + 8, tilesY + (tilesH - pic) / 2, pic);
		const int textX = tx + 8 + pic + 12, textW = tx + tileW - 10 - textX;
		std::string kind = Tr("Random map");
		if (m.kind == TourneyMapKind::Game) kind = Tr("Scenario of the game");
		else if (m.kind == TourneyMapKind::Upload) kind = Tr("Scenario of the cup");
		const std::string warning = CupMapWarning(m);
		TourneyMap mapOnly = m;
		mapOnly.size.clear();
		if (textW > 40) {
			UseFace(p, BrandFace::UiStrong, 16);
			p.Text(textX, tilesY + tilesH / 2 - 24, textW, 22, MapText(mapOnly), hot ? pal::text : pal::body, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(3);
			const std::string size = !m.Scenario() && !m.size.empty() ? " \xC2\xB7 " + LeagueMapText(m.size) : std::string();
			p.Text(textX, tilesY + tilesH / 2 + 2, textW, 20, warning.empty() ? kind + size : warning, warning.empty() ? pal::caption : pal::warn,
				DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (open) p.AddHit(tx, tilesY, tileW, tilesH, kTourney, ArgOf(kSubPick, i));
	}
	p.raw = raw;
}

std::string NoShowClaimText(const TourneyNext &n)
{
	return TrFmt(n.Teams() ? "A player of %s did not come to the room in time: your team can claim the win." : "%s did not come to the room in time: you can claim the win.",
		{n.opponent.name});
}

bool PaintMatchHeader(Painter &p, const State &st, const HeaderUi &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const auto mine = LeagueTourneyMine();
	const TourneyNext &next = mine->mine.next;
	const int right = PaintHeaderActions(p, l, {{Tr(u.slug.empty() ? "Tournaments" : "Back to the bracket"), kBack, true, true}}, mx, my);
	const int64_t left = next.present && next.status == TourneyMatchStatus::Ready ? next.deadlineAt - NowMs() : 0;
	int titleRight = right;
	if (left > 0 && next.pace == TourneyPace::Rounds) {
		// A deadline days away: its day and hour, and the time left beside them.
		const int bw = 400, bx = l.w - l.margin - bw, by = l.margin + kHeaderButtonH + 10;
		const COLORREF ink = left < 24 * 3600000LL ? pal::warn : pal::text;
		PaintCaps(p, bx, by, bw, 18, "Round deadline", left < 24 * 3600000LL ? pal::warn : pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		const int clockH = l.roomy ? 56 : 46;
		UseDisplay(p, clockH);
		p.raw = true;
		const std::string when = ShortWhenText(next.deadlineAt);
		const int whenW = std::min(bw - 120, p.CapsWidth(when, 0));
		p.TextCaps(bx, by + 18, bw, clockH, when, ink, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
		p.Font(3);
		p.Text(bx, by + 18 + clockH - 26, bw - whenW - 14, 20, TrFmt("%s left", {LongWaitText(left)}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		titleRight = std::min(right, bx - 20);
	}
	else if (left > 0) {
		const int bw = 300, bx = l.w - l.margin - bw, by = l.margin + kHeaderButtonH + 10;
		PaintCaps(p, bx, by, bw, 18, "Time to enter", pal::warn, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		const int clockH = l.roomy ? 56 : 46;
		UseDisplay(p, clockH);
		p.raw = true;
		const std::string clock = TourneyClock(left);
		const int clockW = p.CapsWidth(clock, 0);
		p.Text(bx, by + 18, bw, clockH, clock, pal::warn, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		// The hour goes beside the clock, not under it, so the block ends above the content.
		p.Font(3);
		p.Text(bx, by + 18 + clockH - 26, bw - clockW - 14, 20, TrFmt("until %s", {HourText(next.deadlineAt)}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		titleRight = std::min(right, bx - 20);
	}
	PaintScreenTitle(p, l, "Your next match", {}, titleRight, mx, my);
	if (!next.present) return true;
	std::string info = next.name + "   \xC2\xB7   " + BracketStageText(next.bracket, next.stage, next.round) + "   \xC2\xB7   " + TrFmt("Best of %s", {std::to_string(next.bestOf)});
	if (next.bestOf > 1) info += "   \xC2\xB7   " + TrFmt("Game %s, you %s", {std::to_string(next.game), std::to_string(next.scoreMe) + "\xE2\x80\x93" + std::to_string(next.scoreThem)});
	p.Font(0);
	p.raw = true;
	p.Text(l.margin, l.infoY, titleRight - l.margin, l.infoH, info, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	return true;
}

std::vector<std::pair<int, std::string>> MatchCivChoices()
{
	// The room's order of the game's eight (race 8 is the random one), each in the words of the mod.
	static const struct {
		int race;
		const char *name;
	} kBase[] = {{8, "Random"}, {1, "Republican Rome"}, {4, "Imperial Rome"}, {0, "Gaul"}, {3, "Iberia"}, {2, "Carthage"}, {7, "Germany"}, {6, "Egypt"}, {5, "Britain"}};
	const std::vector<Civ> civs = CivsList();
	std::vector<std::pair<int, std::string>> out;
	for (const auto &b : kBase) {
		bool replaced = false;
		for (const Civ &c : civs) replaced = replaced || (c.replacesBase && c.base == b.race);
		if (!replaced) out.push_back({b.race, Tr(b.name)});
	}
	for (const Civ &c : civs) out.push_back({c.index, CivDisplayName(c)});
	return out;
}

int MatchCiv(Ui &u)
{
	if (u.civRace == -2) u.civRace = GameRoomLoadSetting("TourneyCiv", -1);
	return u.civRace;
}

int MatchColor(Ui &u)
{
	if (u.colorPick == -2) {
		u.colorPick = GameRoomLoadSetting("TourneyColor", -1);
		u.colorRgb = static_cast<uint32_t>(GameRoomLoadSetting("TourneyColorRgb", 0)) & 0xffffff;
		if (u.colorPick != kOwnColor && (u.colorPick < -1 || u.colorPick >= 12)) u.colorPick = -1;
	}
	return u.colorPick;
}

COLORREF MatchColorRef(Ui &u)
{
	const int pick = MatchColor(u);
	if (pick == kOwnColor) return colorwheel::Ref(u.colorRgb);
	if (pick < 0 || pick >= 12) return CLR_INVALID;
	uint32_t palette[12] = {};
	GameRoomPalette(palette);
	return RGB((palette[pick] >> 16) & 255, (palette[pick] >> 8) & 255, palette[pick] & 255);
}

void PaintMatchColorMenu(Painter &p, const State &st)
{
	Ui &u = g_tourney;
	const int mx = st.mouseX, my = st.mouseY, pad = 12;
	const RECT a = g_colorAnchor;
	p.hits->clear();
	p.AddHit(0, 0, st.viewW, st.viewH, kTourney, ArgOf(kSubColorClose, 0));
	if (u.colorWheel) {
		// The player's own colour: the wheel and what it looks like, then back to the twelve or take it.
		const int btnH = 34, w = pad * 2 + colorwheel::kWidth, h = pad * 2 + colorwheel::kWheel + 10 + 28 + 10 + btnH;
		const int x = std::min<int>(a.left, st.viewW - 16 - w);
		int y = a.bottom + 4;
		if (y + h > st.viewH - 8) y = a.top - 4 - h;
		p.Fill(x + 4, y + 4, w, h, pal::shadow);
		p.Fill(x, y, w, h, pal::panel);
		p.Frame(x, y, w, h, pal::lineStrong);
		p.AddHit(x, y, w, h, kNone, 0);
		colorwheel::Paint(p, x + pad, y + pad, st.colorTry, pal::panel, kGColorWheel, kGColorValue, 0);
		const int sy = y + pad + colorwheel::kWheel + 10;
		p.Fill(x + pad, sy, colorwheel::kWidth, 28, colorwheel::Ref(st.colorTry));
		p.Frame(x + pad, sy, colorwheel::kWidth, 28, pal::edge);
		const int by = sy + 38, half = (colorwheel::kWidth - 8) / 2;
		p.Button(x + pad, by, half, btnH, "Back", kTourney, ArgOf(kSubColorOpen, 0), mx, my, true, false, 3);
		p.Button(x + pad + half + 8, by, colorwheel::kWidth - half - 8, btnH, "Use", kTourney, ArgOf(kSubColorUse, 0), mx, my, true, true, 3);
		return;
	}
	// The game's twelve, as the room's menu shows them; under them the room's own and the wheel.
	const int cell = 34, gap = 6, rowH = 32;
	const int w = pad * 2 + 6 * cell + 5 * gap, h = pad * 2 + 2 * cell + gap + 10 + 2 * rowH + gap;
	const int x = std::min<int>(a.left, st.viewW - 16 - w);
	int y = a.bottom + 4;
	if (y + h > st.viewH - 8) y = a.top - 4 - h;
	p.Fill(x + 4, y + 4, w, h, pal::shadow);
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, pal::lineStrong);
	p.AddHit(x, y, w, h, kNone, 0);
	uint32_t palette[12] = {};
	GameRoomPalette(palette);
	const int pick = MatchColor(u);
	for (int c = 0; c < 12; c++) {
		const int cx = x + pad + (c % 6) * (cell + gap), cy = y + pad + (c / 6) * (cell + gap);
		const bool hot = p.Hover(cx, cy, cell, cell, mx, my);
		p.Fill(cx, cy, cell, cell, RGB((palette[c] >> 16) & 255, (palette[c] >> 8) & 255, palette[c] & 255));
		p.Frame(cx, cy, cell, cell, c == pick ? pal::text : hot ? pal::edgeHot : pal::bg);
		if (c == pick) p.Frame(cx + 1, cy + 1, cell - 2, cell - 2, pal::text);
		p.AddHit(cx, cy, cell, cell, kTourney, ArgOf(kSubColorPick, c));
	}
	const int ly = y + pad + 2 * cell + gap + 10, lw = w - 2 * pad;
	p.Button(x + pad, ly, lw, rowH, "The room's own", kTourney, ArgOf(kSubColorPick, -1), mx, my, pick != -1, false, 3);
	p.Button(x + pad, ly + rowH + gap, lw, rowH, "Own colour...", kTourney, ArgOf(kSubColorOpen, 1), mx, my, true, pick == kOwnColor, 3);
}

namespace {

// What is checked before the room: the content, the map, Steam and the check-in.
void PaintReadyCard(Painter &p, const State &st, const TourneyNext &n, const TourneyMineView &mine, const TourneyMap &map, int m, int ry, int cw, int rh, int pad)
{
	PaintGlassCard(p, m, ry, cw, rh);
	PaintCaps(p, m + pad, ry + 20, cw - 2 * pad, 20, "Ready to enter", pal::caption);
	const bool sameContent = n.contentKey.empty() || n.contentKey == SteamOwnGameplayKey();
	const CupMapState mapState = LeagueCupMap(map);
	const TourneyMyEntry *entry = MyEntry(mine, n.tournamentId);
	struct Check {
		bool ok;
		std::string text;
	} checks[] = {
		{sameContent, sameContent ? TrFmt("%s active, the same content as the room", {n.contentName.empty() ? Tr("The mod's rules") : n.contentName})
								  : TrFmt("This tournament needs %s: switch your content profile", {n.contentName.empty() ? Tr("other content") : n.contentName})},
		{mapState.status == CupMapStatus::Ready, MapCheckText(map, mapState)},
		{st.ready, st.ready ? Tr("Steam is ready") : Tr("Steam is not available. Open Steam and try again.")},
		{!entry || entry->checkedIn, Tr(!entry || entry->checkedIn ? "Check-in done" : "Check-in missing")},
	};
	int ly = ry + 54;
	for (const Check &c : checks) {
		if (ly + 26 > ry + rh) break;
		PaintCheck(p, m + pad, ly + 2, 20, c.ok);
		p.Font(0);
		p.raw = true;
		p.Text(m + pad + 32, ly, cw - 2 * pad - 32, 24, c.text, c.ok ? pal::body : pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ly += 32;
	}
}

void PaintMatchRules(Painter &p, const TourneyNext &n, const TourneyPageView &page, const TourneyMap &map, bool teams, bool byRounds, int x2, int ry, int cw, int rh,
	int pad)
{
	PaintGlassCard(p, x2, ry, cw, rh);
	PaintCaps(p, x2 + pad, ry + 20, cw - 2 * pad, 20, "Rules of this match", pal::caption);
	// Where the winner goes is told for a single elimination; the other formats' paths are on the cup's page.
	std::string after;
	const bool single = n.format == TourneyFormat::Single && n.bracket == TourneyBracket::Winners;
	if (single && n.round >= n.rounds) after = Tr("The winner is the champion");
	else if (single && page.loaded) {
		const TourneyMatch *following = nullptr, *mineM = MatchById(page.detail, n.match);
		if (mineM) following = TourneyMatchAt(page.detail, mineM->round + 1, mineM->position / 2);
		const TourneyCard *other = following ? (mineM->position % 2 ? &following->a : &following->b) : nullptr;
		after = other && other->Present() ? TrFmt("If you win, %s against %s", {StageText(following->stage, following->round), other->name})
										  : TrFmt("If you win, the %s", {StageText(TourneyStageOf(n.round + 1, n.rounds), n.round + 1)});
	}
	std::vector<std::pair<std::string, std::string>> rules;
	// With the by-rounds pace the deadline and the no-show come first: the card may not have room for every row.
	if (byRounds && n.deadlineAt) rules.push_back({Tr("Deadline"), UntilText(n.deadlineAt)});
	if (byRounds && n.noshowFrom) rules.push_back({Tr("No-show"), FromText(n.noshowFrom)});
	if (teams && (n.team.side == "a" || n.team.side == "b")) rules.push_back({Tr("Teams"), TrFmt("Set by the league: yours is team %s", {n.team.side == "a" ? "1" : "2"})});
	rules.push_back({Tr("Settings"), PresetText(n.settings)});
	rules.push_back({Tr("Map"), map.Known() ? MapText(map) : Tr(n.pick.present ? "To be chosen" : "Chosen in the room")});
	rules.push_back({Tr("Civilizations"), Tr(n.settings.hideCivs ? "Hidden until the game starts" : "Visible in the room")});
	rules.push_back({Tr("Spectators"), TourneySpecDelay(n.settings) ? TrFmt("With a %s s delay", {std::to_string(TourneySpecDelay(n.settings))}) : Tr("No spectators")});
	rules.push_back({Tr("After"), after});
	// The map's view and the diplomacy, as the preset and the cup's switches leave them: last, the first rows to go when the card is short.
	const TourneyRoomRules room = TourneyRulesOf(n.settings);
	rules.push_back({Tr("Map view"), Tr(room.explored ? (room.fog ? "Explored, with fog of war" : "Explored, no fog of war") : room.fog ? "Unexplored, with fog of war" : "Unexplored, no fog of war")});
	rules.push_back({Tr("Diplomacy"), Tr(room.lockedDiplomacy ? "Locked" : "Free")});
	int ly = ry + 54;
	for (const auto &r : rules) {
		if (ly + 26 > ry + rh || r.second.empty()) continue;
		p.Font(0);
		p.raw = true;
		p.Text(x2 + pad, ly, 140, 24, r.first, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Text(x2 + pad + 150, ly, cw - 2 * pad - 150, 24, r.second, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ly += 30;
	}
}

// What to do if something fails, with the claim of the win, the dispute and the withdrawal.
void PaintTroubleCard(Painter &p, const TourneyNext &n, int x3, int ry, int cw, int rh, int pad, int mx, int my)
{
	PaintGlassCard(p, x3, ry, cw, rh);
	PaintCaps(p, x3 + pad, ry + 20, cw - 2 * pad, 20, "If something fails", pal::caption);
	p.Font(0);
	const char *const lines[] = {"You drop: you have 5 min to return to the game.", "It ends in the first 3 minutes with no losses: it is played again.",
		"The mod sends the result. If the reports differ, the organisers decide."};
	int ly = ry + 54;
	for (const char *text : lines) {
		if (ly + 26 > ry + rh - 50) break;
		p.Text(x3 + pad, ly, cw - 2 * pad, 24, text, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ly += 28;
	}
	const int fy = ry + rh - 22 - 36, gw = (cw - 2 * pad - 20) / 3;
	if (n.canClaimNoShow) p.Button(x3 + pad, fy, gw, 36, "Claim the win", kTourney, ArgOf(kSubNoShow, n.match), mx, my, !LeagueTourneyBusy(TourneyOp::NoShow, n.match), true);
	else PaintGhostButton(p, x3 + pad, fy, gw, 36, "Claim the win", kNone, false, false, mx, my);
	p.Button(x3 + pad + gw + 10, fy, gw, 36, "Dispute", kTourney, ArgOf(kSubDispute, n.match), mx, my);
	p.Button(x3 + pad + 2 * (gw + 10), fy, gw, 36, "Withdraw", kTourney, ArgOf(kSubWithdraw, n.tournamentId), mx, my);
}

}

void PaintMatchScreen(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	const auto mineView = LeagueTourneyMine();
	const TourneyMineView &mine = *mineView;
	const TourneyNext &n = mine.mine.next;
	if (!n.present) {
		PaintGlassCard(p, m, l.top, inner, 200);
		p.Font(0);
		p.Text(m + 32, l.top + 40, inner - 64, 30, mine.loaded ? "You have no match to play now." : "Reading your tournaments...", pal::textDim);
		p.Button(m + 32, l.top + 100, 320, 48, "Back to the tournaments", kBack, 0, mx, my);
		return;
	}
	const auto pageView = LeagueTourneyPage(n.slug, 30000);
	const TourneyPageView &page = *pageView;
	const TourneyEntrant *meE = page.loaded ? TourneyEntrantBySteamId(page.detail, SteamOwnId()) : nullptr;
	const TourneyEntrant *themE = page.loaded && n.hasOpponent ? TourneyEntrantBySteamId(page.detail, n.opponent.steamId) : nullptr;
	const bool teams = n.Teams();
	// A team's players: as the next match lists them, else as the cup's page does, else the one player known.
	std::vector<TourneyMember> ours = n.team.members, theirs = n.opponent.members;
	if (ours.empty() && meE) ours = meE->members;
	if (ours.empty()) ours.push_back({SteamOwnId(), st.player, -1});
	if (theirs.empty() && themE) theirs = themE->members;
	if (theirs.empty() && n.hasOpponent) theirs.push_back({n.opponent.steamId, n.opponent.name, -1});
	PatronsWatch(TourneyMatchPlayers(n, SteamOwnId()));
	PaintRing(p, l.w / 2, l.top + 300, 380, pal::hairline);

	const int cardH = l.roomy ? 430 : 360, cy = l.top - (l.roomy ? 10 : 6), centerW = 420, sideW = (inner - centerW) / 2;
	PaintGlassCard(p, m, cy, inner, cardH);
	p.Fill(m + sideW, cy + 1, 1, cardH - 2, pal::hairline);
	p.Fill(m + sideW + centerW, cy + 1, 1, cardH - 2, pal::hairline);
	const bool live = n.status == TourneyMatchStatus::Live;
	if (teams) {
		PaintTeamColumn(p, st, n, m + sideW / 2, cy, sideW - 40, cardH, n.team.name.empty() && meE ? meE->name : n.team.name, n.me.seed, ours, true,
			meE ? PreviousText(page.detail, meE->entryId, n.round) : std::string(), live);
		if (!live && n.status == TourneyMatchStatus::Ready) PaintCivMenu(p, st, m + sideW / 2 - 140, cy + cardH - 84, 280);
		if (n.hasOpponent) {
			PaintTeamColumn(p, st, n, m + sideW + centerW + sideW / 2, cy, sideW - 40, cardH, n.opponent.name, n.opponent.seed, theirs, false,
				themE ? PreviousText(page.detail, themE->entryId, n.round) : std::string(), live);
		}
		else {
			p.Font(0);
			p.Text(m + sideW + centerW, cy, sideW, cardH, "The other team is still to be decided.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	else {
		const bool civ = !live && n.status == TourneyMatchStatus::Ready;
		const int under = PaintPlayerColumn(p, m + sideW / 2, cy, cardH, SteamOwnId(), st.player, true, n.me.seed, meE ? PreviousText(page.detail, meE->entryId, n.round) : std::string(),
			live ? std::string() : Tr(n.me.inRoom ? "In the room" : "You're not in the room yet"), n.me.inRoom, civ ? 80 : 0);
		if (civ) PaintCivMenu(p, st, m + sideW / 2 - 140, under + 6, 280);
		if (n.hasOpponent) {
			PaintPlayerColumn(p, m + sideW + centerW + sideW / 2, cy, cardH, n.opponent.steamId, n.opponent.name, false, n.opponent.seed,
				themE ? PreviousText(page.detail, themE->entryId, n.round) : std::string(), live ? std::string() : Tr(n.opponent.inRoom ? "In the room" : "Not in the room yet"),
				n.opponent.inRoom);
		}
		else {
			p.Font(0);
			p.Text(m + sideW + centerW, cy, sideW, cardH, "Your opponent is still to be decided.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}
	// The middle: the stage, VS and the map of the game; in a series, its games.
	const TourneyMap map = TourneyGameMap(n);
	const int64_t now = NowMs();
	const bool byRounds = n.pace == TourneyPace::Rounds;
	PaintMatchCenter(p, n, page, map, m + sideW, cy, centerW, cardH, l.roomy);

	// The big button and what it will do.
	const RoomFlow &f = u.room;
	const bool flowing = f.phase != RoomFlow::kIdle && f.match == n.match;
	const bool inRoom = st.room.id && TourneyMatchOfTag(st.room.info.leagueTag) == n.match;
	std::string label = "Ready", line;
	bool enabled = n.status == TourneyMatchStatus::Ready && !inRoom && !flowing;
	int action = kSubGo;
	if (inRoom) {
		// In the room: ready (the mod gets the player ready on entering; pressed again after taking it back), then the start.
		const bool ready = LocalReady(st.game);
		const int left = GameRoomCountdownLeftMs();
		action = kSubReady;
		enabled = !ready && !live;
		label = ready ? (left > 0 ? TrFmt("Starts in %s", {std::to_string((left + 999) / 1000)}) : Tr("Waiting for your opponent...")) : "Ready";
		if (live) label = "Game in progress";
	}
	else if (n.pick.present) {
		// The room opens once the next game's map is chosen: until then the league has the claim wait.
		label = "Waiting for the map...";
		enabled = false;
	}
	else if (flowing) {
		label = f.phase == RoomFlow::kClaiming ? "Asking the league..." : f.phase == RoomFlow::kCreating ? "Opening the room..." : "Entering the room...";
		if (f.phase == RoomFlow::kWaiting) {
			label = "Waiting for the room...";
			line = TrFmt("%s is opening the room...", {f.waitingFor.empty() ? Tr("Your opponent") : f.waitingFor});
		}
	}
	else if (live) label = "Game in progress";
	else if (n.status == TourneyMatchStatus::Pending) label = "Waiting for your opponent";
	else if (n.status == TourneyMatchStatus::Disputed) label = "Under review";
	const std::string rival = n.opponent.name.empty() ? Tr("your opponent") : n.opponent.name;
	// Side A's captain opens the room of a match of teams, with the teams set; the other three enter it.
	const bool sideA = n.team.side == "a";
	const uint64_t captainA = sideA ? n.team.steamId : n.opponent.steamId;
	std::string opener;
	if (teams && captainA && captainA != SteamOwnId()) {
		const TourneyMember *who = TourneyMemberOf(sideA ? ours : theirs, captainA);
		opener = who && !who->name.empty() ? TrFmt("%s opens the room with the teams set; you enter it then.", {who->name})
										   : TrFmt("The captain of %s opens the room with the teams set; you enter it then.", {sideA ? n.team.name : n.opponent.name});
	}
	if (line.empty()) {
		if (n.status == TourneyMatchStatus::Pending && teams) line = Tr("The other team comes from a match still being played. We tell you when it is over.");
		else if (n.status == TourneyMatchStatus::Pending) line = Tr("Your opponent comes from a match still being played. We tell you when it is over.");
		else if (n.status == TourneyMatchStatus::Disputed) line = Tr("The organisers are looking at the reports of the game.");
		else if (live) {
			// The button says the game is under way: nothing to add under it.
		}
		else if (n.pick.present && n.MyPick()) line = TrFmt("You lost the last game: choose the map of game %s below. If time runs out, it is drawn.", {std::to_string(n.game)});
		else if (n.pick.present) line = TrFmt("%s is choosing the map of game %s among the series' maps.", {n.opponent.name.empty() ? Tr("Your opponent") : n.opponent.name, std::to_string(n.game)});
		else if (inRoom) {
			// The button says where it stands.
		}
		else if (n.lobby && n.roomClaimBy == n.opponent.steamId && n.opponent.steamId)
			line = TrFmt("%s opened the room. You enter as a player; the map and the rules are the tournament's.", {n.opponent.name});
		else if (n.lobby && teams) line = Tr("The room is open. You enter in your team; the teams, the map and the rules are the tournament's.");
		else if (n.lobby) line = Tr("The room is open. You enter as a player; the map and the rules are the tournament's.");
		else if (!opener.empty() && !byRounds) line = opener;
		else if (byRounds && n.noshowFrom > now) {
			// The deadline is over the title and the no-show in the rules: nothing to repeat here.
		}
		else if (byRounds) line = TrFmt("Last day of the round: if %s does not come to the room, you can claim the win.", {rival});
		else if (teams) line = Tr("Nobody has opened the room yet: you will open it, with the teams, the map and the rules of the tournament.");
		else line = Tr("Nobody has opened the room yet: you will open it, with the map and the rules of the tournament.");
	}
	// Before the room: this computer's copy of the game's scenario, when it is not the cup's.
	const std::string mapWarning = CupMapWarning(map);
	COLORREF lineInk = pal::textDim;
	if (!mapWarning.empty() && !inRoom && !live && n.status != TourneyMatchStatus::Disputed) {
		line = mapWarning;
		lineInk = pal::warn;
	}
	// With the by-rounds pace the players agree on a time: the rival's Steam chat goes beside the button.
	const int bw = 460, bh = l.roomy ? 72 : 60, by = cy + cardH + (l.roomy ? 24 : 16);
	const bool chat = byRounds && n.hasOpponent && n.opponent.steamId;
	const int chatW = chat ? ChatButtonWidth(p) : 0, groupW = bw + (chat ? 16 + chatW : 0), bx = (l.w - groupW) / 2;
	p.Button(bx, by, bw, bh, label, kTourney, ArgOf(action, n.match), mx, my, enabled, true, 2);
	if (chat) PaintSecondButton(p, bx + bw + 16, by, chatW, bh, Tr("Write on Steam"), ArgOf(kSubSteamChat, 0), true, mx, my);
	// The room is there under the page, for when something needs it.
	if (inRoom && !live) PaintSecondButton(p, bx - 16 - 200, by, 200, bh, Tr("Show the room"), ArgOf(kSubShowRoom, 0), true, mx, my);
	p.Font(0);
	p.raw = true;
	p.Text(m, by + bh + 8, inner, 24, line, n.pick.present && n.MyPick() ? pal::warn : lineInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;

	// While the next game's map is being chosen, the choice takes the place of the three cards.
	const int ry = by + bh + 44, rh = bottom - ry, cw = (inner - 2 * kCardGap) / 3, pad = 26;
	if (rh < 120) return;
	if (n.pick.present) {
		PaintPickPanel(p, st, n, m, ry, inner, rh);
		return;
	}
	// Three cards: what is checked, the rules and what to do if something fails.
	PaintReadyCard(p, st, n, mine, map, m, ry, cw, rh, pad);
	const int x2 = m + cw + kCardGap;
	PaintMatchRules(p, n, page, map, teams, byRounds, x2, ry, cw, rh, pad);
	const int x3 = x2 + cw + kCardGap;
	PaintTroubleCard(p, n, x3, ry, cw, rh, pad, mx, my);
}

bool PaintChampionHeader(Painter &p, const State &st, const HeaderUi &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const auto page = LeagueTourneyPage(u.championSlug, 60000);
	const int right = PaintHeaderActions(p, l, {{Tr("Back to the bracket"), kBack, true, true}}, mx, my);
	if (!page->loaded) return true;
	const TourneyDetail &d = page->detail;
	const int y = l.margin + kHeaderButtonH + (l.roomy ? 22 : 12);
	p.raw = true;
	PaintCaps(p, l.margin, y, right - l.margin - 420, 22, d.name + " \xC2\xB7 " + Tr("Final result"), pal::redText);
	const int64_t end = TourneyFinishedAt(d);
	p.Font(0);
	// A cup paced by rounds lasts days: its first and last day say more than the hours.
	const std::string when = end - d.startsAt >= 86400000LL ? TrFmt("from %s to %s", {DayText(d.startsAt), DayText(end)})
		: DayText(d.startsAt) + " \xC2\xB7 " + (end ? TrFmt("from %s to %s", {HourText(d.startsAt), HourText(end)}) : TrFmt("from %s", {HourText(d.startsAt)}));
	p.Text(l.w - l.margin - 420, y, 420, 22, when, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	return true;
}

namespace {

// The champion beside the crown: their name, a team's players, the final that crowned them; or why the cup has none.
void PaintChampionName(Painter &p, const ScreenLayout &l, const TourneyDetail &d, const TourneyEntrant *champ, int tx, int top, int tw, int blockH,
	std::vector<uint64_t> &ids)
{
	if (champ) {
		const bool teams = d.Teams();
		int nameSize = std::min(teams ? 150 : 210, blockH * (teams ? 30 : 45) / 100);
		UseDisplay(p, nameSize);
		p.raw = true;
		while (nameSize > 60 && p.CapsWidth(champ->name, 1) > tw) {
			nameSize -= 10;
			UseDisplay(p, nameSize);
		}
		p.TextCaps(tx, top + 56, tw, nameSize, champ->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
		p.raw = false;
		ids.push_back(champ->steamId);
		int ty = top + 56 + nameSize + 10;
		if (teams) {
			// Each player of the team with their picture, side by side.
			const int disc = l.roomy ? 60 : 48;
			int px2 = tx;
			for (const TourneyMember &pl : TourneyPlayersOf(*champ)) {
				if (px2 + disc + 80 > tx + tw) break;
				PaintPlayerDisc(p, px2, ty, disc, pl.steamId, pl.name, kGold);
				UseFace(p, BrandFace::UiStrong, l.roomy ? 24 : 20);
				p.raw = true;
				const std::string who = pl.name.empty() ? Tr("A player") : pl.name;
				const int nw = std::min(tx + tw - (px2 + disc + 16), std::min(300, p.TextWidth(who) + 4));
				p.Text(px2 + disc + 16, ty, nw, disc, who, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				p.raw = false;
				if (pl.steamId) ids.push_back(pl.steamId);
				px2 += disc + 16 + nw + 40;
			}
			ty += disc + 16;
		}
		UseFace(p, BrandFace::Body, 26);
		p.raw = true;
		p.Text(tx, ty, tw, 34, TrFmt(teams ? "win %s" : "wins %s", {d.name}), pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ty += 42;
		// The final that crowned them (the grand final and its reset in a double elimination), or their points in a Swiss tournament.
		const std::string line = ChampionLine(d, *champ);
		if (!line.empty()) {
			UseFace(p, BrandFace::Body, 18);
			p.Text(tx, ty, tw, 26, line, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			ty += 40;
		}
		p.raw = false;
		if (d.official && ty + 50 < top + blockH) {
			p.Fill(tx, ty, std::min(tw, 640), 48, pal::raised);
			p.Fill(tx, ty, 3, 48, pal::red);
			p.Font(0);
			p.Text(tx + 18, ty, std::min(tw, 640) - 26, 48, "The phalerae are already in the players' showcase and next to their names in the game.", pal::body,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	else {
		p.Font(0);
		p.Text(tx, top + 70, tw, 30, d.status == TourneyStatus::Running ? "The tournament is still being played." : "This tournament ended without a champion.", pal::textDim);
	}
}

// The path to the title, match by match.
void PaintTitlePath(Painter &p, const TourneyDetail &d, const TourneyEntrant *champ, int pathX, int top, int pathW, int blockH)
{
	PaintGlassCard(p, pathX, top, pathW, blockH);
	PaintCaps(p, pathX + 28, top + 24, pathW - 56, 20, "Path to the title", pal::caption);
	if (champ) {
		std::vector<const TourneyMatch *> path = TourneyPathOf(d, champ->entryId);
		// A champion of a double elimination who came back through the losers' bracket has many matches: the earliest give way, counted.
		const int room = blockH - 92, fit = std::max(1, room / 52);
		const int earlier = std::max(0, static_cast<int>(path.size()) - fit);
		if (earlier) path.erase(path.begin(), path.begin() + earlier);
		const int stepH = std::max(52, std::min(84, room / std::max<int>(1, static_cast<int>(path.size()))));
		int sy = top + 66;
		if (earlier) {
			p.Font(3);
			p.raw = true;
			p.Text(pathX + 62, top + 22, pathW - 62 - 28, 24, earlier == 1 ? Tr("+ 1 match before") : TrFmt("+ %s matches before", {std::to_string(earlier)}), pal::caption,
				DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.raw = false;
		}
		p.Fill(pathX + 35, sy + 12, 2, std::max(0, stepH * (static_cast<int>(path.size()) - 1)), pal::lineStrong);
		for (const TourneyMatch *mm : path) {
			const bool last = mm == path.back();
			const bool bye = mm->status == TourneyMatchStatus::Bye || !(mm->a.Present() && mm->b.Present());
			// A champion can lose on the way: a round of a Swiss tournament, or the match that sent them to the losers' bracket.
			const bool lost = !bye && TourneyMatchOver(mm->status) && mm->winner != champ->entryId;
			p.Shape(pathX + 28, sy + 4, 16, 16, InUnitEllipse, last ? kGold : lost ? pal::red : RGB(0x7a, 0x75, 0x6e));
			if (!last) p.Shape(pathX + 30, sy + 6, 12, 12, InUnitEllipse, pal::panel);
			const int sx = pathX + 62, sw = pathW - 62 - 28;
			std::string stage = MatchStageText(d, *mm);
			if (mm->bestOf > 1) stage += " \xC2\xB7 " + TrFmt("Best of %s", {std::to_string(mm->bestOf)});
			// How long the series took and its first map, when the league kept them.
			if (!bye && mm->startedAt > 0 && mm->finishedAt > mm->startedAt) stage += " \xC2\xB7 " + DurationText(mm->finishedAt - mm->startedAt);
			if (!bye && !mm->games.empty() && mm->games[0].map.Known()) stage += " \xC2\xB7 " + MapText(mm->games[0].map);
			PaintCaps(p, sx, sy, sw, 20, stage, last ? kGold : lost ? pal::redText : pal::caption);
			const bool sideB = mm->b.entryId == champ->entryId;
			const TourneyCard &other = sideB ? mm->a : mm->b;
			UseDisplay(p, 30);
			p.raw = true;
			std::string score = bye ? std::string("-") : std::to_string(sideB ? mm->scoreB : mm->scoreA) + "\xE2\x80\x93" + std::to_string(sideB ? mm->scoreA : mm->scoreB);
			if (mm->status == TourneyMatchStatus::Walkover && !bye) score = lost ? "0" : "W";
			p.Text(sx, sy + 20, 70, 34, score, lost ? pal::redText : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			UseFace(p, BrandFace::Body, 17);
			const std::string against = bye ? Tr(TourneyIsSwiss(d) ? "Bye: a point without playing" : "Bye: straight to the next round")
				: other.seed ? TrFmt("against %s (%s)", {other.name, std::to_string(other.seed)})
							 : TrFmt("against %s", {other.name});
			p.Text(sx + 74, sy + 20, sw - 74, 34, against, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
			sy += stepH;
		}
	}
}

void PaintPodium(Painter &p, const TourneyDetail &d, int m, int ry, int cw, int rh, int pad, std::vector<uint64_t> &ids)
{
	PaintGlassCard(p, m, ry, cw, rh);
	PaintCaps(p, m + pad, ry + 24, cw - 2 * pad, 20, "Podium", pal::caption);
	const int rowH = std::min(82, (rh - 70) / 3);
	struct Step {
		int place;
		LeagueMetal metal;
		COLORREF ink;
		const char *label;
	} steps[] = {{1, LeagueMetal::Gold, kGold, "1st"}, {2, LeagueMetal::Silver, kSilver, "2nd"}, {3, LeagueMetal::Bronze, kBronze, "3rd"}};
	int py = ry + 56;
	for (const Step &s : steps) {
		const std::vector<const TourneyEntrant *> who = TourneyPlaced(d, s.place);
		// A bracket without semifinals has no third place.
		if (s.place == 3 && who.empty() && d.rounds < 2) break;
		UseDisplay(p, 30);
		p.Text(m + pad, py, 50, rowH, s.label, s.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (d.official) DrawLeaguePhalera(p, m + pad + 56, py + (rowH - 40) / 2, 40, s.metal);
		std::string names;
		for (const TourneyEntrant *e : who) {
			names += (names.empty() ? "" : " " + Tr("and") + " ") + e->name;
			for (const TourneyMember &pl : TourneyPlayersOf(*e)) ids.push_back(pl.steamId);
		}
		UseFace(p, BrandFace::UiStrong, 20);
		p.raw = true;
		p.Text(m + pad + 110, py, cw - 2 * pad - 110, rowH / 2 + 8, names.empty() ? std::string("-") : names, pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		// Under the name: the Swiss points that placed them, else their seed; a shared third place says where it was lost.
		std::string under = who.size() == 1 && who[0]->seed ? TrFmt("Seed %s", {std::to_string(who[0]->seed)}) : s.place == 3 && !TourneyIsDouble(d) ? Tr("Semifinals") : std::string();
		if (const TourneyStanding *standing = who.size() == 1 && TourneyIsSwiss(d) ? TourneyStandingOf(d, who[0]->entryId) : nullptr) {
			under = PointsText(standing->points) + " \xC2\xB7 " + Tr("Buchholz") + " " + std::to_string(standing->buchholz);
		}
		// A team's players come first under its name.
		if (who.size() == 1 && d.Teams()) {
			const std::string players = TeamPlayersLine(who[0]->name, TourneyPlayersOf(*who[0]));
			if (!players.empty()) under = under.empty() ? players : players + " \xC2\xB7 " + under;
		}
		p.Text(m + pad + 110, py + rowH / 2 + 8, cw - 2 * pad - 110, 20, under, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		if (s.place < 3) p.Fill(m + pad, py + rowH - 1, cw - 2 * pad, 1, pal::hairline);
		py += rowH;
	}
}

void PaintCupNumbers(Painter &p, const TourneyDetail &d, int64_t end, int x2, int ry, int cw, int rh, int pad)
{
	PaintGlassCard(p, x2, ry, cw, rh);
	PaintCaps(p, x2 + pad, ry + 24, cw - 2 * pad, 20, "The cup in numbers", pal::caption);
	int done = 0, total = 0, walkovers = 0, disputes = 0;
	TourneyProgress(d, done, total);
	for (const TourneyMatch &mm : d.matches) walkovers += mm.status == TourneyMatchStatus::Walkover && mm.a.Present() && mm.b.Present();
	for (const TourneyLogLine &line : d.log) disputes += line.action == "disputed";
	const std::pair<std::string, std::string> stats[] = {{std::to_string(d.entries), TrFmt(d.Teams() ? "teams, of %s places" : "players, of %s places", {std::to_string(d.maxEntries)})},
		{std::to_string(done), Tr("series played")},
		{end ? SpanText(end - d.startsAt) : std::string("-"),
			!end ? std::string() : end - d.startsAt >= 86400000LL ? TrFmt("from %s to %s", {DayText(d.startsAt), DayText(end)}) : TrFmt("from %s to %s", {HourText(d.startsAt), HourText(end)})},
		{std::to_string(walkovers + disputes), Tr("no-shows and disputes")}};
	const int gw = (cw - 2 * pad - 24) / 2;
	for (int i = 0; i < 4; i++) {
		const int gx = x2 + pad + (i % 2) * (gw + 24), gy = ry + 62 + (i / 2) * 92;
		if (gy + 70 > ry + rh) break;
		UseDisplay(p, 44);
		p.raw = true;
		p.Text(gx, gy, gw, 46, stats[i].first, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(gx, gy + 46, gw, 22, stats[i].second, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	// The biggest surprise: the lowest seed that knocked out the highest one.
	const TourneyMatch *upset = nullptr;
	int gap = 0;
	for (const TourneyMatch &mm : d.matches) {
		if (mm.status != TourneyMatchStatus::Done || !mm.a.seed || !mm.b.seed) continue;
		const TourneyCard &w = mm.winner == mm.a.entryId ? mm.a : mm.b, &lo = mm.winner == mm.a.entryId ? mm.b : mm.a;
		if (w.seed - lo.seed > gap) {
			gap = w.seed - lo.seed;
			upset = &mm;
		}
	}
	// The longest series, as the league timed it.
	const TourneyMatch *longest = nullptr;
	for (const TourneyMatch &mm : d.matches) {
		if (mm.startedAt <= 0 || mm.finishedAt <= mm.startedAt || !mm.a.Present() || !mm.b.Present()) continue;
		if (!longest || mm.finishedAt - mm.startedAt > longest->finishedAt - longest->startedAt) longest = &mm;
	}
	int lineY = ry + 62 + 2 * 92 + 4;
	if ((upset || longest) && lineY + 26 < ry + rh) p.Fill(x2 + pad, lineY - 10, cw - 2 * pad, 1, pal::hairline);
	if (upset && lineY + 26 < ry + rh) {
		const TourneyCard &w = upset->winner == upset->a.entryId ? upset->a : upset->b, &lo = upset->winner == upset->a.entryId ? upset->b : upset->a;
		p.Font(0);
		p.raw = true;
		// Only a single elimination knocks a player out with one loss.
		const char *beat = TourneyIsSwiss(d) || TourneyIsDouble(d) ? "%s beat %s" : "%s knocked out %s";
		p.Text(x2 + pad, lineY, cw - 2 * pad, 24,
			Tr("The surprise") + " \xC2\xB7 " + TrFmt(beat, {w.name + " (" + std::to_string(w.seed) + ")", lo.name + " (" + std::to_string(lo.seed) + ")"}), pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		lineY += 30;
	}
	if (longest && lineY + 26 < ry + rh) {
		p.Font(0);
		p.raw = true;
		p.Text(x2 + pad, lineY, cw - 2 * pad, 24,
			Tr("The longest") + " \xC2\xB7 " + TrFmt("%s, %s against %s", {DurationText(longest->finishedAt - longest->startedAt), longest->a.name, longest->b.name}),
			pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
}

// The player's own result, and the next official cup.
void PaintOwnCup(Painter &p, const State &st, const TourneyDetail &d, int x3, int ry, int cw, int rh, int pad)
{
	const int mx = st.mouseX, my = st.mouseY;
	PaintGlassCard(p, x3, ry, cw, rh);
	PaintCaps(p, x3 + pad, ry + 24, cw - 2 * pad, 20, "Your cup", pal::caption);
	const TourneyEntrant *me = TourneyEntrantBySteamId(d, SteamOwnId());
	PaintPlayerDisc(p, x3 + pad, ry + 60, 52, SteamOwnId(), st.player, pal::redText);
	UseFace(p, BrandFace::UiStrong, 20);
	p.raw = true;
	p.Text(x3 + pad + 68, ry + 58, cw - 2 * pad - 68, 28, st.player, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(0);
	const int place = me ? EntrantPlace(d, *me) : 0;
	std::string result = !place ? Tr(me ? "Your result is not final yet" : "You did not play this cup")
		: TourneyIsSwiss(d) || TourneyIsDouble(d) ? PlacementText(d, place)
												   : PlaceText(place, d.rounds);
	if (me && d.Teams()) {
		for (const TourneyMember &pl : me->members) {
			if (pl.steamId != SteamOwnId() && !pl.name.empty()) result += " \xC2\xB7 " + TrFmt("with %s", {pl.name});
		}
	}
	p.Text(x3 + pad + 68, ry + 86, cw - 2 * pad - 68, 24, result, pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (const TourneyStanding *standing = me && TourneyIsSwiss(d) && d.status == TourneyStatus::Finished ? TourneyStandingOf(d, me->entryId) : nullptr) {
		p.Font(0);
		p.raw = true;
		p.Text(x3 + pad, ry + 130, cw - 2 * pad, 24, TrFmt("You finished with %s and a Buchholz of %s.", {PointsText(standing->points), std::to_string(standing->buchholz)}), pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}
	else if (me) {
		const std::vector<const TourneyMatch *> path = TourneyPathOf(d, me->entryId);
		const TourneyMatch *lastLost = nullptr;
		for (const TourneyMatch *mm : path) {
			if (TourneyMatchOver(mm->status) && mm->winner && mm->winner != me->entryId) lastLost = mm;
		}
		if (lastLost) {
			const TourneyCard &other = lastLost->a.entryId == me->entryId ? lastLost->b : lastLost->a;
			p.Font(0);
			p.raw = true;
			p.Text(x3 + pad, ry + 130, cw - 2 * pad, 24, TrFmt("You fell in the %s against %s.", {MatchStageText(d, *lastLost), other.name}), pal::body,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
		}
	}
	// The next official cup.
	const TourneyListsView lists = LeagueTourneyLists();
	const TourneySummary *nextCup = nullptr;
	for (const TourneySummary &t : lists.open) {
		if (t.official && t.id != d.id && !nextCup) nextCup = &t;
	}
	if (nextCup && rh > 230) {
		const int bx = x3 + pad, by = ry + rh - 24 - 84, bw = cw - 2 * pad;
		p.Fill(bx, by, bw, 84, pal::raised);
		p.Frame(bx, by, bw, 84, pal::edge);
		PaintCaps(p, bx + 16, by + 12, bw - 32, 18, "Next official cup", pal::caption);
		UseFace(p, BrandFace::UiStrong, 17);
		p.raw = true;
		p.Text(bx + 16, by + 38, bw - 32 - 130, 30, WhenText(nextCup->startsAt), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		p.Button(bx + bw - 16 - 120, by + 36, 120, 36, "View", kTourney, ArgOf(kSubOpen, nextCup->id), mx, my, true, true);
	}
}

}

void PaintChampionScreen(Painter &p, const State &st, const Ui &u)
{
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	const auto page = LeagueTourneyPage(u.championSlug, 60000);
	const int top = l.margin + kHeaderButtonH + (l.roomy ? 64 : 44);
	if (!page->loaded) {
		PaintGlassCard(p, m, top, inner, 160);
		p.Font(0);
		p.Text(m + 32, top + 40, inner - 64, 30, page->failed ? "Couldn't reach the league server. Check your connection and try again." : "Reading the tournament...", pal::textDim);
		return;
	}
	const TourneyDetail &d = page->detail;
	const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
	std::vector<uint64_t> ids;
	const int blockH = std::min(l.roomy ? 450 : 360, (bottom - top) * 55 / 100);
	const int size = std::min(400, blockH - 20), px = m + 40, pcy = top + blockH / 2;
	PaintRing(p, px + size / 2, pcy, size * 8 / 10, pal::hairline);
	PaintRing(p, px + size / 2, pcy, size * 11 / 10, RGB(0x22, 0x1f, 0x1d));
	// Only official cups award phalerae; a community cup crowns its champion with a laurel and no prize.
	if (d.official) DrawLeaguePhalera(p, px, pcy - size / 2, size, LeagueMetal::Gold);
	else {
		PaintOrnament(p, kOrnamentLaurel, px, pcy - size / 2, size / 2, size, kGold);
		PaintOrnament(p, kOrnamentLaurel, px + size / 2, pcy - size / 2, size / 2, size, kGold, true);
		UseDisplay(p, size * 45 / 100);
		p.raw = true;
		p.Text(px, pcy - size / 2, size, size, "1", kGold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
	}
	const int pathW = std::min(540, inner * 28 / 100), pathX = m + inner - pathW;
	const int tx = px + size + 56, tw = pathX - 40 - tx;
	PaintCaps(p, tx, top + 30, tw, 20, champ ? (d.official ? Tr("Gold phalera") + " \xC2\xB7 " + Tr("1st place") : Tr("Champion of the cup")) : Tr("No champion"), kGold);
	const int64_t end = TourneyFinishedAt(d);
	PaintChampionName(p, l, d, champ, tx, top, tw, blockH, ids);
	PaintTitlePath(p, d, champ, pathX, top, pathW, blockH);

	// The podium, the cup in numbers and the player's own result.
	const int ry = top + blockH + (l.roomy ? 32 : 20), rh = bottom - ry, cw = (inner - 2 * kCardGap) / 3, pad = 28;
	if (rh < 140) return;
	PaintPodium(p, d, m, ry, cw, rh, pad, ids);
	const int x2 = m + cw + kCardGap;
	PaintCupNumbers(p, d, end, x2, ry, cw, rh, pad);
	const int x3 = x2 + cw + kCardGap;
	PaintOwnCup(p, st, d, x3, ry, cw, rh, pad);
	PatronsWatch(ids);
}

void PaintDisputeDialog(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const Dialog d = DialogBegin(p, st.viewW, st.viewH, 600, 280, "Dispute the result", pal::redText);
	DialogText(p, d, 64, "Tell the organisers in a few words what went wrong. They will look at the reports of both mods and decide.", pal::textDim, 3);
	PaintEditField(p, d.BodyX(), d.y + 130, d.BodyW(), 40, u.disputeText, Tr("What happened?"), st.focus == kTourneyField, false, st.edit, kFieldTourney);
	DialogButtons(p, d, {{"Send", kTourney, ArgOf(kSubDisputeSend, 0), true}, {"Cancel", kTourney, ArgOf(kSubDisputeCancel, 0)}}, mx, my);
}

}

int PaintTourneyRoomStatus(Painter &p, const State &st, int right, int y, std::string &note)
{
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t id = 0;
	if (!st.room.id || !LeagueParseRoomTag(st.room.info.leagueTag, kind, id) || kind != LeagueMatchKind::Tournament) return right;
	const auto mine = LeagueTourneyMine();
	const TourneyNext &n = mine->mine.next;
	if (!n.present || n.match != id) return right;
	const std::string rival = n.opponent.name.empty() ? Tr("Your opponent") : n.opponent.name;
	if (n.pick.present) {
		const std::string clock = TourneyClock(std::max<int64_t>(0, n.pick.until - NowMs())), game = std::to_string(n.game);
		note = n.MyPick() ? TrFmt("You choose the map of game %s · %s", {game, clock}) : TrFmt("%s is choosing the map of game %s · %s", {rival, game, clock});
		return right;
	}
	if (n.canClaimNoShow) {
		p.Font(1);
		const int w = p.CapsWidth(Tr("Claim the win"), 1) + 48, x = right - 12 - w;
		p.Button(x, y, w, kHeaderButtonH, "Claim the win", kTourney, ArgOf(kSubNoShow, n.match), st.mouseX, st.mouseY, !LeagueTourneyBusy(TourneyOp::NoShow, n.match), true);
		note = NoShowClaimText(n);
		return x;
	}
	const int64_t now = NowMs(), left = n.deadlineAt - now;
	if (n.status != TourneyMatchStatus::Ready || n.opponent.inRoom || left <= 0) return right;
	// With the by-rounds pace the deadline is days away and the no-show only counts on its last day.
	// A team is in the room once both its players are.
	const bool teams = n.Teams();
	if (n.pace == TourneyPace::Rounds && n.noshowFrom > now) {
		note = TrFmt(teams ? "%s are not all in the room yet · the no-show counts %s" : "%s is not in the room yet · the no-show counts %s", {rival, FromText(n.noshowFrom)});
	}
	else if (n.pace == TourneyPace::Rounds) note = TrFmt(teams ? "%s are not all in the room yet · round open %s" : "%s is not in the room yet · round open %s", {rival, UntilText(n.deadlineAt)});
	else note = TrFmt(teams ? "%s have %s to be all in the room (until %s)" : "%s has %s to come to the room (until %s)", {n.opponent.name, TourneyClock(left), HourText(n.deadlineAt)});
	return right;
}

int PaintTourneyRoomPick(Painter &p, const State &st, int x, int y, int w)
{
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t id = 0;
	if (!st.room.id || !LeagueParseRoomTag(st.room.info.leagueTag, kind, id) || kind != LeagueMatchKind::Tournament) return 0;
	const auto mine = LeagueTourneyMine();
	const TourneyNext &n = mine->mine.next;
	if (!n.present || n.match != id || !n.pick.present) return 0;
	const int h = 140;
	PaintPickPanel(p, st, n, x, y, w, h);
	return h + 16;
}
