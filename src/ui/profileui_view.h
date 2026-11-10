// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/playerini.h"
#include "net/patrons.h"
#include "net/steamnet.h"
#include "ui/uikit.h"

namespace profileui {

struct RecentRow {
	int day = 0, month = 0, year = 0;
	bool won = false, multi = false;
	std::string civ;
	uint32_t durationMs = 0, kills = 0;
};

// One profile, with every text already in the player's language.
struct Card {
	std::string name;
	bool active = false;
	bool readable = true;
	ProfileSummary summary;
	std::string rankName, nextRankName, favoriteCiv, favoriteUnit, bestUnit;
	// Newest first.
	std::vector<RecentRow> recent;
};

struct View {
	std::vector<Card> cards;
	int selected = -1;
	int scroll = 0;
	int mouseX = -10000, mouseY = -10000;
	// Steam is the identity of the computer, so its avatar and laurel go only on the profile in use.
	std::shared_ptr<const OnlineAvatar> avatar;
	PatronInfo patron;
	// The ranks of the game's constants, which spread the colours of the ranks.
	int rankCount = 0;
};

enum Action { kActNone = 0, kActPick, kActUse, kActBack, kActPatron };

constexpr int kScreenW = 1100, kScreenH = 680;
// How many rows of the list fit; the controller scrolls by rows.
constexpr int kListRows = 7;
constexpr int kChartGames = 16;

// The player goes with the Steam account, so the list is left only for installs that still keep several from before.
inline bool ShowsList(const View &v) { return v.cards.size() > 1; }
inline bool SelectedIsActive(const View &v) { return v.selected >= 0 && v.selected < static_cast<int>(v.cards.size()) && v.cards[v.selected].active; }

void PaintProfileScreen(ui::Painter &p, const View &v);

}
