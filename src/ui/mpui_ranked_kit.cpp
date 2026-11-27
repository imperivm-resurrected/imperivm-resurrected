// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/leagueteam.h"
#include "core/tournament.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "net/patrons.h"
#include "ui/leaguerewards.h"
#include "ui/leagueui.h"
#include "ui/patronui.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_ranked_kit.h"

namespace mpui_impl {

void Display(Painter &p, int size)
{
	SelectObject(p.dc, BrandFont(BrandFace::Display, size, p.s));
}

void Body(Painter &p, int size, bool bold)
{
	SelectObject(p.dc, BrandFont(bold ? BrandFace::BodyBold : BrandFace::Body, size, p.s));
}

void Caps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt, int spacing)
{
	p.Font(4);
	p.TextCaps(x, y, w, h, text, c, fmt | DT_END_ELLIPSIS, spacing);
}

void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt)
{
	p.raw = true;
	p.Text(x, y, w, h, text, c, fmt);
	p.raw = false;
}

void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my)
{
	const bool hot = enabled && p.Hover(x, y, w, h, mx, my);
	const bool down = enabled && p.pressAction == kRankedAct && p.pressArg == arg;
	p.Well(x, y, w, h, hot || down);
	Caps(p, x, y, w, h, label, !enabled ? pal::disabled : hot || down ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	if (enabled) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void Chip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back)
{
	p.Fill(x, y, w, h, back);
	Caps(p, x, y, w, h, text, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
}

void Segment(Painter &p, int x, int y, int w, int h, const std::string &label, bool on, int arg, int mx, int my, int spacing)
{
	const bool hot = !on && p.Hover(x, y, w, h, mx, my);
	p.WellSegment(x, y, w, h, on, hot);
	Caps(p, x, y, w, h, label, on || hot ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, spacing);
	if (!on) p.AddHit(x, y, w, h, kRankedAct, arg);
}

void SummaryButton(Painter &p, int x, int y, int w, int mx, int my, UINT align)
{
	const bool hot = p.Hover(x, y, w, 44, mx, my);
	const COLORREF ink = hot ? pal::text : pal::textDim;
	p.Well(x, y, w, 44, hot);
	if (w < 100) PaintBackArrow(p, x + (w - 14) / 2, y + 22, ink);
	else {
		PaintBackArrow(p, x + 18, y + 22, ink);
		Caps(p, x + 18 + kBackArrowW, y, w - 26 - kBackArrowW, 44, "Summary", ink, align | DT_VCENTER | DT_SINGLELINE);
	}
	p.AddHit(x, y, w, 44, kRankedAct, kRkHub);
}

RankedBoardFrame RankedBoardFrameFor(int viewW, int viewH)
{
	const ScreenLayout l = ScreenLayoutFor(viewW, viewH, false);
	const int inner = l.w - 2 * l.margin;
	RankedBoardFrame f;
	f.detailW = std::min(537, std::max(400, inner * 537 / 1792));
	f.tableW = inner - f.detailW - 32;
	// Measured in the screen's type at scale 1, with both queues shown: the widest the bar gets in this language, with "Summary" down
	// to its arrow and the season's menu at its narrowest.
	HDC dc = CreateCompatibleDC(nullptr);
	if (!dc) return f;
	Painter p{dc, 1.0f, 0, 0, nullptr};
	UseScreenFonts(p);
	p.Font(4);
	auto chip = [&p](const std::string &text, int minW) { return std::max(minW, p.CapsWidth(text, 1) + 28); };
	p.raw = true;
	const int queue = 2 * std::max(chip(RankedQueueLabel(kLeagueQueueDuel), 96), chip(RankedQueueLabel(kLeagueQueueTeam), 96)) - 1;
	p.raw = false;
	const int seg = std::max(chip("All", 96), chip("Friends", 96));
	const int need = 56 + 16 + queue + 16 + 170 + 16 + 2 * seg + 16 + chip("My place", 130) + 12 + 2 * 48 + 8;
	DeleteDC(dc);
	if (need > f.tableW) f.secondRow = 52;
	return f;
}

void Avatar(Painter &p, int x, int y, int size, uint64_t id, const std::string &name)
{
	if (PaintAvatar(p, x, y, size, id)) return;
	p.Fill(x, y, size, size, pal::hairline);
	p.Font(size >= 48 ? 2 : 1);
	RawText(p, x, y, size, size, name.substr(0, std::min<size_t>(2, name.size())), pal::body, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

COLORREF Ink(LeagueTier t)
{
	return t == LeagueTier::None ? pal::caption : LeagueTierInk(t);
}

std::string RankName(LeagueTier t, int division)
{
	return t == LeagueTier::None ? Tr("Unranked") : LeagueRankLabel(t, division);
}

std::string RankOrPlacement(LeagueTier t, int division)
{
	return t == LeagueTier::None ? Tr("In placement") : LeagueRankLabel(t, division);
}

LeagueBadge RankBadge(LeagueTier t, int division)
{
	LeagueBadge b;
	if (t == LeagueTier::None) return b;
	b.kind = LeagueBadgeKind::Rank;
	b.tier = t;
	b.division = division;
	return b;
}

std::string SignedText(int v)
{
	return v > 0 ? "+" + std::to_string(v) : v < 0 ? "\xE2\x88\x92" + std::to_string(-v) : "0";
}

namespace {

// "Yes" or "No", translated; a dash for -1, what the season does not say.
std::string YesNo(int flag)
{
	return flag < 0 ? std::string("\xE2\x80\x94") : Tr(flag ? "Yes" : "No");
}

}

std::string RecordText(const LeaguePlayerCard &c)
{
	std::string text = std::to_string(c.wins) + "\xE2\x80\x93" + std::to_string(c.losses) + "  \xC2\xB7  " + std::to_string(LeagueWinRate(c.wins, c.losses)) + "\xC2\xA0%";
	if (c.streakKnown && c.streak) text += "  \xC2\xB7  " + LeagueStreakText(c.streak);
	return text;
}

std::string StandingText(const LeaguePlayerCard &c)
{
	std::string text = TrFmt("%s Glory", {std::to_string(c.glory)});
	if (c.place > 0) text += "  \xC2\xB7  " + TrFmt("place %s", {std::to_string(c.place)});
	return text;
}

std::vector<LeagueMatchTeam> MatchTeams(const LeagueRankedView &v)
{
	const std::vector<LeagueMatchTeam> teams = v.ticketLoaded ? LeagueTicketTeams(v.ticket) : std::vector<LeagueMatchTeam>();
	return teams.empty() ? v.found.teams : teams;
}

FoundBox PaintFoundBox(Painter &p, const State &st, int maxW, int maxH)
{
	FoundBox b;
	b.w = std::min(maxW, st.viewW - 40);
	b.h = std::min(maxH, st.viewH - 40);
	b.x = (st.viewW - b.w) / 2;
	b.y = (st.viewH - b.h) / 2;
	p.hits->clear();
	p.noHover = false;
	PaintTint(p, 0, 0, st.viewW, st.viewH, pal::bg, 219);
	p.Fill(b.x + 6, b.y + 6, b.w, b.h, RGB(8, 8, 7));
	p.Fill(b.x, b.y, b.w, b.h, pal::panel);
	p.Frame(b.x, b.y, b.w, b.h, pal::hairline);
	UseScreenFonts(p);
	return b;
}

std::array<MatchFact, 6> MatchFacts(const LeagueRoomSettings &s)
{
	return {{
		{"Preset", s.preset.empty() || s.preset == "competitive" ? Tr("Competitive") : s.preset},
		{"No fog", YesNo(s.fog < 0 ? -1 : !s.fog)},
		{"Explored map", YesNo(s.explored)},
		{"Civilizations", s.hideCivs == 1 ? Tr("Hidden until the game") : Tr("Visible")},
		{"Diplomacy", s.diplomacy == "locked" ? Tr("Locked") : s.diplomacy.empty() ? std::string("\xE2\x80\x94") : Tr("Free")},
		{"Spectators", s.specDelay == 0 ? Tr("No spectators") : TrFmt("With a %s s delay", {std::to_string(s.specDelay > 0 ? s.specDelay : 120)})},
	}};
}

void PaintEvenness(Painter &p, const LeagueRankedView &v, bool team, int x, int y, int w)
{
	const double expected = v.ticketLoaded && v.ticket.expected >= 0 ? v.ticket.expected : v.found.expected;
	const char *even = LeagueEvennessText(expected, team);
	if (!even) return;
	const COLORREF ink = std::string(even) == "Even match" ? pal::ok : expected > 0.5 ? pal::text : pal::warn;
	p.Font(1);
	if (p.TextWidth(even) > w) p.Font(3);
	p.Text(x, y, w, 24, even, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

int64_t LocalTime(int64_t serverMs)
{
	return serverMs - LeagueServerClockOffsetMs();
}

std::string DateText(int64_t ms, bool withTime)
{
	const time_t t = static_cast<time_t>(ms / 1000);
	tm local = {};
	if (localtime_s(&local, &t) != 0) return std::string();
	char buf[48];
	if (withTime) sprintf_s(buf, "%02d/%02d/%04d %02d:%02d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900, local.tm_hour, local.tm_min);
	else sprintf_s(buf, "%02d/%02d/%04d", local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
	return buf;
}

std::string TimeText(int64_t ms)
{
	const time_t t = static_cast<time_t>(ms / 1000);
	tm local = {};
	if (localtime_s(&local, &t) != 0) return std::string();
	char buf[16];
	sprintf_s(buf, "%02d:%02d", local.tm_hour, local.tm_min);
	return buf;
}


// A player's avatar (in their frame, when it shows) and name as the league shows them everywhere: the donor's laurel, the emblem they
// chose after the name, and their Steam profile on a click. A practice bot has none of them.
void PaintPlayerAvatar(Painter &p, int x, int y, int size, uint64_t id, const std::string &name)
{
	const LeagueFrame frame = id && !TourneyIsTestBot(id) ? PlayerFrameOf(id) : LeagueFrame();
	if (frame.Shown() && size >= 36) PaintFramedAvatar(p, x, y, size, id, name, frame);
	else if (!PaintAvatar(p, x, y, size, id)) p.Fill(x, y, size, size, pal::hairline);
}

int PaintPlayerName(Painter &p, const State &st, int x, int y, int w, int h, uint64_t id, const std::string &name)
{
	const UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
	const bool person = id && !TourneyIsTestBot(id);
	const PatronInfo patron = person ? PatronOf(id) : PatronInfo();
	const LeagueBadge badge = person ? PlayerBadgeOf(id) : LeagueBadge();
	const int laurel = patron.tier != PatronTier::None ? std::min(h, PatronLaurelSize(patron.tier)) : 0;
	const int nx = x + (laurel ? laurel + 4 : 0);
	p.raw = true;
	const int nameW = std::max(20, std::min(x + w - nx - LeagueBadgeRoom(badge), p.TextWidth(name) + 4));
	if (laurel) {
		const int ly = y + (h - laurel) / 2;
		DrawPatron(p, x, ly, laurel, nx, y, nameW, h, name, patron.tier, fmt);
		PatronHotspot(p, x, ly, laurel, patron, st.mouseX, st.mouseY, kPatronLink);
	}
	else p.Text(nx, y, nameW, h, name, pal::text, fmt);
	if (person) {
		DrawBadgeAfterName(p, nx, y, nameW, h, name, badge, st.mouseX, st.mouseY, id);
		SteamProfileHotspot(p, nx, y, nameW, h, name, id, st.mouseX, st.mouseY);
	}
	p.raw = false;
	return nx + nameW + (person ? LeagueBadgeRoom(badge) : 0);
}

}
