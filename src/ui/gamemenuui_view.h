// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "ui/uikit.h"

namespace gamemenuui {

enum Action { kActNone, kActButton, kActYes, kActNo };

struct Row {
	int id = 0;
	std::string label;
	bool enabled = true;
	bool strong = false;
	bool startsGroup = false;
};

constexpr int kStatsColumns = 3;

struct StatsPlayer {
	std::string name;
	COLORREF color = 0;
	std::string value[kStatsColumns];
	int percent[kStatsColumns] = {};
};

struct View {
	int mouseX = -1, mouseY = -1;
	std::string title = "MENU";
	int titleSpacing = 6;
	std::vector<std::string> notes;
	std::vector<Row> rows;
	std::string question;
	int statsPage = 0;
	std::string statsTime;
	std::string statsColumns[kStatsColumns];
	std::vector<StatsPlayer> statsPlayers;
};

constexpr int kMenuW = 460, kConfirmW = 560, kConfirmH = 220, kStatsW = 900, kStatsH = 560;
constexpr int kStatsClose = 8;

int MenuHeight(const View &v);
void PaintMenu(ui::Painter &p, const View &v);
void PaintConfirm(ui::Painter &p, const View &v);
void PaintStatistics(ui::Painter &p, const View &v);

}
