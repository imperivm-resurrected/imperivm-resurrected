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

struct View {
	int mouseX = -1, mouseY = -1;
	std::vector<Row> rows;
	std::string question;
};

constexpr int kMenuW = 420, kConfirmW = 560, kConfirmH = 220;

int MenuHeight(const View &v);
void PaintMenu(ui::Painter &p, const View &v);
void PaintConfirm(ui::Painter &p, const View &v);

}
