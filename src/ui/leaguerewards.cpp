// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguerewards.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/leagueranked.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/leagueclient_seasons.h"
#include "net/steamnet.h"
#include "ui/fullscreen.h"
#include "ui/leagueemblem.h"
#include "ui/leagueui.h"

using namespace ui;

namespace {

struct FrameColors {
	COLORREF dark, body, light;
};

COLORREF Ref(uint32_t rgb)
{
	return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

FrameColors ColorsOf(const LeagueFrame &f)
{
	const LeagueColors c = f.tier != LeagueTier::None ? LeagueTierColors(f.tier) : f.founder ? LeagueFounderColors() : LeagueColors{0x2A2725, 0x5A5550, 0xBDBBB4, 0xBDBBB4};
	return {Ref(c.border), Ref(c.body), Ref(c.light)};
}

std::string TierWord(const LeagueFrame &f)
{
	if (f.tier != LeagueTier::None) return Tr(LeagueTierName(f.tier));
	return f.founder ? Tr("Founder") : std::string();
}

// The first two letters of a name, whole, for a missing picture.
std::string Initials(const std::string &name)
{
	size_t end = 0;
	for (int letters = 0; letters < 2 && end < name.size(); letters++) {
		end++;
		while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80) end++;
	}
	return name.substr(0, end);
}

// A row of a list to pick from, as the badge's: lit while chosen, a click elsewhere.
bool ChoiceRow(Painter &p, int x, int y, int w, int h, bool chosen, bool enabled, int action, int arg, int mx, int my)
{
	const bool hot = enabled && !chosen && p.Hover(x, y, w, h, mx, my);
	if (chosen || hot) p.Fill(x, y, w, h, chosen ? pal::selected : pal::raised);
	if (chosen) p.Fill(x, y, 3, h, pal::red);
	if (enabled && !chosen) p.AddHit(x, y, w, h, action, arg);
	return hot;
}

}

void PaintLeagueFrame(Painter &p, int x, int y, int size, const LeagueFrame &frame)
{
	if (!frame.Shown() || size < 16) return;
	const FrameColors c = ColorsOf(frame);
	const bool big = size >= 80, plate = size >= 56;
	const int b = big ? 4 : size >= 40 ? 3 : 2;
	p.Fill(x, y, size, b, c.body);
	p.Fill(x, y + size - b, size, b, c.body);
	p.Fill(x, y, b, size, c.body);
	p.Fill(x + size - b, y, b, size, c.body);
	p.Frame(x + b, y + b, size - 2 * b, size - 2 * b, c.light);
	// The lozenges sit on the corners, a little inside.
	const int r = big ? 6 : plate ? 5 : 3, in = b / 2;
	const int corners[4][2] = {{x + in, y + in}, {x + size - in, y + in}, {x + in, y + size - in}, {x + size - in, y + size - in}};
	for (const auto &k : corners) {
		p.Diamond(k[0], k[1], r, c.dark);
		p.Diamond(k[0], k[1], r - 2, c.light);
	}
	if (!plate) return;
	const int pw = big ? 52 : 44, ph = big ? 18 : 16, px = x + (size - pw) / 2, py = y + size - ph + 7;
	p.Fill(px, py, pw, ph, c.dark);
	p.Frame(px, py, pw, ph, c.body);
	SelectObject(p.dc, BrandFont(BrandFace::Display, big ? 14 : 12, p.s));
	p.raw = true;
	// Without the rank the plate only says the season.
	const std::string who = TierWord(frame);
	p.TextCaps(px, py + 1, pw, ph, who.empty() ? LeagueRoman(frame.season) : LeagueFramePlate(who, frame.season), c.light, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
	p.raw = false;
}

void PaintFramedAvatar(Painter &p, int x, int y, int size, uint64_t steamId, const std::string &name, const LeagueFrame &frame)
{
	const int in = frame.Shown() ? std::min(kLeagueFrameInset, size / 8) : 0, as = size - 2 * in;
	const std::shared_ptr<const OnlineAvatar> a = steamId ? SteamAvatar(steamId) : nullptr;
	if (a && !a->bgra.empty()) p.Image(x + in, y + in, as, as, a->bgra.data(), a->width, a->height);
	else {
		p.Fill(x + in, y + in, as, as, pal::raised);
		SelectObject(p.dc, BrandFont(BrandFace::Display, std::max(12, as * 2 / 5), p.s));
		p.raw = true;
		p.TextCaps(x + in, y + in, as, as, Initials(name), pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		p.raw = false;
	}
	if (frame.Shown()) PaintLeagueFrame(p, x, y, size, frame);
	else p.Frame(x, y, size, size, pal::edge);
}

std::string LeagueFrameLabel(const LeagueFrame &frame)
{
	const std::string who = TierWord(frame);
	if (who.empty()) return TrFmt("Season %s frame", {LeagueRoman(frame.season)});
	return TrFmt("%s frame", {who});
}

std::string LeagueTitleLabel(const LeagueFrame &title)
{
	// A title is a rank's: without it there is nothing to write.
	if (!title.Shown() || (title.tier == LeagueTier::None && !title.founder)) return std::string();
	return LeagueSeasonTitle(title.tier, title.founder, title.seasonName, title.season);
}

COLORREF LeagueFrameInk(const LeagueFrame &frame)
{
	if (frame.tier != LeagueTier::None) return LeagueTierInk(frame.tier);
	return frame.founder ? Ref(LeagueFounderColors().text) : pal::textDim;
}

LeagueFrame LeagueOwnCosmetic(const LeagueMe &me, int season)
{
	const LeagueReward *r = season >= 0 ? LeagueRewardOf(me, season) : nullptr;
	return r && r->frame ? LeagueFrameOfReward(*r) : LeagueFrame();
}

LeagueBadge LeagueVexillumBadge(LeagueTier tier, bool founder, int season)
{
	LeagueBadge b;
	b.kind = LeagueBadgeKind::Season;
	b.id = std::to_string(season);
	b.season = season;
	b.tier = tier;
	b.founder = founder;
	return b;
}

int PaintLeagueShowcase(Painter &p, int x, int y, int w, int cell, const LeagueMe &me, int action, int mx, int my)
{
	const bool busy = LeagueRankedState().badgeBusy;
	const std::vector<LeagueBadgeChoice> choices = LeagueBadgeChoices(me);
	const int gap = 8, fit = std::max(0, (w + gap) / (cell + gap));
	int shown = 0;
	for (size_t i = 0; i < choices.size() && shown < fit; i++) {
		const LeagueBadgeChoice &c = choices[i];
		if (c.kind != "season" && c.kind != "trophy") continue;
		const int cx = x + shown * (cell + gap);
		const bool hot = !busy && !c.chosen && p.Hover(cx, y, cell, cell, mx, my);
		p.Fill(cx, y, cell, cell, c.chosen ? pal::selected : hot ? pal::raised : pal::panel);
		p.Frame(cx, y, cell, cell, c.chosen ? pal::redText : hot ? pal::edgeHot : pal::hairline);
		const int art = cell - 16;
		DrawLeagueBadge(p, cx + 8, y + 8, art, c.look);
		DrawLeagueBadgeCount(p, cx + 8, y + 8, art, c.look);
		if (c.chosen) p.Fill(cx + 1, y + cell - 4, cell - 2, 3, pal::red);
		HoverLeagueBadge(p, cx, y, cell, c.look, mx, my);
		if (!busy && !c.chosen) p.AddHit(cx, y, cell, cell, action, static_cast<int>(i));
		shown++;
	}
	return shown;
}

int PaintLeagueCosmeticChoices(Painter &p, int x, int y, int w, const LeagueMe &me, int frameAction, int titleAction, int mx, int my)
{
	const LeagueSeasonsView sv = LeagueSeasonsState();
	const std::vector<LeagueReward> won = LeagueTitleRewards(me);
	const bool enabled = !sv.cosmeticsBusy;
	int cy = y;
	PaintSectionLabel(p, x, cy, w, 22, "Avatar frame");
	cy += 30;
	if (won.empty()) {
		p.Font(3);
		const char *text = "Play 25 ranked games in a season to win its avatar frame and its title.";
		const int h = p.Measure(w, text);
		p.TextWrapped(x, cy, w, h + 2, text, pal::caption);
		return cy + h + 8 - y;
	}
	const uint64_t own = SteamOwnId();
	const int rowH = 44;
	ChoiceRow(p, x, cy, w, rowH, me.equippedFrame < 0, enabled, frameAction, -1, mx, my);
	p.Frame(x + 12, cy + 8, 28, 28, pal::edge);
	p.Font(me.equippedFrame < 0 ? 1 : 0);
	p.Text(x + 52, cy, w - 60, rowH, "No frame", me.equippedFrame < 0 ? pal::text : pal::body);
	cy += rowH + 2;
	for (const LeagueReward &r : won) {
		const LeagueFrame f = LeagueFrameOfReward(r);
		const bool chosen = me.equippedFrame == r.season;
		ChoiceRow(p, x, cy, w, rowH, chosen, enabled, frameAction, r.season, mx, my);
		PaintFramedAvatar(p, x + 10, cy + 6, 32, own, SteamOnlinePlayerName(), f);
		p.Font(chosen ? 1 : 0);
		TextAsIs(p, x + 52, cy, w - 60, rowH, LeagueFrameLabel(f) + "  \xC2\xB7  " + TrFmt("Season %s", {LeagueRoman(r.season)}), chosen ? pal::text : pal::body);
		cy += rowH + 2;
	}
	cy += 14;
	PaintSectionLabel(p, x, cy, w, 22, "Title under your name");
	cy += 30;
	const int titleH = 36;
	ChoiceRow(p, x, cy, w, titleH, me.equippedTitle < 0, enabled, titleAction, -1, mx, my);
	p.Font(me.equippedTitle < 0 ? 1 : 0);
	p.Text(x + 16, cy, w - 24, titleH, "No title", me.equippedTitle < 0 ? pal::text : pal::body);
	cy += titleH + 2;
	for (const LeagueReward &r : won) {
		const LeagueFrame t = LeagueFrameOfReward(r);
		const bool chosen = me.equippedTitle == r.season;
		ChoiceRow(p, x, cy, w, titleH, chosen, enabled, titleAction, r.season, mx, my);
		p.Font(4);
		p.raw = true;
		p.TextCaps(x + 16, cy, w - 24, titleH, LeagueTitleLabel(t), LeagueFrameInk(t), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 2);
		p.raw = false;
		cy += titleH + 2;
	}
	p.Font(3);
	if (sv.cosmeticsBusy) {
		p.Spinner(x + 11, cy + 16, 9, 4, GetTickCount());
		p.Text(x + 32, cy + 4, w - 32, 24, "Saving...", pal::textDim);
	}
	else if (!sv.cosmeticsError.empty()) p.Text(x, cy + 4, w, 24, sv.cosmeticsError, pal::bad, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	return cy + 30 - y;
}
