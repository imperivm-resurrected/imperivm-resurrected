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
#include "ui/scrollpane.h"
#include "ui/uikit.h"

namespace profileui {

// A recorded game, with its texts already in the player's language.
struct GameRow {
	int day = 0, month = 0, year = 0, hour = 0, minute = 0;
	bool won = false, multi = false;
	// MatchLeague: a league or a cup game.
	int league = 0;
	// The civilization's number (-1 unknown, 10 or higher from a mod) and its name.
	int race = -1;
	std::string civ, bestUnit;
	uint32_t durationMs = 0, kills = 0, losses = 0, produced = 0, armyMax = 0, gold = 0, food = 0, bestLevel = 0;
	uint32_t damageDealt = 0, damageTaken = 0;
	// From this computer's note of the game (MatchNote): -1 when it has none.
	int64_t goldSpent = -1, scriptKills = -1, scriptDeaths = -1;
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
	// The league account of the player in use: its standing, its rewards and the frame and title it wears.
	bool linked = false, meLoaded = false;
	LeagueMe me;
	// The league account's dialog is open over the screen.
	bool leagueOpen = false;
	// The tab shown: the player's games (kTabGeneral) or their league (kTabLeague), which only the player in use has.
	int tab = 0;
	// The ranks of the game's constants, which spread the colours of the ranks.
	int rankCount = 0;
	int metric = kMetricKills, chartMode = kModeAll, chartRange = kRange20;
	int outcome = kOutcomeAll, historyScroll = 0;
	// The civilization of the civilizations tab (kCivAll for every one), and the queue of the league tab's chart of Glory (0 the duel).
	int civ = kCivAll, gloryQueue = 0;
	// The position in the selected card's games of the one opened in the history, or -1.
	int expanded = -1;
	int todayYear = 0, todayMonth = 0, todayDay = 0;
	// The size of the screen last painted, in its design units (ScreenViewFor): the layout, and so the scrolls, follow it.
	int screenW = 1920, screenH = 1080;
	// The league dialog's body and the league tab's lists, when they hold more than their room.
	ui::PaneScrolls panes;
};

enum Action {
	kActNone = 0, kActPick, kActUse, kActBack, kActPatron, kActMetric, kActChartMode, kActChartRange, kActOutcome, kActBar, kActGame,
	kActHistory, kActLeague, kActLeagueClose, kActLeagueLink, kActLeagueOpen, kActLeagueCancel, kActLeagueUnlink, kActLeagueBadge,
	kActLeagueFrame, kActLeagueTitle, kActTab, kActCiv, kActGloryQueue,
};

constexpr int kTabGeneral = 0, kTabLeague = 1, kTabCivs = 2;

constexpr int kBarList = 0, kBarHistory = 1;
constexpr int kPaneLeagueDialog = ui::kPaneIdBase, kPaneTrophies = ui::kPaneIdBase + 1, kPaneSeasons = ui::kPaneIdBase + 2, kPaneCivs = ui::kPaneIdBase + 3;

// The player goes with the Steam account, so the list is left only for installs that still keep several from before.
inline bool ShowsList(const View &v) { return v.cards.size() > 1; }
inline bool SelectedIsActive(const View &v) { return v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) && v.cards[v.selected].active; }
inline int ShownTab(const View &v)
{
	if (v.tab == kTabCivs) return kTabCivs;
	return v.tab == kTabLeague && SelectedIsActive(v) ? kTabLeague : kTabGeneral;
}
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
