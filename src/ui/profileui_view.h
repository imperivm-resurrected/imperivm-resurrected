// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/league.h"
#include "core/matchhistory.h"
#include "game/playerini.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/uikit.h"

namespace profileui {

// A recorded game, with its texts already in the player's language.
struct GameRow {
	int day = 0, month = 0, year = 0, hour = 0, minute = 0;
	bool won = false, multi = false;
	std::string civ, bestUnit;
	uint32_t durationMs = 0, kills = 0, losses = 0, produced = 0, armyMax = 0, gold = 0, food = 0, bestLevel = 0;
	uint32_t damageDealt = 0, damageTaken = 0;
};

MatchFacts FactsOf(const GameRow &g);

// One profile, with every text already in the player's language.
struct Card {
	std::string name;
	bool active = false;
	bool readable = true;
	ProfileSummary summary;
	std::string rankName, nextRankName, favoriteCiv, favoriteUnit, bestUnit;
	// Every recorded game, newest first.
	std::vector<GameRow> games;
};

enum ChartRange { kRange20, kRange50, kRangeAll };

struct View {
	std::vector<Card> cards;
	int selected = -1;
	int scroll = 0;
	ui::ScrollDrag bar;
	int mouseX = -10000, mouseY = -10000;
	// Steam is the identity of the computer, so its avatar and laurel go only on the profile in use.
	std::shared_ptr<const OnlineAvatar> avatar;
	PatronInfo patron;
	LeagueBadge badge;
	// The ranks of the game's constants, which spread the colours of the ranks.
	int rankCount = 0;
	int metric = kMetricKills, chartMode = kModeAll, chartRange = kRange20;
	int outcome = kOutcomeAll, historyScroll = 0;
	// The position in the selected card's games of the one opened in the history, or -1.
	int expanded = -1;
	int todayYear = 0, todayMonth = 0, todayDay = 0;
	// The size of the screen last painted, in its design units (ScreenViewFor): the layout, and so the scrolls, follow it.
	int screenW = 1920, screenH = 1080;
};

enum Action {
	kActNone = 0, kActPick, kActUse, kActBack, kActPatron, kActMetric, kActChartMode, kActChartRange, kActOutcome, kActBar, kActGame,
	kActHistory,
};

constexpr int kBarList = 0, kBarHistory = 1;

// The player goes with the Steam account, so the list is left only for installs that still keep several from before.
inline bool ShowsList(const View &v) { return v.cards.size() > 1; }
inline bool SelectedIsActive(const View &v) { return v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) && v.cards[v.selected].active; }
const Card *SelectedCard(const View &v);
// How many rows of the list fit; the controller scrolls by rows.
int ListRows(const View &v);

// The games of the history, as positions in the selected card's games, newest first.
std::vector<size_t> HistoryGames(const View &v);
// How far the history scrolls, in rows, with the open game's detail counted in.
int HistoryMaxScroll(const View &v);
// Opens a game in the history and scrolls to it, showing every game if the filter hid it.
void HistoryReveal(View &v, int game);

void PaintProfileScreen(ui::Painter &p, const View &v);

}
