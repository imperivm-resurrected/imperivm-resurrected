// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/uikit.h"

namespace diplomacyui {

enum Action { kActNone, kActPreset, kActToggle, kActAccept, kActAllied, kActApply, kActCancel, kActMuteChat, kActMuteMarks };

constexpr int kBarCards = 0;

struct Card {
	std::string name;
	uint32_t rgb = 0;
	bool ai = false;
	int team = 0;
	unsigned mine = 0, theirs = 0;
	// The player's index in the match, and what this computer hides of them (the chat's /mute and /mutepings).
	int player = -1;
	bool mutedChat = false, mutedMarks = false;
};

struct View {
	int mouseX = -1, mouseY = -1;
	std::vector<Card> cards;
	bool alliedVictory = false;
	bool locked = false;
	int scroll = 0;  // in rows of cards
	ui::ScrollDrag bar;
};

constexpr int kPanelW = 1000;
// Preset and toggle hits carry the card times kArgStride plus the preset or the permission's position.
constexpr int kArgStride = 8;

// fullHeight: as tall as with two rows of cards, which sets one scale for every number of players.
int PanelHeight(const View &v, bool fullHeight = false);
int MaxScroll(const View &v);
void PaintDiplomacy(ui::Painter &p, const View &v);

}
