// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_seasons.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/leagueemblem.h"
#include "ui/leaguerewards.h"
#include "ui/leagueui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {

const int kGap = 24;
const COLORREF kGoldPlinth = RGB(0x2A, 0x1A, 0x12), kGoldEdge = RGB(0x5A, 0x42, 0x0E), kGoldNumeral = RGB(0xF2, 0xD0, 0x6B);

// Big capitals that shrink, size by size, before they would be cut.
void FitCaps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, std::initializer_list<int> sizes)
{
	int chosen = *sizes.begin();
	for (int size : sizes) {
		chosen = size;
		Display(p, size);
		p.raw = true;
		const int tw = p.CapsWidth(text, 1);
		p.raw = false;
		if (tw <= w) break;
	}
	Display(p, chosen);
	p.raw = true;
	p.TextCaps(x, y, w, h, text, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
}

// A line of body text centred in `w`, in the smaller size when the larger would be cut.
void FitLine(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c)
{
	Body(p, 17);
	p.raw = true;
	if (p.TextWidth(text) > w) Body(p, 15);
	p.Text(x, y, w, h, text, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
}

// The sizes of the screen's parts: the mockup's on a 1080 screen, smaller on a shorter one so everything keeps its room.
struct Look {
	bool tight = false;
	int labelY = 28, vexY = 62, vexillum = 240, raysY = 182, rays = 250, titleY = 304, titleH = 76, factsY = 386;
	int boxesY = 438, boxH = 226, pictureH = 124, buttonsY = 676, buttonH = 66, noteY = 752;
	int smallVex = 60, nameRowY = 84, frame = 96, frameY = 14, nameSize = 40, nameY = 20, titleCapsY = 74;
	int nextH = 150, podiumBottom = 290, firstAvatar = 64, otherAvatar = 52, rowH = 34;
	int plinths[3] = {100, 80, 60};
};

Look LookFor(int cardH)
{
	Look k;
	if (cardH >= 780) return k;
	k.tight = true;
	k.labelY = 18;
	k.vexY = 44;
	k.vexillum = 160;
	k.raysY = 124;
	k.rays = 180;
	k.titleY = 206;
	k.titleH = 60;
	k.factsY = 268;
	k.boxesY = 306;
	k.boxH = 210;
	k.pictureH = 100;
	k.buttonsY = 528;
	k.buttonH = 58;
	k.noteY = 594;
	k.smallVex = 40;
	k.nameRowY = 60;
	k.frame = 80;
	k.frameY = 8;
	k.nameSize = 34;
	k.nameY = 14;
	k.titleCapsY = 60;
	k.nextH = 104;
	k.podiumBottom = 250;
	k.firstAvatar = 52;
	k.otherAvatar = 44;
	k.rowH = 30;
	k.plinths[0] = 80;
	k.plinths[1] = 60;
	k.plinths[2] = 44;
	return k;
}

// The box of one of the three rewards, under its picture.
void PaintRewardBack(Painter &p, const Look &k, int x, int y, int w)
{
	p.Fill(x, y, w, k.boxH, pal::raised);
	p.Frame(x, y, w, k.boxH, pal::hairline);
	p.Fill(x + 20, y + k.pictureH + 8, w - 40, 1, pal::hairline);
}

// Under the picture, the reward's name and what it is for; one the season did not give is dim, with what it asked for.
void PaintRewardText(Painter &p, const Look &k, int x, int y, int w, const std::string &name, const std::string &what, bool won)
{
	Body(p, 17, true);
	RawText(p, x + 20, y + k.pictureH + 14, w - 40, 26, name, won ? pal::text : pal::disabled);
	p.Font(3);
	const int top = k.pictureH + 42, h = std::min(k.boxH - top - 4, p.Measure(w - 40, what) + 2);
	p.TextWrapped(x + 20, y + top, w - 40, h, what, won ? pal::caption : pal::disabled);
}

void PaintRewards(Painter &p, const State &st, const LeagueCeremony &c, const Look &k, int x, int y, int w)
{
	const LeagueReward &r = c.reward;
	const std::string me = st.player;
	const uint64_t own = SteamOwnId();
	const int gap = 26, boxW = (w - 2 * gap) / 3;
	const bool frame = r.frame;
	const LeagueFrame cosmetic = LeagueFrameOfReward(r);
	for (int i = 0; i < 3; i++) PaintRewardBack(p, k, x + i * (boxW + gap), y, boxW);

	// The vexillum next to the name, as others will see it.
	const LeagueBadge vex = LeagueVexillumBadge(r.tier, r.founder, r.season);
	DrawLeagueBadge(p, x + (boxW - k.smallVex) / 2, y + 10, k.smallVex, vex);
	p.Font(0);
	p.raw = true;
	const int nameW = std::min(boxW - 60, p.TextWidth(me));
	const int rowX = x + (boxW - nameW - kLeagueBadgeGap - kLeagueBadgeSize) / 2;
	p.Text(rowX, y + k.nameRowY, nameW, 26, me, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	DrawBadgeAfterName(p, rowX, y + k.nameRowY, nameW, 26, me, vex, st.mouseX, st.mouseY);
	PaintRewardText(p, k, x, y, boxW, r.founder ? Tr("The Founder's vexillum") : TrFmt("Vexillum of %s", {Tr(LeagueTierName(r.tier))}),
		"Next to your name in rooms, the list, the chat and the profile. Yours for ever.", true);

	// The frame around the player's own picture.
	const int fx = x + boxW + gap;
	PaintFramedAvatar(p, fx + (boxW - k.frame) / 2, y + k.frameY, k.frame, own, me, frame ? cosmetic : LeagueFrame());
	if (!frame) PaintTint(p, fx + 1, y + 1, boxW - 2, k.pictureH + 2, pal::raised, 150);
	const std::string missing = TrFmt("It asked for %s games: you played %s.", {std::to_string(kLeagueFrameGames), std::to_string(r.games)});
	PaintRewardText(p, k, fx, y, boxW, LeagueFrameLabel(cosmetic), frame ? Tr("Around your avatar in the profile and on your card in the leaderboard.") : missing,
		frame);

	// The title under the name.
	const int tx = fx + boxW + gap;
	Display(p, k.nameSize);
	p.raw = true;
	p.Text(tx + 16, y + k.nameY, boxW - 32, k.nameSize + 8, me, frame ? pal::text : pal::disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	// The title in capitals, or on two lines of plain type where the box is too narrow for it.
	const std::string title = LeagueTitleLabel(cosmetic);
	const COLORREF titleInk = frame ? LeagueFrameInk(cosmetic) : pal::disabled;
	p.Font(4);
	p.raw = true;
	if (p.CapsWidth(title, 2) <= boxW - 32) p.TextCaps(tx + 16, y + k.titleCapsY, boxW - 32, 22, title, titleInk, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	else {
		p.Font(3);
		p.Text(tx + 16, y + k.titleCapsY - 2, boxW - 32, 36, title, titleInk, DT_CENTER | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
	}
	p.raw = false;
	PaintRewardText(p, k, tx, y, boxW, Tr("Season title"), frame ? Tr("Under your name in the profile. You can change it for another one you won.") : missing,
		frame);
}

void PaintYourSeason(Painter &p, const State &st, const LeagueCeremony &c, const LeagueSeasonsView &sv, const Look &k, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY;
	const LeagueReward &r = c.reward;
	PaintGlassCard(p, x, y, w, h);
	const COLORREF ink = r.tier != LeagueTier::None ? LeagueTierInk(r.tier) : pal::text;
	const int cx = x + w / 2;
	PaintRays(p, cx, y + k.raysY, k.rays, ink, 70);
	p.Font(4);
	p.TextCaps(x, y + k.labelY, w, 20, "Your season", pal::redText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
	DrawLeagueBadge(p, cx - k.vexillum / 2, y + k.vexY, k.vexillum, LeagueVexillumBadge(r.tier, r.founder, r.season));
	const LeagueFrame title = LeagueFrameOfReward(r);
	if (k.tight) FitCaps(p, x + 24, y + k.titleY, w - 48, k.titleH, LeagueTitleLabel(title), ink, {58, 46, 40});
	else FitCaps(p, x + 24, y + k.titleY, w - 48, k.titleH, LeagueTitleLabel(title), ink, {72, 58, 46});
	// The highest rank and the last, the games, the share won and the place at the close.
	std::string facts = TrFmt("Highest rank: %s", {Tr(r.tier != LeagueTier::None ? LeagueTierName(r.tier) : "Unranked")});
	if (c.finalTier != LeagueTier::None) facts += "  \xC2\xB7  " + TrFmt("final: %s", {RankName(c.finalTier, c.finalDivision)});
	facts += "  \xC2\xB7  " + (r.games == 1 ? Tr("1 game") : TrFmt("%s games", {std::to_string(r.games)}));
	if (c.wins + c.losses > 0) facts += "  \xC2\xB7  " + TrFmt("%s of victories", {std::to_string(LeagueWinRate(c.wins, c.losses)) + " %"});
	if (r.place > 0) facts += "  \xC2\xB7  " + TrFmt("final place #%s", {std::to_string(r.place)});
	FitLine(p, x + 24, y + k.factsY, w - 48, 26, facts, pal::textDim);

	PaintRewards(p, st, c, k, x + 44, y + k.boxesY, w - 88);

	// Equipping takes the three; later leaves them in the profile.
	const bool busy = sv.ceremonyBusy;
	const int bw = std::min(420, (w - 88) * 9 / 16), gw = std::min(236, w - 88 - bw - 16), bx = cx - (bw + 16 + gw) / 2, by = y + k.buttonsY;
	p.fonts[5] = BrandFont(BrandFace::Display, k.tight ? 32 : 36, p.s);
	p.Button(bx, by, bw, k.buttonH, "Equip and continue", kRankedAct, kRkCeremonyEquip, mx, my, !busy, true, 5);
	PaintGhostButton(p, bx + bw + 16, by, gw, k.buttonH, "Choose later", kRankedAct, false, !busy, mx, my, kRkCeremonyLater);
	p.Font(3);
	p.Text(x + 24, y + k.noteY, w - 48, 22, "Your showcase keeps what you won in every season: it never expires, even if your rank goes down.", pal::caption,
		DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// A player of the podium: their picture, name and figures over a plinth with their place in Roman numerals.
void PaintPodiumPlace(Painter &p, const LeagueHallRow &row, int cx, int y, int colW, int avatar, int plinthH, int bottom, bool first)
{
	const uint64_t id = _strtoui64(row.steamId.c_str(), nullptr, 10);
	PaintFramedAvatar(p, cx - avatar / 2, y, avatar, id, row.name, LeagueFrame());
	p.Font(0);
	p.raw = true;
	const std::string name = row.name.empty() ? Tr("Player") : row.name;
	p.Text(cx - colW / 2 + 6, y + avatar + 6, colW - 12, 24, name, pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.raw = false;
	p.Font(3);
	// The Glory and the rank, or the Glory alone where the column is narrow.
	std::string figures = TrFmt("%s Glory", {std::to_string(row.glory)}) + "  \xC2\xB7  " + RankName(row.tier, row.division);
	p.raw = true;
	if (p.TextWidth(figures) > colW - 12) figures = TrFmt("%s Glory", {std::to_string(row.glory)});
	p.raw = false;
	RawText(p, cx - colW / 2 + 6, y + avatar + 30, colW - 12, 20, figures, pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	const int px = cx - colW / 2 + 4, pw = colW - 8, py = bottom - plinthH;
	p.Fill(px, py, pw, plinthH, first ? kGoldPlinth : pal::selected);
	p.Frame(px, py, pw, plinthH + 1, first ? kGoldEdge : pal::edge);
	Display(p, first ? 40 : 30);
	p.TextCaps(px, py + 4, pw, std::min(plinthH - 4, first ? 48 : 38), LeagueRoman(row.place), first ? kGoldNumeral : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
}

void PaintHall(Painter &p, const State &st, const LeagueSeasonsView &sv, const Look &k, int season, int x, int y, int w, int h)
{
	const int mx = st.mouseX, my = st.mouseY, ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, h);
	p.Font(4);
	p.raw = true;
	p.TextCaps(ix, y + 16, iw, 22, Tr("Hall of fame") + "  \xC2\xB7  " + TrFmt("Season %s", {LeagueRoman(season)}) + "  \xC2\xB7  " + RankedQueueLabel(sv.hallQueue),
		pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	p.raw = false;
	const bool ready = sv.hallSeason == season && sv.hallLoaded;
	const std::vector<LeagueHallRow> &top = sv.hall.top;
	const int buttonY = y + h - 54;
	PaintGhostButton(p, ix, buttonY, iw, 38, "See the final leaderboard", kRankedAct, false, true, mx, my, kRkCeremonyBoard);
	if (!ready || top.empty()) {
		p.Font(0);
		if (!ready && !(sv.hallSeason == season && sv.hallFailed)) {
			// The podium and the rows under it, as they will stand.
			const int colW = std::min(195, iw / 3), cx = x + w / 2, bottom = y + k.podiumBottom;
			const int offsets[3] = {0, -1, 1};
			for (int place = 0; place < 3; place++) {
				const int plinth = k.plinths[place], avatar = place == 0 ? k.firstAvatar : k.otherAvatar, px = cx + offsets[place] * colW;
				const int ay = bottom - plinth - avatar - 62;
				PaintSkeleton(p, px - avatar / 2, ay, avatar, avatar);
				PaintSkeleton(p, px - colW / 3, ay + avatar + 14, colW * 2 / 3, 12);
				PaintSkeleton(p, px - colW / 2 + 10, bottom - plinth, colW - 20, plinth);
			}
			p.Fill(ix, bottom, iw, 1, pal::lineStrong);
			const int rows = std::max(0, (buttonY - 8 - bottom - 8) / k.rowH);
			PaintSkeletonRows(p, ix, bottom + 8, iw, k.rowH, std::min(7, rows), {{ix, 48}, {ix + 48, iw - 48 - 120}, {ix + iw - 110, 110}});
		}
		else {
			const char *text = !ready ? "Couldn't reach the league server. Check your connection and try again."
									  : "Nobody played the elite's minimum of games this season: the hall of fame stays empty.";
			p.TextWrapped(ix, y + 60, iw, 60, text, pal::caption);
		}
		return;
	}
	std::vector<uint64_t> ids;
	for (const LeagueHallRow &r : top) ids.push_back(_strtoui64(r.steamId.c_str(), nullptr, 10));
	PatronsWatch(ids);
	// The first three on a podium, the first in the middle with the tallest plinth; the rest of the ten in rows under it.
	const int colW = std::min(195, iw / 3), cx = x + w / 2, bottom = y + k.podiumBottom;
	const int places[3][2] = {{0, 0}, {1, -1}, {2, 1}};
	for (const auto &pl : places) {
		if (pl[0] >= static_cast<int>(top.size())) continue;
		const int plinth = k.plinths[pl[0]], avatar = pl[0] == 0 ? k.firstAvatar : k.otherAvatar;
		PaintPodiumPlace(p, top[pl[0]], cx + pl[1] * colW, bottom - plinth - avatar - 62, colW, avatar, plinth, bottom, pl[0] == 0);
	}
	p.Fill(ix, bottom, iw, 1, pal::lineStrong);
	const int rowH = k.rowH;
	int ry = bottom + 8;
	for (size_t i = 3; i < top.size() && ry + rowH <= buttonY - 8; i++) {
		const LeagueHallRow &r = top[i];
		p.Font(1);
		RawText(p, ix, ry, 40, rowH, std::to_string(r.place), pal::textDim);
		const uint64_t id = _strtoui64(r.steamId.c_str(), nullptr, 10);
		const LeagueBadge badge = RankBadge(r.tier, r.division);
		p.Font(0);
		const int nameW = iw - 40 - 200 - LeagueBadgeRoom(badge);
		RawText(p, ix + 40, ry, nameW, rowH, r.name.empty() ? Tr("Player") : r.name, pal::text);
		p.raw = true;
		SteamProfileHotspot(p, ix + 40, ry, nameW, rowH, r.name.empty() ? Tr("Player") : r.name, id, mx, my);
		DrawBadgeAfterName(p, ix + 40, ry, nameW, rowH, r.name.empty() ? Tr("Player") : r.name, badge, mx, my, id);
		p.raw = false;
		p.Font(3);
		RawText(p, ix + iw - 200, ry, 200, rowH, TrFmt("%s Glory", {std::to_string(r.glory)}) + "  \xC2\xB7  " + RankName(r.tier, r.division), pal::caption,
			DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		ry += rowH;
		p.Fill(ix, ry - 1, iw, 1, pal::hairline);
	}
}

// The season that follows, once the server lists it (it lists only the seasons that started).
const LeagueSeasonInfo *NextSeason(const LeagueSeasonsView &sv, int closed)
{
	const LeagueSeasonInfo *next = nullptr;
	for (const LeagueSeasonInfo &s : sv.seasons) {
		if (s.id > closed && !s.closed && (!next || s.id < next->id)) next = &s;
	}
	return next;
}

void PaintNextSeason(Painter &p, const LeagueSeasonInfo &s, const Look &k, int x, int y, int w, int h)
{
	const int ix = x + 24, iw = w - 48;
	PaintGlassCard(p, x, y, w, h);
	const bool started = s.startsAt <= LeagueServerNowMs();
	p.Font(4);
	p.raw = true;
	p.TextCaps(ix, y + (k.tight ? 12 : 16), iw, 22, started ? TrFmt("Season %s has begun", {LeagueRoman(s.id)}) : TrFmt("Season %s is coming", {LeagueRoman(s.id)}),
		pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
	p.raw = false;
	Display(p, k.tight ? 38 : 50);
	p.raw = true;
	p.TextCaps(ix, y + (k.tight ? 34 : 44), iw, k.tight ? 42 : 54, s.name.empty() ? TrFmt("Season %s", {LeagueRoman(s.id)}) : s.name, pal::text,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 1);
	p.raw = false;
	p.Font(0);
	if (s.startsAt && s.endsAt) {
		RawText(p, ix, y + (k.tight ? 76 : 102), iw, 24, TrFmt("From %s to %s", {DateText(LocalTime(s.startsAt)), DateText(LocalTime(s.endsAt))}), pal::textDim);
	}
}

}

void PaintCeremony(Painter &p, const State &st)
{
	const int mx = st.mouseX, my = st.mouseY;
	const auto meView = LeagueMeShared();
	const LeagueMeView &me = *meView;
	const auto seasonsView = LeagueSeasonsShared();
	const LeagueSeasonsView &sv = *seasonsView;
	const int season = st.ranked.ceremonySeason;
	LeagueCeremony c = me.me.ceremony;
	// Once seen, /me no longer has it: the screen keeps what it opened with from the season's reward.
	if (!c.present || c.reward.season != season) {
		if (const LeagueReward *r = LeagueRewardOf(me.me, season)) c.reward = *r;
		c.reward.season = season;
	}
	const ScreenLayout l = ScreenLayoutFor(st.viewW, st.viewH);
	const int m = l.margin, top = l.top, inner = l.w - 2 * m, h = l.h - m - top;
	p.Font(4);
	const int cw = GhostButtonWidth(p, "Continue", false);
	PaintGhostButton(p, l.w - m - cw, l.margin, cw, kHeaderButtonH, "Continue", kRankedAct, false, !sv.ceremonyBusy, mx, my, kRkCeremonyLater);
	p.raw = true;
	PaintScreenTitle(p, l, TrFmt("End of Season %s", {LeagueRoman(season)}), {}, l.w - m - cw - 24, mx, my);
	p.raw = false;
	std::string info = c.reward.seasonName;
	if (const LeagueSeasonInfo *s = LeagueSeasonById(sv.seasons, season); s && s->startsAt && s->endsAt) {
		if (info.empty()) info = s->name;
		info += (info.empty() ? "" : "  \xC2\xB7  ") + DateText(LocalTime(s->startsAt)) + " \xE2\x80\x93 " + DateText(LocalTime(s->endsAt));
	}
	if (!info.empty()) {
		p.raw = true;
		PaintScreenInfo(p, l, info);
		p.raw = false;
	}
	const int leftW = std::max(760, inner * 1100 / 1792), rightW = inner - leftW - kGap;
	const Look k = LookFor(h);
	PaintYourSeason(p, st, c, sv, k, m, top, leftW, h);
	const int rx = m + leftW + kGap;
	const LeagueSeasonInfo *next = NextSeason(sv, season);
	const int hallH = next ? h - kGap - k.nextH : h;
	PaintHall(p, st, sv, k, season, rx, top, rightW, hallH);
	if (next) PaintNextSeason(p, *next, k, rx, top + hallH + kGap, rightW, k.nextH);
}

}
