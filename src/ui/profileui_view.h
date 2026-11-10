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
#include "ui/textedit.h"
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
	int saves = 0;
	// Newest first.
	std::vector<RecentRow> recent;
};

enum Modal { kModalNone, kModalNew, kModalRename, kModalDelete };

struct View {
	std::vector<Card> cards;
	int selected = -1;
	int scroll = 0;
	int mouseX = -10000, mouseY = -10000;
	int modal = kModalNone;
	std::string text;
	ui::EditState edit;
	// Steam is the identity of the computer, so its avatar and laurel go only on the profile in use.
	std::shared_ptr<const OnlineAvatar> avatar;
	std::string steamName;
	PatronInfo patron;
};

enum Action {
	kActNone = 0, kActPick, kActNew, kActRename, kActDelete, kActPlay, kActBack, kActModalOk, kActModalCancel, kActField, kActPatron,
};

constexpr int kScreenW = 1100, kScreenH = 680;
// How many rows of the list fit; the controller scrolls by rows.
constexpr int kListRows = 7;
constexpr int kRecentRows = 5, kChartGames = 16;

std::string NameDraft(const std::string &text);
void PaintProfileScreen(ui::Painter &p, const View &v);

}
