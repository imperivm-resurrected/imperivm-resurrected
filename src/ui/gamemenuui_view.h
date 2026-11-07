// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/gamestats.h"
#include "ui/uikit.h"

namespace gamemenuui {

enum Action { kActNone, kActButton, kActYes, kActNo, kActCategory, kActChart, kActMetric, kActRange, kActGroup, kActPlayer, kActSort };

struct Row {
	int id = 0;
	std::string label;
	bool enabled = true;
	bool strong = false;
	bool startsGroup = false;
};

struct StatsFilters {
	int category = 0, metric = 0, range = 0, group = 0;
	bool chart = true;
	uint32_t hidden = 0;
	int sortColumn = -1;
	bool sortAscending = false;
};

struct View {
	int mouseX = -1, mouseY = -1;
	std::string title = "MENU";
	int titleSpacing = 6;
	std::vector<std::string> notes;
	std::vector<Row> rows;
	std::string question;
	StatsFilters stats;
	std::vector<int> statsPlayers;
	std::shared_ptr<const StatsSnapshot> statsData;
};

constexpr int kMenuW = 460, kConfirmW = 560, kConfirmH = 220, kStatsW = 1040, kStatsH = 680;
constexpr int kStatsClose = 8;

int MenuHeight(const View &v);
void PaintMenu(ui::Painter &p, const View &v);
void PaintConfirm(ui::Painter &p, const View &v);
void PaintStatistics(ui::Painter &p, const View &v);

}
