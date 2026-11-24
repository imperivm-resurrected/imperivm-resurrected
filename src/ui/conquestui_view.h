// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "game/conquest.h"
#include "ui/screenscale.h"
#include "ui/uikit.h"

namespace conquestui {

// kActNative presses the game's button `arg`; kActBack is the header's Back, the game's Quit.
enum Action { kActNone = -1, kActNative = 1, kActDifficulty, kActOk, kActBack };

constexpr int kBarDescription = 0;

struct View {
	ConquestSnapshot game;
	int mouseX = -1000, mouseY = -1000;
	int scroll = 0;
	ui::ScrollDrag bar;
	bool victory = false;
	std::string victoryText;
	// The game menu's screen draws the game's question (quit?) over this one.
	bool questionDrawn = false;
};

// Where the last paint put things, in screen pixels. `map` shows the part `crop` of the map's picture, in the picture's pixels.
struct Painted {
	std::vector<ui::Hit> hits;
	RECT map = {}, crop = {}, desc = {}, ok = {};
	int maxScroll = 0;
};

// The screen, in the units of ui::ScreenViewFor. The map keeps the shape of its picture and is as large as the room left by the right
// column and the path allows; the legend goes beside it when that leaves the map larger, under it otherwise.
struct Box {
	int x = 0, y = 0, w = 0, h = 0;
	int Right() const { return x + w; }
	int Bottom() const { return y + h; }
};

struct Layout {
	ui::ScreenLayout head;
	Box path, mapCard, map, legend, hint, column, territory, desc, bonus, difficulty, main, second;
	bool legendBeside = false;
};

constexpr int kGap = 24, kRowGap = 16, kPathH = 148, kMapPad = 8, kSideMin = 230, kSideMax = 320, kLegendRowH = 44, kLegendH = 150;
constexpr int kTerritoryH = 150, kBonusH = 118, kDifficultyH = 56, kMainH = 48, kSecondH = 40;

inline Layout LayoutFor(int w, int h, int picW, int picH)
{
	Layout l;
	l.head = ui::ScreenLayoutFor(w, h, false);
	const int m = l.head.margin, top = l.head.top, bottom = h - m;
	const int colW = std::max(400, std::min(480, w * 25 / 100)), leftW = w - 2 * m - colW - kGap;
	if (picW <= 0 || picH <= 0) {
		picW = 4;
		picH = 3;
	}
	l.path = {m, top, leftW, kPathH};
	const int areaTop = top + kPathH + kRowGap, areaH = bottom - areaTop;
	// The map's card is the picture and kMapPad around it.
	const auto fit = [&](int roomW, int roomH, Box &card) {
		int iw = roomW - 2 * kMapPad, ih = iw * picH / picW;
		if (ih > roomH - 2 * kMapPad) {
			ih = roomH - 2 * kMapPad;
			iw = ih * picW / picH;
		}
		card = {m, areaTop, iw + 2 * kMapPad, ih + 2 * kMapPad};
	};
	Box beside, under;
	fit(leftW - kGap - kSideMin, areaH, beside);
	fit(leftW, areaH - kRowGap - kLegendRowH, under);
	l.legendBeside = beside.w >= under.w;
	l.mapCard = l.legendBeside ? beside : under;
	l.map = {l.mapCard.x + kMapPad, l.mapCard.y + kMapPad, l.mapCard.w - 2 * kMapPad, l.mapCard.h - 2 * kMapPad};
	if (l.legendBeside) {
		const int sideX = l.mapCard.Right() + kGap, sideW = std::min(kSideMax, m + leftW - sideX);
		l.legend = {sideX, areaTop, sideW, kLegendH};
		l.hint = {sideX, l.legend.Bottom() + kRowGap, sideW, std::min(l.mapCard.Bottom(), bottom) - l.legend.Bottom() - kRowGap};
	}
	else {
		l.legend = {m, l.mapCard.Bottom() + kRowGap, l.mapCard.w, kLegendRowH};
	}
	l.column = {w - m - colW, top, colW, bottom - top};
	const int cx = l.column.x;
	l.territory = {cx, top, colW, kTerritoryH};
	l.second = {cx, bottom - kSecondH, colW, kSecondH};
	l.main = {cx, l.second.y - 10 - kMainH, colW, kMainH};
	l.difficulty = {cx, l.main.y - 18 - kDifficultyH, colW, kDifficultyH};
	l.bonus = {cx, l.difficulty.y - kRowGap - kBonusH, colW, kBonusH};
	l.desc = {cx, l.territory.Bottom() + 12, colW, l.bonus.y - 12 - (l.territory.Bottom() + 12)};
	return l;
}

// The pixel of the map's picture under a point of the screen, from the last paint; false off the map.
inline bool MapPixelAt(const Painted &p, POINT at, POINT &pixel)
{
	const int w = p.map.right - p.map.left, h = p.map.bottom - p.map.top;
	if (w <= 0 || h <= 0 || !PtInRect(&p.map, at)) return false;
	pixel.x = p.crop.left + static_cast<int>(static_cast<long long>(at.x - p.map.left) * (p.crop.right - p.crop.left) / w);
	pixel.y = p.crop.top + static_cast<int>(static_cast<long long>(at.y - p.map.top) * (p.crop.bottom - p.crop.top) / h);
	return true;
}

// The whole screen, opaque, over the game's dialog, which stays open under it for its logic; and the victory box over everything.
void PaintConquest(uint32_t *buf, int w, int h, const View &v, int pressAction, int pressArg, Painted &out);

}
