// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/gamemenuui_view.h"

#include <cstdlib>

#include "ui/widgets.h"

using namespace ui;

namespace gamemenuui {

namespace {
constexpr int kMarginX = 40, kButtonH = 42, kButtonGap = 8, kGroupGap = 18, kBottomPad = 26, kNoteH = 44;

void DrawCentered(Painter &p, RECT area, const std::string &text, COLORREF color)
{
	const std::wstring wide = W(Tr(text));
	RECT measured = area;
	DrawTextW(p.dc, wide.c_str(), -1, &measured, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	const int textH = measured.bottom - measured.top;
	if (textH < area.bottom - area.top) area.top += (area.bottom - area.top - textH) / 2;
	SetTextColor(p.dc, color);
	SetBkMode(p.dc, TRANSPARENT);
	DrawTextW(p.dc, wide.c_str(), -1, &area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
}

int ColumnCount(const View &v)
{
	int count = 0;
	for (const std::string &column : v.statsColumns) count += column.empty() ? 0 : 1;
	return count;
}
}

int MenuHeight(const View &v)
{
	int h = kWindowHeaderH + kBottomPad - kButtonGap + static_cast<int>(v.notes.size()) * kNoteH;
	for (const Row &r : v.rows) h += kButtonH + kButtonGap + (r.startsGroup ? kGroupGap : 0);
	return h;
}

void PaintMenu(Painter &p, const View &v)
{
	PaintWindowFrame(p, kMenuW, MenuHeight(v));
	int y = PaintWindowHeader(p, kMenuW, v.title, v.titleSpacing);
	for (const std::string &note : v.notes) {
		p.Font(3);
		DrawCentered(p, p.R(kMarginX - 10, y, kMenuW - (kMarginX - 10) * 2, kNoteH - 6), note, kText);
		y += kNoteH;
	}
	for (const Row &r : v.rows) {
		if (r.startsGroup) y += kGroupGap;
		p.Button(kMarginX, y, kMenuW - kMarginX * 2, kButtonH, r.label, kActButton, r.id, v.mouseX, v.mouseY, r.enabled, r.strong);
		y += kButtonH + kButtonGap;
	}
}

void PaintConfirm(Painter &p, const View &v)
{
	constexpr int kPad = 36, kTextTop = 30, kRuleY = 136, kButtonW = 150, kButtonsY = 158, kButtonsGap = 16;
	PaintWindowFrame(p, kConfirmW, kConfirmH);
	p.Font(1);
	DrawCentered(p, p.R(kPad, kTextTop, kConfirmW - kPad * 2, kRuleY - kTextTop - 8), v.question, pal::goldLight);
	PaintOrnateRule(p, kRuleY, kConfirmW);
	const int x = (kConfirmW - kButtonW * 2 - kButtonsGap) / 2;
	p.Button(x, kButtonsY, kButtonW, kButtonH, "Yes", kActYes, 0, v.mouseX, v.mouseY, true, true);
	p.Button(x + kButtonW + kButtonsGap, kButtonsY, kButtonW, kButtonH, "No", kActNo, 0, v.mouseX, v.mouseY);
}

void PaintStatistics(Painter &p, const View &v)
{
	constexpr int kPad = 24, kTabsY = 86, kTabH = 36, kTableY = 134, kHeadH = 34, kRowH = 40, kFooterY = 500, kNameW = 230, kColGap = 18;
	PaintWindowFrame(p, kStatsW, kStatsH);
	PaintWindowHeader(p, kStatsW, "STATISTICS");

	const std::vector<TabSpec> tabs = {{"Resources", kActButton, 4, v.statsPage == 4, true},
		{"Gold production", kActButton, 5, v.statsPage == 5, true},
		{"Units", kActButton, 6, v.statsPage == 6, true},
		{"Scores", kActButton, 7, v.statsPage == 7, true}};
	const int tabW = (kStatsW - kPad * 2 - 6 * 3) / 4;
	PaintTabs(p, kPad, kTabsY, tabs, v.mouseX, v.mouseY, tabW, kTabH);
	p.Fill(kPad, kTabsY + kTabH, kStatsW - kPad * 2, 1, pal::line);

	const int tableW = kStatsW - kPad * 2, rows = static_cast<int>(v.statsPlayers.size());
	PaintCard(p, kPad, kTableY, tableW, kHeadH + std::max(1, rows) * kRowH + 6);

	const int columns = std::max(1, ColumnCount(v));
	const int colX = kPad + 16 + kNameW, colW = (tableW - 32 - kNameW - kColGap * (columns - 1)) / columns;
	int best[kStatsColumns] = {};
	for (const StatsPlayer &player : v.statsPlayers) {
		for (int c = 0; c < kStatsColumns; c++) best[c] = std::max(best[c], std::atoi(player.value[c].c_str()));
	}

	p.Font(4);
	p.Text(kPad + 16, kTableY + 8, kNameW, 20, "Player", kGold);
	for (int c = 0, shown = 0; c < kStatsColumns; c++) {
		if (v.statsColumns[c].empty()) continue;
		p.Text(colX + shown * (colW + kColGap), kTableY + 8, colW, 20, v.statsColumns[c], kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		shown++;
	}

	int y = kTableY + kHeadH;
	for (const StatsPlayer &player : v.statsPlayers) {
		p.Fill(kPad + 14, y, tableW - 28, 1, RGB(48, 37, 23));
		p.Fill(kPad + 16, y + 13, 16, 16, player.color);
		p.Frame(kPad + 16, y + 13, 16, 16, RGB(10, 8, 6));
		p.Font(0);
		p.Text(kPad + 42, y + 2, kNameW - 30, kRowH - 4, player.name, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		for (int c = 0, shown = 0; c < kStatsColumns; c++) {
			if (v.statsColumns[c].empty()) continue;
			const int x = colX + shown * (colW + kColGap);
			shown++;
			const int value = std::atoi(player.value[c].c_str());
			const bool leader = value > 0 && value == best[c];
			p.Font(leader ? 1 : 0);
			p.Text(x, y + 4, colW / 2, 22, player.value[c], leader ? pal::goldLight : kText);
			p.Font(3);
			p.Text(x + colW / 2, y + 4, colW - colW / 2, 22, std::to_string(player.percent[c]) + " %", kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			p.Fill(x, y + 28, colW, 5, pal::well);
			const int filled = colW * std::max(0, std::min(100, player.percent[c])) / 100;
			if (filled > 0) p.FillH(x, y + 28, filled, 5, pal::bronze, leader ? pal::goldLight : kGold);
		}
		y += kRowH;
	}

	p.Fill(18, kFooterY, kStatsW - 36, 1, pal::line);
	p.Font(0);
	p.Text(kPad + 4, kFooterY + 10, 500, 40, v.statsTime, kDim);
	p.Button(kStatsW - kPad - 160, kFooterY + 10, 160, 40, "Close", kActButton, kStatsClose, v.mouseX, v.mouseY, true, true);
}

}
