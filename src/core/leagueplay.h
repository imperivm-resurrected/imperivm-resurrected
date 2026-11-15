// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/leagueranked.h"

// What ranked play needs around the game itself: each player's own pauses, the words for the evenness of a match and for the search
// window, and the presence friends see. Nothing here talks to the network or the game.

// A ranked game lets each player pause 3 times and 3 minutes in all (2-clasificatoria-sistema.md §2.3), or what the season says.
constexpr int kLeaguePauseCount = 3;
constexpr int64_t kLeaguePauseTotalMs = 3 * 60 * 1000;

// One player's pauses in a ranked game, counted on their own computer: how many they took, for how long, and the one on now (since
// `since`, this computer's clock).
struct LeaguePauseBudget {
	int count = kLeaguePauseCount;
	int64_t totalMs = kLeaguePauseTotalMs;
	int used = 0;
	int64_t usedMs = 0;
	bool active = false;
	int64_t since = 0;
};
// The limits of the room's settings (settings.pauses), the defaults where they say nothing.
LeaguePauseBudget LeaguePauseBudgetOf(const LeagueRoomSettings &s);
// Another pause may start: none of theirs is on, and they have pauses and time left.
bool LeaguePauseCanStart(const LeaguePauseBudget &b);
// A pause of theirs began, or ended. Starting one while one is on, or ending one that is not, changes nothing.
void LeaguePauseStarted(LeaguePauseBudget &b, int64_t nowMs);
void LeaguePauseEnded(LeaguePauseBudget &b, int64_t nowMs);
// The time of pause they have left, counting the one on; never negative.
int64_t LeaguePauseLeftMs(const LeaguePauseBudget &b, int64_t nowMs);
// Their pause is on and their time is over: it has to be lifted.
bool LeaguePauseRunOut(const LeaguePauseBudget &b, int64_t nowMs);

// How even a found match is, from this player's chance to win: "Even match" from 40 to 60 %, otherwise who is the favourite. The
// English text, the key of its translation; null when the server did not say (a chance outside 0 to 1).
const char *LeagueEvennessText(double expected);

// A text to translate and the values that fill its %s.
struct LeagueText {
	const char *text = nullptr;
	std::vector<std::string> args;
};
// The line under the search: "Opponents between %s and %s Glory", the one end the window has, or any Glory once it holds none. No text
// without a window.
LeagueText LeagueWindowText(const LeagueSearchWindow &w);

// What friends see on Steam and Discord: a ranked game first, then a search (a match found that waits for answers is still one).
enum class LeaguePresence { None, Searching, Playing };
LeaguePresence LeaguePresenceOf(bool searching, bool rankedGame);
// "Searching ranked 1 vs 1" or "Ranked 1 vs 1", to translate; null for None.
const char *LeaguePresenceLabel(LeaguePresence p);
// The label and the rank, both already translated ("Clasificatoria 1 contra 1 · Centurión II"); the label alone without a rank.
std::string LeaguePresenceText(const std::string &label, const std::string &rank);
