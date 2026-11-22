// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguemastery.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "game/civs.h"
#include "game/gametext.h"
#include "i18n/tr.h"
#include "ui/leagueui.h"
#include "ui/patronui.h"

using namespace ui;

namespace {

struct Tip {
	bool show = false;
	int x = 0, y = 0;
	std::string civ;
	LeagueMastery mastery;
};

Tip g_tip;

}

std::string LeagueCivName(const std::string &id)
{
	if (id.empty()) return std::string();
	if (const Civ *c = CivByName(id)) return CivDisplayName(*c);
	// The base civilizations are no mod civilizations: the game names them in its language.
	const int race = GameCivByName(id);
	if (race >= 0) {
		const std::string name = GameRaceText(race);
		if (!name.empty()) return name;
	}
	std::string s = id;
	std::replace(s.begin(), s.end(), '_', ' ');
	s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
	return s;
}

std::string LeagueMasteryLabel(int level)
{
	return Tr(LeagueMasteryName(level));
}

COLORREF LeagueMasteryInk(int level)
{
	switch (std::max(0, std::min(kLeagueMasteryTop, level))) {
	case 0: return RGB(0x9a, 0x96, 0x8e);
	case 1: return LeagueMetalInk(LeagueMetal::Bronze);
	case 2: return LeagueMetalInk(LeagueMetal::Silver);
	default: return LeagueMetalInk(LeagueMetal::Gold);
	}
}

void DrawMasteryBadge(Painter &p, int x, int y, int size, int level)
{
	const COLORREF ink = LeagueMasteryInk(level);
	const int cx = x + size / 2, cy = y + size / 2;
	if (level >= kLeagueMasteryTop) {
		PaintWreath(p, x, y, size, ink);
		const int r = std::max(3, size * 22 / 100);
		p.Diamond(cx, cy, r, ink);
		p.Diamond(cx, cy, std::max(1, r - std::max(2, size / 12)), pal::bg);
		p.Diamond(cx, cy, std::max(1, r / 2), ink);
		return;
	}
	const int r = std::max(4, size / 2 - 1), edge = std::max(2, size / 9);
	p.Diamond(cx, cy, r, ink);
	p.Diamond(cx, cy, std::max(1, r - edge), pal::bg);
	// The gem grows with the level, so the badges tell apart in grey too.
	p.Diamond(cx, cy, std::max(1, (r - edge) * (2 + level) / 6), ink);
}

void PaintMasteryBar(Painter &p, int x, int y, int w, int h, const LeagueMastery &m, int seasonWins)
{
	const COLORREF ink = LeagueMasteryInk(m.level);
	p.Fill(x, y, w, h, pal::bg);
	const int fill = static_cast<int>(w * LeagueMasteryProgress(m));
	p.Fill(x, y, fill, h, ink);
	if (m.Top() || m.nextWins <= m.levelWins || seasonWins <= 0) return;
	// The wins of this season that are inside the level, lighter at the end of the fill.
	const int inside = std::min(seasonWins, m.wins - m.levelWins);
	const int part = w * inside / (m.nextWins - m.levelWins);
	if (part > 0) PaintTint(p, x + fill - part, y, part, h, RGB(255, 255, 255), 70);
}

std::string LeagueMasteryNextText(const LeagueMastery &m)
{
	if (m.Top()) return Tr("Top mastery");
	const int left = LeagueMasteryWinsLeft(m);
	const std::string next = LeagueMasteryLabel(m.level + 1);
	return left == 1 ? TrFmt("1 more win for %s", {next}) : TrFmt("%s more wins for %s", {std::to_string(left), next});
}

void HoverMastery(Painter &p, int x, int y, int size, const std::string &civ, const LeagueMastery &m, int mx, int my)
{
	if (!m.present || !p.Hover(x, y, size, size, mx, my)) return;
	g_tip.show = true;
	g_tip.x = x;
	g_tip.y = y + size + 6;
	g_tip.civ = civ;
	g_tip.mastery = m;
}

void PaintMasteryTip(Painter &p, int screenW, int screenH)
{
	if (!g_tip.show) return;
	g_tip.show = false;
	const LeagueMastery &m = g_tip.mastery;
	const COLORREF ink = LeagueMasteryInk(m.level);
	const std::string title = TrFmt("%s with %s", {LeagueMasteryLabel(m.level), LeagueCivName(g_tip.civ)});
	const std::string wins = m.wins == 1 ? Tr("1 ranked win") : TrFmt("%s ranked wins", {std::to_string(m.wins)});
	const std::string line = wins + "  \xC2\xB7  " + LeagueMasteryNextText(m);
	p.raw = true;
	p.Font(1);
	int w = p.TextWidth(title);
	p.Font(3);
	w = std::max(w, p.TextWidth(line)) + 24;
	const int h = 58;
	const int x = std::max(4, std::min(g_tip.x, screenW - w - 4));
	const int y = g_tip.y + h > screenH - 4 ? std::max(4, g_tip.y - h - 40) : g_tip.y;
	p.Fill(x, y, w, h, pal::panel);
	p.Frame(x, y, w, h, ink);
	p.Fill(x, y, 3, h, ink);
	p.Font(1);
	p.Text(x + 12, y + 8, w - 20, 20, title, ink, DT_LEFT | DT_TOP | DT_SINGLELINE);
	p.Font(3);
	p.Text(x + 12, y + 31, w - 20, 18, line, pal::caption, DT_LEFT | DT_TOP | DT_SINGLELINE);
	p.raw = false;
}
