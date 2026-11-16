// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "core/league.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "ui/leagueui.h"

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
	if (down) p.Fill(x, y, w, h, pal::raised);
	p.Frame(x, y, w, h, !enabled ? pal::hairline : hot || down ? pal::edgeHot : pal::edge);
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
	if (on) p.Fill(x, y, w, h, pal::selected);
	p.Frame(x, y, w, h, on || hot ? pal::edgeHot : pal::edge);
	Caps(p, x, y, w, h, label, on || hot ? pal::text : pal::caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE, spacing);
	if (!on) p.AddHit(x, y, w, h, kRankedAct, arg);
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

std::string YesNo(int flag)
{
	return flag < 0 ? std::string("\xE2\x80\x94") : Tr(flag ? "Yes" : "No");
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

}
