// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueplay.h"

#include <algorithm>

LeaguePauseBudget LeaguePauseBudgetOf(const LeagueRoomSettings &s)
{
	LeaguePauseBudget b;
	if (s.pauseCount >= 0) b.count = s.pauseCount;
	if (s.pauseSeconds >= 0) b.totalMs = static_cast<int64_t>(s.pauseSeconds) * 1000;
	return b;
}

bool LeaguePauseCanStart(const LeaguePauseBudget &b)
{
	return !b.active && b.used < b.count && b.usedMs < b.totalMs;
}

void LeaguePauseStarted(LeaguePauseBudget &b, int64_t nowMs)
{
	if (b.active) return;
	b.active = true;
	b.since = nowMs;
	b.used++;
}

void LeaguePauseEnded(LeaguePauseBudget &b, int64_t nowMs)
{
	if (!b.active) return;
	b.active = false;
	b.usedMs += std::max<int64_t>(0, nowMs - b.since);
}

int64_t LeaguePauseLeftMs(const LeaguePauseBudget &b, int64_t nowMs)
{
	const int64_t on = b.active ? std::max<int64_t>(0, nowMs - b.since) : 0;
	return std::max<int64_t>(0, b.totalMs - b.usedMs - on);
}

bool LeaguePauseRunOut(const LeaguePauseBudget &b, int64_t nowMs)
{
	return b.active && LeaguePauseLeftMs(b, nowMs) == 0;
}

const char *LeagueEvennessText(double expected)
{
	if (!(expected >= 0 && expected <= 1)) return nullptr;
	// The server's chance is a double: 0.4 and 0.6 themselves are even.
	const double margin = 1e-9;
	if (expected >= 0.4 - margin && expected <= 0.6 + margin) return "Even match";
	return expected > 0.6 ? "You are the favourite" : "Your opponent is the favourite";
}

LeagueText LeagueWindowText(const LeagueSearchWindow &w)
{
	LeagueText t;
	if (!w.present) return t;
	const bool low = w.from > 0, high = w.to >= 0;
	if (low && high) {
		t.text = "Opponents between %s and %s Glory";
		t.args = {std::to_string(std::min(w.from, w.to)), std::to_string(std::max(w.from, w.to))};
	}
	else if (low) {
		t.text = "Opponents from %s Glory";
		t.args = {std::to_string(w.from)};
	}
	else if (high) {
		t.text = "Opponents up to %s Glory";
		t.args = {std::to_string(w.to)};
	}
	else t.text = "Any opponent";
	return t;
}

LeaguePresence LeaguePresenceOf(bool searching, bool rankedGame)
{
	if (rankedGame) return LeaguePresence::Playing;
	return searching ? LeaguePresence::Searching : LeaguePresence::None;
}

const char *LeaguePresenceLabel(LeaguePresence p)
{
	switch (p) {
	case LeaguePresence::Searching: return "Searching ranked 1 vs 1";
	case LeaguePresence::Playing: return "Ranked 1 vs 1";
	default: return nullptr;
	}
}

std::string LeaguePresenceText(const std::string &label, const std::string &rank)
{
	if (rank.empty()) return label;
	return label + " \xC2\xB7 " + rank;
}
