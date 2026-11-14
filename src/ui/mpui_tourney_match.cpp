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

void PaintPlayerColumn(Painter &p, int cx, int y, int h, uint64_t id, const std::string &name, bool me, int seed, const std::string &before,
	const std::string &state, bool stateOk)
{
	const int disc = h >= 400 ? 132 : 108;
	int cy = y + (h - (disc + 216)) / 2;
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
}

// The map's frame: the mockup's sketch of a random map, with the two starts.
void PaintMapBox(Painter &p, int x, int y, int w, int h)
{
	p.Fill(x, y, w, h, RGB(0x0c, 0x0b, 0x0a));
	p.Frame(x, y, w, h, pal::lineStrong);
	p.Frame(x + 7, y + 7, w - 14, h - 14, RGB(0x5a, 0x4a, 0x2a));
	const int pts[][2] = {{14, 104}, {56, 52}, {84, 82}, {118, 26}, {148, 70}, {174, 46}, {208, 86}, {230, 68}};
	for (int i = 0; i + 1 < 8; i++) {
		p.Line(x + pts[i][0] * w / 258, y + pts[i][1] * h / 130, x + pts[i + 1][0] * w / 258, y + pts[i + 1][1] * h / 130, pal::edgeHot);
		p.Line(x + pts[i][0] * w / 258, y + (pts[i][1] + 12) * h / 130, x + pts[i + 1][0] * w / 258, y + (pts[i + 1][1] + 12) * h / 130, pal::lineStrong);
	}
	p.Shape(x + 34 * w / 258, y + 92 * h / 130, 12, 12, InUnitEllipse, pal::red);
	p.Shape(x + 210 * w / 258, y + 34 * h / 130, 12, 12, InUnitEllipse, RGB(0x2f, 0x6f, 0xd1));
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

bool PaintMatchHeader(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const TourneyNext next = LeagueTourneyMine().mine.next;
	const int right = PaintHeaderActions(p, l, {{Tr(u.slug.empty() ? "Tournaments" : "Back to the bracket"), kBack, true, true}}, mx, my);
	const int64_t left = next.present && next.status == TourneyMatchStatus::Ready ? next.deadlineAt - NowMs() : 0;
	int titleRight = right;
	if (left > 0) {
		const int bw = 300, bx = l.w - l.margin - bw, by = l.margin + kHeaderButtonH + 10;
		PaintCaps(p, bx, by, bw, 18, "Time to enter", pal::warn, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		UseDisplay(p, l.roomy ? 56 : 46);
		p.raw = true;
		p.Text(bx, by + 18, bw, l.roomy ? 56 : 46, TourneyClock(left), pal::warn, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.Font(3);
		p.Text(bx, by + (l.roomy ? 74 : 64), bw, 20, TrFmt("until %s", {HourText(next.deadlineAt)}), pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		p.raw = false;
		titleRight = std::min(right, bx - 20);
	}
	PaintScreenTitle(p, l, "Your next match", {}, titleRight, mx, my);
	if (!next.present) return true;
	std::string info = next.name + "   \xC2\xB7   " + StageText(next.stage, next.round) + "   \xC2\xB7   " + TrFmt("Best of %s", {std::to_string(next.bestOf)});
	if (next.bestOf > 1) info += "   \xC2\xB7   " + TrFmt("Game %s, you %s", {std::to_string(next.game), std::to_string(next.scoreMe) + "\xE2\x80\x93" + std::to_string(next.scoreThem)});
	p.Font(0);
	p.raw = true;
	p.Text(l.margin, l.infoY, titleRight - l.margin, l.infoH, info, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	return true;
}

void PaintMatchScreen(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	const TourneyMineView mine = LeagueTourneyMine();
	const TourneyNext &n = mine.mine.next;
	if (!n.present) {
		PaintGlassCard(p, m, l.top, inner, 200);
		p.Font(0);
		p.Text(m + 32, l.top + 40, inner - 64, 30, mine.loaded ? "You have no match to play now." : "Reading your tournaments...", pal::textDim);
		p.Button(m + 32, l.top + 100, 320, 48, "Back to the tournaments", kBack, 0, mx, my);
		return;
	}
	const TourneyPageView page = LeagueTourneyPage(n.slug, 30000);
	const TourneyEntrant *meE = page.loaded ? TourneyEntrantBySteamId(page.detail, SteamOwnId()) : nullptr;
	const TourneyEntrant *themE = page.loaded && n.hasOpponent ? TourneyEntrantBySteamId(page.detail, n.opponent.steamId) : nullptr;
	PatronsWatch({SteamOwnId(), n.opponent.steamId});
	PaintRing(p, l.w / 2, l.top + 300, 380, pal::hairline);

	const int cardH = l.roomy ? 430 : 360, cy = l.top - (l.roomy ? 10 : 6), centerW = 420, sideW = (inner - centerW) / 2;
	PaintGlassCard(p, m, cy, inner, cardH);
	p.Fill(m + sideW, cy + 1, 1, cardH - 2, pal::hairline);
	p.Fill(m + sideW + centerW, cy + 1, 1, cardH - 2, pal::hairline);
	const bool live = n.status == TourneyMatchStatus::Live;
	PaintPlayerColumn(p, m + sideW / 2, cy, cardH, SteamOwnId(), st.player, true, n.me.seed, meE ? PreviousText(page.detail, meE->entryId, n.round) : std::string(),
		live ? std::string() : Tr(n.me.inRoom ? "In the room" : "You're not in the room yet"), n.me.inRoom);
	if (n.hasOpponent) {
		PaintPlayerColumn(p, m + sideW + centerW + sideW / 2, cy, cardH, n.opponent.steamId, n.opponent.name, false, n.opponent.seed,
			themE ? PreviousText(page.detail, themE->entryId, n.round) : std::string(), live ? std::string() : Tr(n.opponent.inRoom ? "In the room" : "Not in the room yet"),
			n.opponent.inRoom);
	}
	else {
		p.Font(0);
		p.Text(m + sideW + centerW, cy, sideW, cardH, "Your opponent is still to be decided.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	// The middle: the stage, VS and the map.
	const int ccx = m + sideW, mapW = 260, mapH = 132;
	int my0 = cy + (cardH - 360) / 2;
	PaintCaps(p, ccx, my0, centerW, 20, StageText(n.stage, n.round) + " \xC2\xB7 " + TrFmt("Bo%s", {std::to_string(n.bestOf)}), pal::caption,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	UseDisplay(p, 120);
	p.TextCaps(ccx, my0 + 20, centerW, 124, "Vs", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	PaintMapBox(p, ccx + (centerW - mapW) / 2, my0 + 156, mapW, mapH);
	UseFace(p, BrandFace::UiStrong, 17);
	p.raw = true;
	TourneyMap mapOnly = n.map;
	mapOnly.size.clear();
	p.Text(ccx, my0 + 300, centerW, 24, n.map.Known() ? MapText(mapOnly) : Tr("Chosen in the room"), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	// The size alone, as MapText writes it after the name.
	const std::string full = MapText(n.map), name = MapText(mapOnly);
	std::string sub = Tr(n.map.Scenario() ? "Scenario" : "Random map");
	if (n.map.Known() && full.size() > name.size()) sub += full.substr(name.size());
	sub += " \xC2\xB7 " + Tr("2 players");
	p.Font(3);
	p.Text(ccx, my0 + 326, centerW, 20, sub, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;

	// The big button and what it will do.
	const RoomFlow &f = u.room;
	const bool flowing = f.phase != RoomFlow::kIdle && f.match == n.match;
	const bool inRoom = st.room.id && TourneyMatchOfTag(st.room.info.leagueTag) == n.match;
	std::string label = "Go to the room", line;
	bool enabled = n.status == TourneyMatchStatus::Ready && !inRoom && !flowing;
	if (inRoom) label = "In the room";
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
	if (line.empty()) {
		if (n.status == TourneyMatchStatus::Pending) line = Tr("Your opponent comes from a match still being played. We tell you when it is over.");
		else if (n.status == TourneyMatchStatus::Disputed) line = Tr("The organisers are looking at the reports of the game.");
		else if (live) line = Tr("The result is reported by the mod when the game ends.");
		else if (n.lobby && n.roomClaimBy == n.opponent.steamId && n.opponent.steamId)
			line = TrFmt("%s opened the room. You enter as a player; the map and the rules are the tournament's.", {n.opponent.name});
		else if (n.lobby) line = Tr("The room is open. You enter as a player; the map and the rules are the tournament's.");
		else line = Tr("Nobody has opened the room yet: you will open it, with the map and the rules of the tournament.");
	}
	// Before the room: this computer's copy of the round's scenario, when it is not the cup's.
	const std::string mapWarning = CupMapWarning(n.map);
	COLORREF lineInk = pal::textDim;
	if (!mapWarning.empty() && !inRoom && !live && n.status != TourneyMatchStatus::Disputed) {
		line = mapWarning;
		lineInk = pal::warn;
	}
	const int bw = 460, bh = l.roomy ? 72 : 60, by = cy + cardH + (l.roomy ? 24 : 16);
	p.Button((l.w - bw) / 2, by, bw, bh, label, kTourney, ArgOf(kSubGo, n.match), mx, my, enabled, true, 2);
	p.Font(0);
	p.raw = true;
	p.Text(m, by + bh + 8, inner, 24, line, lineInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;

	// Three cards: what is checked, the rules and what to do if something fails.
	const int ry = by + bh + 44, rh = bottom - ry, cw = (inner - 2 * kCardGap) / 3, pad = 26;
	if (rh < 120) return;
	PaintGlassCard(p, m, ry, cw, rh);
	PaintCaps(p, m + pad, ry + 20, cw - 2 * pad, 20, "Ready to enter", pal::caption);
	const bool sameContent = n.contentKey.empty() || n.contentKey == SteamOwnGameplayKey();
	const CupMapState mapState = LeagueCupMap(n.map);
	const TourneyMyEntry *entry = MyEntry(mine, n.tournamentId);
	struct Check {
		bool ok;
		std::string text;
	} checks[] = {
		{sameContent, sameContent ? TrFmt("%s active, the same content as the room", {n.contentName.empty() ? Tr("The mod's rules") : n.contentName})
								  : TrFmt("This tournament needs %s: switch your content profile", {n.contentName.empty() ? Tr("other content") : n.contentName})},
		{mapState.status == CupMapStatus::Ready, MapCheckText(n.map, mapState)},
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

	const int x2 = m + cw + kCardGap;
	PaintGlassCard(p, x2, ry, cw, rh);
	PaintCaps(p, x2 + pad, ry + 20, cw - 2 * pad, 20, "Rules of this match", pal::caption);
	std::string after;
	if (n.round >= n.rounds) after = Tr("The winner is the champion");
	else if (page.loaded) {
		const TourneyMatch *following = nullptr, *mineM = nullptr;
		for (const TourneyMatch &mm : page.detail.matches) {
			if (mm.id == n.match) mineM = &mm;
		}
		if (mineM) following = TourneyMatchAt(page.detail, mineM->round + 1, mineM->position / 2);
		const TourneyCard *other = following ? (mineM->position % 2 ? &following->a : &following->b) : nullptr;
		after = other && other->Present() ? TrFmt("If you win, %s against %s", {StageText(following->stage, following->round), other->name})
										  : TrFmt("If you win, the %s", {StageText(TourneyStageOf(n.round + 1, n.rounds), n.round + 1)});
	}
	const std::pair<std::string, std::string> rules[] = {{Tr("Settings"), PresetText(n.settings)}, {Tr("Map"), n.map.Known() ? MapText(n.map) : Tr("Chosen in the room")},
		{Tr("Civilizations"), Tr(n.settings.hideCivs ? "Hidden until the game starts" : "Visible in the room")},
		{Tr("Spectators"), TourneySpecDelay(n.settings) ? TrFmt("With a %s s delay", {std::to_string(TourneySpecDelay(n.settings))}) : Tr("No spectators")}, {Tr("After"), after}};
	ly = ry + 54;
	for (const auto &r : rules) {
		if (ly + 26 > ry + rh || r.second.empty()) continue;
		p.Font(0);
		p.raw = true;
		p.Text(x2 + pad, ly, 140, 24, r.first, pal::caption, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		p.Text(x2 + pad + 150, ly, cw - 2 * pad - 150, 24, r.second, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
		ly += 30;
	}

	const int x3 = x2 + cw + kCardGap;
	PaintGlassCard(p, x3, ry, cw, rh);
	PaintCaps(p, x3 + pad, ry + 20, cw - 2 * pad, 20, "If something fails", pal::caption);
	p.Font(0);
	const char *const lines[] = {"You drop: you have 5 min to return to the game.", "It ends in the first 3 minutes with no losses: it is played again.",
		"The mod sends the result. If the reports differ, the organisers decide."};
	ly = ry + 54;
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

bool PaintChampionHeader(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const TourneyPageView page = LeagueTourneyPage(u.championSlug, 60000);
	const int right = PaintHeaderActions(p, l, {{Tr("Back to the bracket"), kBack, true, true}}, mx, my);
	if (!page.loaded) return true;
	const TourneyDetail &d = page.detail;
	const int y = l.margin + kHeaderButtonH + (l.roomy ? 22 : 12);
	p.raw = true;
	PaintCaps(p, l.margin, y, right - l.margin - 420, 22, d.name + " \xC2\xB7 " + Tr("Final result"), pal::redText);
	const TourneyMatch *final = d.rounds ? TourneyMatchAt(d, d.rounds, 0) : nullptr;
	p.Font(0);
	const std::string when = DayText(d.startsAt) + " \xC2\xB7 " +
		(final && final->finishedAt ? TrFmt("from %s to %s", {HourText(d.startsAt), HourText(final->finishedAt)}) : TrFmt("from %s", {HourText(d.startsAt)}));
	p.Text(l.w - l.margin - 420, y, 420, 22, when, pal::caption, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	p.raw = false;
	return true;
}

void PaintChampionScreen(Painter &p, const State &st, const Ui &u)
{
	const int mx = st.mouseX, my = st.mouseY;
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, inner = l.w - 2 * m, bottom = l.h - m;
	const TourneyPageView page = LeagueTourneyPage(u.championSlug, 60000);
	const int top = l.margin + kHeaderButtonH + (l.roomy ? 64 : 44);
	if (!page.loaded) {
		PaintGlassCard(p, m, top, inner, 160);
		p.Font(0);
		p.Text(m + 32, top + 40, inner - 64, 30, page.failed ? "Couldn't reach the league server. Check your connection and try again." : "Reading the tournament...", pal::textDim);
		return;
	}
	const TourneyDetail &d = page.detail;
	const TourneyEntrant *champ = TourneyEntrantOf(d, TourneyChampion(d));
	std::vector<uint64_t> ids;
	const int blockH = std::min(l.roomy ? 450 : 360, (bottom - top) * 55 / 100);
	const int size = std::min(400, blockH - 20), px = m + 40, pcy = top + blockH / 2;
	PaintRing(p, px + size / 2, pcy, size * 8 / 10, pal::hairline);
	PaintRing(p, px + size / 2, pcy, size * 11 / 10, RGB(0x22, 0x1f, 0x1d));
	DrawLeaguePhalera(p, px, pcy - size / 2, size, LeagueMetal::Gold);
	const int pathW = std::min(540, inner * 28 / 100), pathX = m + inner - pathW;
	const int tx = px + size + 56, tw = pathX - 40 - tx;
	PaintCaps(p, tx, top + 30, tw, 20, champ ? Tr("Gold phalera") + " \xC2\xB7 " + Tr("1st place") : Tr("No champion"), kGold);
	const TourneyMatch *final = d.rounds ? TourneyMatchAt(d, d.rounds, 0) : nullptr;
	if (champ) {
		int nameSize = std::min(210, blockH * 45 / 100);
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
		UseFace(p, BrandFace::Body, 26);
		p.raw = true;
		p.Text(tx, ty, tw, 34, TrFmt("wins %s", {d.name}), pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ty += 42;
		if (final && final->a.Present() && final->b.Present()) {
			const bool champA = final->a.entryId == champ->entryId;
			const TourneyCard &other = champA ? final->b : final->a;
			const std::string score = std::to_string(champA ? final->scoreA : final->scoreB) + "\xE2\x80\x93" + std::to_string(champA ? final->scoreB : final->scoreA);
			UseFace(p, BrandFace::Body, 18);
			p.Text(tx, ty, tw, 26, final->status == TourneyMatchStatus::Walkover ? TrFmt("Final: %s did not come", {other.name}) : TrFmt("Final: %s against %s", {score, other.name}),
				pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

	// The path to the title, match by match.
	PaintGlassCard(p, pathX, top, pathW, blockH);
	PaintCaps(p, pathX + 28, top + 24, pathW - 56, 20, "Path to the title", pal::caption);
	if (champ) {
		const std::vector<const TourneyMatch *> path = TourneyPathOf(d, champ->entryId);
		const int stepH = std::max(54, std::min(84, (blockH - 80) / std::max<int>(1, static_cast<int>(path.size()))));
		int sy = top + 66;
		p.Fill(pathX + 35, sy + 12, 2, std::max(0, stepH * (static_cast<int>(path.size()) - 1)), pal::lineStrong);
		for (const TourneyMatch *mm : path) {
			const bool last = mm == path.back();
			p.Shape(pathX + 28, sy + 4, 16, 16, InUnitEllipse, last ? kGold : RGB(0x7a, 0x75, 0x6e));
			if (!last) p.Shape(pathX + 30, sy + 6, 12, 12, InUnitEllipse, pal::panel);
			const int sx = pathX + 62, sw = pathW - 62 - 28;
			std::string stage = StageText(mm->stage, mm->round);
			if (mm->bestOf > 1) stage += " \xC2\xB7 " + TrFmt("Best of %s", {std::to_string(mm->bestOf)});
			PaintCaps(p, sx, sy, sw, 20, stage, last ? kGold : pal::caption);
			const bool sideB = mm->b.entryId == champ->entryId;
			const TourneyCard &other = sideB ? mm->a : mm->b;
			UseDisplay(p, 30);
			p.raw = true;
			std::string score = mm->status == TourneyMatchStatus::Bye ? std::string("-") : std::to_string(sideB ? mm->scoreB : mm->scoreA) + "\xE2\x80\x93" + std::to_string(sideB ? mm->scoreA : mm->scoreB);
			if (mm->status == TourneyMatchStatus::Walkover) score = "W";
			p.Text(sx, sy + 20, 70, 34, score, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			UseFace(p, BrandFace::Body, 17);
			const std::string against = mm->status == TourneyMatchStatus::Bye ? Tr("Bye: straight to the next round")
				: other.seed ? TrFmt("against %s (%s)", {other.name, std::to_string(other.seed)})
							 : TrFmt("against %s", {other.name});
			p.Text(sx + 74, sy + 20, sw - 74, 34, against, pal::body, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.raw = false;
			sy += stepH;
		}
	}

	// The podium, the cup in numbers and the player's own result.
	const int ry = top + blockH + (l.roomy ? 32 : 20), rh = bottom - ry, cw = (inner - 2 * kCardGap) / 3, pad = 28;
	if (rh < 140) return;
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
		UseDisplay(p, 30);
		p.Text(m + pad, py, 50, rowH, s.label, s.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		DrawLeaguePhalera(p, m + pad + 56, py + (rowH - 40) / 2, 40, s.metal);
		std::string names;
		for (const TourneyEntrant *e : who) {
			names += (names.empty() ? "" : " " + Tr("and") + " ") + e->name;
			ids.push_back(e->steamId);
		}
		UseFace(p, BrandFace::UiStrong, 20);
		p.raw = true;
		p.Text(m + pad + 110, py, cw - 2 * pad - 110, rowH / 2 + 8, names.empty() ? std::string("-") : names, pal::text, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Font(3);
		p.Text(m + pad + 110, py + rowH / 2 + 8, cw - 2 * pad - 110, 20,
			who.size() == 1 && who[0]->seed ? TrFmt("Seed %s", {std::to_string(who[0]->seed)}) : s.place == 3 ? Tr("Semifinals") : std::string(), pal::caption,
			DT_LEFT | DT_TOP | DT_SINGLELINE);
		p.raw = false;
		if (s.place < 3) p.Fill(m + pad, py + rowH - 1, cw - 2 * pad, 1, pal::hairline);
		py += rowH;
	}

	const int x2 = m + cw + kCardGap;
	PaintGlassCard(p, x2, ry, cw, rh);
	PaintCaps(p, x2 + pad, ry + 24, cw - 2 * pad, 20, "The cup in numbers", pal::caption);
	int done = 0, total = 0, walkovers = 0, disputes = 0;
	TourneyProgress(d, done, total);
	for (const TourneyMatch &mm : d.matches) walkovers += mm.status == TourneyMatchStatus::Walkover && mm.a.Present() && mm.b.Present();
	for (const TourneyLogLine &line : d.log) disputes += line.action == "disputed";
	const std::pair<std::string, std::string> stats[] = {{std::to_string(d.entries), TrFmt("players, of %s places", {std::to_string(d.maxEntries)})},
		{std::to_string(done), Tr("series played")},
		{final && final->finishedAt ? DurationText(final->finishedAt - d.startsAt) : std::string("-"),
			final && final->finishedAt ? TrFmt("from %s to %s", {HourText(d.startsAt), HourText(final->finishedAt)}) : std::string()},
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
	if (upset && ry + 62 + 2 * 92 + 30 < ry + rh) {
		const TourneyCard &w = upset->winner == upset->a.entryId ? upset->a : upset->b, &lo = upset->winner == upset->a.entryId ? upset->b : upset->a;
		p.Fill(x2 + pad, ry + 62 + 2 * 92 - 6, cw - 2 * pad, 1, pal::hairline);
		p.Font(0);
		p.raw = true;
		p.Text(x2 + pad, ry + 62 + 2 * 92 + 4, cw - 2 * pad, 24,
			Tr("The surprise") + " \xC2\xB7 " + TrFmt("%s knocked out %s", {w.name + " (" + std::to_string(w.seed) + ")", lo.name + " (" + std::to_string(lo.seed) + ")"}), pal::body,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.raw = false;
	}

	const int x3 = x2 + cw + kCardGap;
	PaintGlassCard(p, x3, ry, cw, rh);
	PaintCaps(p, x3 + pad, ry + 24, cw - 2 * pad, 20, "Your cup", pal::caption);
	const TourneyEntrant *me = TourneyEntrantBySteamId(d, SteamOwnId());
	PaintPlayerDisc(p, x3 + pad, ry + 60, 52, SteamOwnId(), st.player, pal::redText);
	UseFace(p, BrandFace::UiStrong, 20);
	p.raw = true;
	p.Text(x3 + pad + 68, ry + 58, cw - 2 * pad - 68, 28, st.player, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.Font(0);
	p.Text(x3 + pad + 68, ry + 86, cw - 2 * pad - 68, 24, me && me->placement ? PlaceText(me->placement, d.rounds) : Tr(me ? "Your result is not final yet" : "You did not play this cup"),
		pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	if (me) {
		const std::vector<const TourneyMatch *> path = TourneyPathOf(d, me->entryId);
		const TourneyMatch *lastLost = nullptr;
		for (const TourneyMatch *mm : path) {
			if (TourneyMatchOver(mm->status) && mm->winner && mm->winner != me->entryId) lastLost = mm;
		}
		if (lastLost) {
			const TourneyCard &other = lastLost->a.entryId == me->entryId ? lastLost->b : lastLost->a;
			p.Font(0);
			p.raw = true;
			p.Text(x3 + pad, ry + 130, cw - 2 * pad, 24, TrFmt("You fell in the %s against %s.", {StageText(lastLost->stage, lastLost->round), other.name}), pal::body,
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
