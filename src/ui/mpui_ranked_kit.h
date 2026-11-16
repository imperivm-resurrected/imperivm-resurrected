// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>

#include "core/league.h"
#include "ui/mpui_internal.h"

// What the screens of the ranked tab draw with (the hub, the leaderboard, the history, the guide, the result, the season's ceremony and
// the match found): the brand's type, capitals, the buttons whose clicks are ranked actions, and the words of a rank and of a time.
namespace mpui_impl {

void Display(Painter &p, int size);
void Body(Painter &p, int size, bool bold = false);
// Capitals in the small strong type.
void Caps(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE, int spacing = 2);
// Text drawn as it comes, not translated again: names, and texts already translated.
void RawText(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF c,
	UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
// An outlined button whose click is the ranked action `arg`; a disabled one takes no click.
void Ghost(Painter &p, int x, int y, int w, int h, const std::string &label, int arg, bool enabled, int mx, int my);
void Chip(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF ink, COLORREF back);
// A button of a bar that picks one of a few, lit while it is the one on, which takes no click.
void Segment(Painter &p, int x, int y, int w, int h, const std::string &label, bool on, int arg, int mx, int my, int spacing = 2);

// The colour of a rank, the caption's before there is one.
COLORREF Ink(LeagueTier t);
// "Centurion II", translated; before there is a rank, "Unranked" or "In placement".
std::string RankName(LeagueTier t, int division);
std::string RankOrPlacement(LeagueTier t, int division);
// The badge of a rank, none before there is one.
LeagueBadge RankBadge(LeagueTier t, int division);
// "+12", "−8" (with a minus sign) or "0".
std::string SignedText(int v);
// "Yes" or "No", translated; a dash for -1, what the season does not say.
std::string YesNo(int flag);

// A time of the league's server (its clock) as this computer's clock shows it.
int64_t LocalTime(int64_t serverMs);
// "15/02/2027", with "21:40" after it when `withTime`; and "21:40" alone. Of a time of this computer's clock.
std::string DateText(int64_t ms, bool withTime = false);
std::string TimeText(int64_t ms);

}
