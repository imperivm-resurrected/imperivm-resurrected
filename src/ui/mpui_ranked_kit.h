// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/leagueranked.h"
#include "net/leagueclient.h"
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
// "Summary", the tool bar's button back to the hub, 44 tall: only its arrow when it is narrower than 100.
void SummaryButton(Painter &p, int x, int y, int w, int mx, int my, UINT align = DT_LEFT);
// Where the full leaderboard's table goes: right of it the player's card, and above it the tool bar, which takes a second row when its
// controls don't fit beside each other. The page size of the leaderboard follows it, so the paint and the request agree.
struct RankedBoardFrame {
	int tableW = 0, detailW = 0;
	// 0, or the height of the tool bar's second row.
	int secondRow = 0;
};
RankedBoardFrame RankedBoardFrameFor(int viewW, int viewH);
// The avatar, or the first letters of the name on a plain square.
void Avatar(Painter &p, int x, int y, int size, uint64_t id, const std::string &name);

// The colour of a rank, the caption's before there is one.
COLORREF Ink(LeagueTier t);
// "Centurion II", translated; before there is a rank, "Unranked" or "In placement".
std::string RankName(LeagueTier t, int division);
std::string RankOrPlacement(LeagueTier t, int division);
// The badge of a rank, none before there is one.
LeagueBadge RankBadge(LeagueTier t, int division);
// "+12", "−8" (with a minus sign) or "0".
std::string SignedText(int v);
// A player's record as the match found gives it: wins and losses, the share won, then the streak.
std::string RecordText(const LeaguePlayerCard &c);
// Their Glory, then their place when they have one.
std::string StandingText(const LeaguePlayerCard &c);
// The players of the 2v2 being played or found, from the ticket once it is read.
std::vector<LeagueMatchTeam> MatchTeams(const LeagueRankedView &v);

// The dialog of the match found, `maxW` x `maxH` at most and centred, over a screen whose clicks it takes all; where it went.
struct FoundBox {
	int x, y, w, h;
};
FoundBox PaintFoundBox(Painter &p, const State &st, int maxW, int maxH);
// The season's options as the match found lists them, a label over a value: the preset, the fog of war, the explored map, the
// civilizations, the diplomacy and the spectators. The labels are not translated yet.
struct MatchFact {
	const char *label;
	std::string value;
};
std::array<MatchFact, 6> MatchFacts(const LeagueRoomSettings &s);
// How even the match found is, from the chance the league gives this player's side, centred in `w`; nothing while it does not say.
void PaintEvenness(Painter &p, const LeagueRankedView &v, bool team, int x, int y, int w);

// A time of the league's server (its clock) as this computer's clock shows it.
int64_t LocalTime(int64_t serverMs);
// "15/02/2027", with "21:40" after it when `withTime`; and "21:40" alone. Of a time of this computer's clock.
std::string DateText(int64_t ms, bool withTime = false);
std::string TimeText(int64_t ms);

}
