// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/gamemenuui_view.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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

struct Category {
	const char *name;
	struct Metric {
		StatMetric id;
		const char *label;
	} metrics[4];
};

const Category kCategories[] = {
	{"Score", {{kStatScore, "Overall"}, {kStatMilitary, "Military"}, {kStatDevelopment, "Development"}, {kStatArmyStrength, "Army strength"}}},
	{"Army", {{kStatUnitsAlive, "Units alive"}, {kStatUnitsBuilt, "Units built"}, {kStatUnitsMax, "Largest army"}, {kStatLevelMax, "Highest level"}}},
	{"Combat", {{kStatKilled, "Killed"}, {kStatLost, "Lost"}, {kStatDamageDealt, "Damage dealt"}, {kStatDamageTaken, "Damage taken"}}},
	{"Resources", {{kStatGoldHeld, "Gold held"}, {kStatFoodHeld, "Food held"}, {kStatGoldSpent, "Gold spent"}, {kStatFoodSpent, "Food spent"}}},
	{"Gold", {{kStatGoldTaxes, "From taxes"}, {kStatGoldCaptured, "Captured"}, {kStatGoldOther, "Other"}, {kStatGoldTotal, "Total gold"}}},
};

const uint32_t kRangeMs[] = {0, 10 * 60000, 5 * 60000};
const char *const kRangeLabels[] = {"All game", "Last 10 min", "Last 5 min"};
const char *const kGroupLabels[] = {"All", "Humans", "My team", "Rivals"};

constexpr int kPad = 24, kRailW = 216, kMainX = kPad + kRailW + 16, kMainW = kStatsW - kMainX - kPad, kFooterY = 622;

COLORREF ColorOf(const StatsPlayerInfo &info) { return RGB(info.rgb >> 16 & 255, info.rgb >> 8 & 255, info.rgb & 255); }

bool InGroup(const StatsPlayerInfo &info, int group)
{
	switch (group) {
	case 1: return info.human;
	case 2: return info.local || info.ally;
	case 3: return !info.local && !info.ally;
	default: return true;
	}
}

// Dark colours would vanish on the panel, so the lines of the chart get lifted a little.
COLORREF LineColor(COLORREF c)
{
	const int light = GetRValue(c) * 3 + GetGValue(c) * 6 + GetBValue(c);
	if (light >= 900) return c;
	const int lift = (900 - light) / 10;
	return RGB(std::min(255, GetRValue(c) + lift), std::min(255, GetGValue(c) + lift), std::min(255, GetBValue(c) + lift));
}

std::vector<const StatsPlayerInfo *> ListedPlayers(const View &v)
{
	std::vector<const StatsPlayerInfo *> players;
	if (!v.statsData) return players;
	for (const StatsPlayerInfo &info : v.statsData->players) {
		if (v.statsPlayers.empty() || std::find(v.statsPlayers.begin(), v.statsPlayers.end(), info.index) != v.statsPlayers.end()) players.push_back(&info);
	}
	return players;
}

bool Shown(const View &v, const StatsPlayerInfo &info)
{
	return InGroup(info, v.stats.group) && !(v.stats.hidden >> info.index & 1);
}

void Chip(Painter &p, int x, int y, int w, int h, const std::string &label, bool active, int action, int arg, const View &v)
{
	const bool hot = !active && p.Hover(x, y, w, h, v.mouseX, v.mouseY);
	if (active) p.FillV(x, y, w, h, RGB(124, 88, 34), RGB(84, 56, 20));
	else p.Fill(x, y, w, h, hot ? pal::fieldHover : pal::well);
	p.Frame(x, y, w, h, active ? kGold : hot ? pal::edgeHot : pal::edge);
	p.Font(3);
	p.Text(x + 4, y, w - 8, h, label, active ? pal::goldLight : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (!active) p.AddHit(x, y, w, h, action, arg);
}

void PaintRail(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players)
{
	const int chipW = (kRailW - 6) / 2;
	for (int i = 0; i < 4; i++) {
		Chip(p, kPad + (i % 2) * (chipW + 6), 92 + (i / 2) * 34, chipW, 28, kGroupLabels[i], v.stats.group == i, kActGroup, i, v);
	}
	constexpr int kListY = 166, kRowH = 42;
	PaintCard(p, kPad, kListY, kRailW, std::max(1, static_cast<int>(players.size())) * kRowH + 4);
	int y = kListY + 2;
	for (const StatsPlayerInfo *info : players) {
		const bool hidden = (v.stats.hidden >> info->index & 1) != 0, inGroup = InGroup(*info, v.stats.group), on = !hidden && inGroup;
		const bool hot = inGroup && p.Hover(kPad, y, kRailW, kRowH, v.mouseX, v.mouseY);
		if (hot) p.Fill(kPad + 1, y, kRailW - 2, kRowH, RGB(44, 34, 24));
		const COLORREF color = ColorOf(*info);
		if (on) p.Fill(kPad + 12, y + 13, 16, 16, color);
		else p.Fill(kPad + 12, y + 13, 16, 16, pal::well);
		p.Frame(kPad + 12, y + 13, 16, 16, on ? RGB(10, 8, 6) : color);
		p.Font(0);
		p.Text(kPad + 36, y, kRailW - 44, kRowH, info->name, !on ? kDim : info->local ? pal::goldLight : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (inGroup) p.AddHit(kPad, y, kRailW, kRowH, kActPlayer, info->index);
		y += kRowH;
	}
}

void PaintTable(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top)
{
	constexpr int kHeadH = 36, kRowH = 44, kNameW = 170, kColGap = 14, kPadIn = 16;
	const Category &category = kCategories[v.stats.category];
	std::vector<const StatsPlayerInfo *> rows;
	for (const StatsPlayerInfo *info : players) {
		if (Shown(v, *info)) rows.push_back(info);
	}
	const auto value = [&](const StatsPlayerInfo *info, int column) {
		const std::vector<StatSample> &samples = v.statsData->history.Samples(info->index);
		return samples.empty() ? 0 : samples.back().values[category.metrics[column].id];
	};
	if (v.stats.sortColumn >= 0) {
		std::stable_sort(rows.begin(), rows.end(), [&](const StatsPlayerInfo *a, const StatsPlayerInfo *b) {
			const int va = value(a, v.stats.sortColumn), vb = value(b, v.stats.sortColumn);
			return v.stats.sortAscending ? va < vb : va > vb;
		});
	}
	PaintCard(p, kMainX, top, kMainW, kHeadH + std::max(1, static_cast<int>(rows.size())) * kRowH + 6);
	const int colX = kMainX + kPadIn + kNameW, colW = (kMainW - kPadIn * 2 - kNameW - kColGap * 3) / 4;
	int best[4] = {};
	for (const StatsPlayerInfo *info : rows) {
		for (int c = 0; c < 4; c++) best[c] = std::max(best[c], value(info, c));
	}
	p.Font(4);
	p.Text(kMainX + kPadIn, top + 8, kNameW, 20, "Player", kGold);
	for (int c = 0; c < 4; c++) {
		const int x = colX + c * (colW + kColGap);
		const bool sorted = v.stats.sortColumn == c, hot = p.Hover(x, top, colW, kHeadH, v.mouseX, v.mouseY);
		p.Font(4);
		p.Text(x, top + 8, colW - 14, 20, category.metrics[c].label, sorted || hot ? pal::goldLight : kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (sorted) {
			if (v.stats.sortAscending) p.ArrowUp(x + colW - 11, top + 14, 9, 7, pal::goldLight);
			else p.ArrowDown(x + colW - 11, top + 14, 9, 7, pal::goldLight);
		}
		p.AddHit(x, top, colW, kHeadH, kActSort, c);
	}
	int y = top + kHeadH;
	if (rows.empty()) {
		p.Font(0);
		p.Text(kMainX + kPadIn, y, kMainW - kPadIn * 2, kRowH, "No player matches the filters.", kDim);
	}
	for (const StatsPlayerInfo *info : rows) {
		p.Fill(kMainX + 14, y, kMainW - 28, 1, RGB(48, 37, 23));
		p.Fill(kMainX + kPadIn, y + 14, 16, 16, ColorOf(*info));
		p.Frame(kMainX + kPadIn, y + 14, 16, 16, RGB(10, 8, 6));
		p.Font(0);
		p.Text(kMainX + kPadIn + 24, y + 2, kNameW - 30, kRowH - 4, info->name, info->local ? pal::goldLight : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		for (int c = 0; c < 4; c++) {
			const int x = colX + c * (colW + kColGap), number = value(info, c), percent = best[c] > 0 ? number * 100 / best[c] : 0;
			const bool leader = number > 0 && number == best[c];
			p.Font(leader ? 1 : 0);
			p.Text(x, y + 5, colW, 22, StatsFormatShort(number), leader ? pal::goldLight : kText);
			p.Fill(x, y + 31, colW, 5, pal::well);
			const int filled = colW * std::max(0, std::min(100, percent)) / 100;
			if (filled > 0) p.FillH(x, y + 31, filled, 5, pal::bronze, leader ? pal::goldLight : kGold);
		}
		y += kRowH;
	}
}

// Antialiased, which GDI lines are not: the canvas is a DIB and each pixel near the line takes its share of the colour.
struct Surface {
	uint32_t *bits = nullptr;
	int w = 0, h = 0, stride = 0;
	bool Open(HDC dc)
	{
		GdiFlush();
		DIBSECTION section = {};
		if (GetObject(GetCurrentObject(dc, OBJ_BITMAP), sizeof(section), &section) != sizeof(section) || !section.dsBm.bmBits) return false;
		w = section.dsBm.bmWidth;
		h = section.dsBm.bmHeight;
		stride = section.dsBm.bmWidthBytes / 4;
		// Every Canvas is top-down; GetObject reports a positive height for those too, so it cannot say.
		bits = static_cast<uint32_t *>(section.dsBm.bmBits);
		return true;
	}
	void Blend(int x, int y, COLORREF c, float a)
	{
		if (x < 0 || y < 0 || x >= w || y >= h) return;
		uint32_t &px = bits[static_cast<ptrdiff_t>(y) * stride + x];
		const int k = static_cast<int>(a * 255);
		const int b = px & 255, g = px >> 8 & 255, r = px >> 16 & 255;
		px = (px & 0xff000000u) | ((r + (GetRValue(c) - r) * k / 255) << 16) | ((g + (GetGValue(c) - g) * k / 255) << 8) | (b + (GetBValue(c) - b) * k / 255);
	}
	void Segment(float x0, float y0, float x1, float y1, float width, COLORREF c)
	{
		const float half = width / 2, reach = half + 1;
		const int left = static_cast<int>(std::floor(std::min(x0, x1) - reach)), right = static_cast<int>(std::ceil(std::max(x0, x1) + reach));
		const int top = static_cast<int>(std::floor(std::min(y0, y1) - reach)), bottom = static_cast<int>(std::ceil(std::max(y0, y1) + reach));
		const float dx = x1 - x0, dy = y1 - y0, length2 = dx * dx + dy * dy;
		for (int y = top; y <= bottom; y++) {
			for (int x = left; x <= right; x++) {
				const float px = x + 0.5f - x0, py = y + 0.5f - y0;
				const float t = length2 > 0 ? std::max(0.0f, std::min(1.0f, (px * dx + py * dy) / length2)) : 0.0f;
				const float ex = px - t * dx, ey = py - t * dy;
				const float cover = half + 0.5f - std::sqrt(ex * ex + ey * ey);
				if (cover > 0) Blend(x, y, c, std::min(1.0f, cover));
			}
		}
	}
};

struct Series {
	std::string name;
	COLORREF color = 0;
	bool thick = false;
	std::vector<StatsPoint> points;
	std::vector<StatsPoint> marks;
};

int ValueAt(const std::vector<StatsPoint> &points, uint32_t ms, bool &found)
{
	const auto after = std::upper_bound(points.begin(), points.end(), ms, [](uint32_t t, const StatsPoint &s) { return t < s.ms; });
	found = !points.empty();
	if (!found) return 0;
	return after == points.begin() ? points.front().value : (after - 1)->value;
}

void PaintLines(Painter &p, const View &v, const std::vector<Series> &series, int top, int bottom)
{
	PaintCard(p, kMainX, top, kMainW, bottom - top);
	const int plotX = kMainX + 62, plotW = kMainW - 62 - 18, plotY = top + 16, plotH = bottom - top - 16 - 32;
	uint32_t first = 0xffffffffu, now = 0;
	for (const Series &s : series) {
		if (s.points.empty()) continue;
		first = std::min(first, s.points.front().ms);
		now = std::max(now, s.points.back().ms);
	}
	const uint32_t span = kRangeMs[v.stats.range];
	const uint32_t from = std::max(first == 0xffffffffu ? 0 : first, span && now > span ? now - span : 0);
	const uint32_t length = std::max<uint32_t>(now > from ? now - from : 0, 1000);

	int maximum = 0;
	size_t mostPoints = 0;
	for (const Series &s : series) {
		size_t points = 0;
		for (const StatsPoint &point : s.points) {
			if (point.ms < from) continue;
			maximum = std::max(maximum, point.value);
			points++;
		}
		mostPoints = std::max(mostPoints, points);
	}
	if (series.empty() || mostPoints < 2) {
		p.Font(0);
		p.Text(plotX, plotY, plotW, plotH, series.empty() ? "No player matches the filters." : "Not enough history to draw yet. The Table view has the totals.", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const StatsAxis axis = StatsNiceAxis(maximum);

	p.Font(3);
	for (int value = 0; value <= axis.top; value += axis.step) {
		const int y = plotY + plotH - static_cast<int>(static_cast<int64_t>(plotH) * value / axis.top);
		p.Fill(plotX, y, plotW, 1, value == 0 ? pal::line : RGB(48, 37, 23));
		p.Text(kMainX + 6, y - 10, 50, 20, StatsFormatShort(value), kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	constexpr int kTimeLabels = 5;
	for (int i = 0; i <= kTimeLabels; i++) {
		const int x = plotX + plotW * i / kTimeLabels;
		const uint32_t ms = from + static_cast<uint32_t>(static_cast<uint64_t>(length) * i / kTimeLabels);
		p.Fill(x, plotY + plotH, 1, 5, pal::line);
		const UINT align = i == 0 ? DT_LEFT : i == kTimeLabels ? DT_RIGHT : DT_CENTER;
		const int labelX = i == 0 ? x : i == kTimeLabels ? x - 70 : x - 35;
		p.Text(labelX, plotY + plotH + 6, 70, 20, StatsFormatTime(ms), kDim, align | DT_VCENTER | DT_SINGLELINE);
	}

	const auto xOf = [&](uint32_t ms) { return plotX + static_cast<float>(ms - from) * plotW / length; };
	const auto yOf = [&](int value) { return plotY + plotH - static_cast<float>(value) * plotH / axis.top; };

	Surface surface;
	const bool direct = surface.Open(p.dc);
	std::vector<const Series *> ordered;
	for (const Series &s : series) ordered.push_back(&s);
	std::stable_sort(ordered.begin(), ordered.end(), [](const Series *a, const Series *b) { return !a->thick && b->thick; });
	for (const Series *s : ordered) {
		const COLORREF color = LineColor(s->color);
		const float width = (s->thick ? 2.6f : 1.8f) * p.s;
		const StatsPoint *previous = nullptr;
		for (const StatsPoint &point : s->points) {
			if (point.ms < from) continue;
			if (previous && direct) surface.Segment(xOf(previous->ms) * p.s, yOf(previous->value) * p.s, xOf(point.ms) * p.s, yOf(point.value) * p.s, width, color);
			previous = &point;
		}
		if (previous && direct) {
			const float px = xOf(previous->ms) * p.s, py = yOf(previous->value) * p.s;
			surface.Segment(px, py, px, py, width * 2.2f, color);
		}
	}
	bool anyMark = false;
	for (const Series &s : series) {
		for (const StatsPoint &mark : s.marks) {
			if (mark.ms < from) continue;
			anyMark = true;
			const int x = static_cast<int>(xOf(mark.ms)), y = static_cast<int>(yOf(mark.value));
			p.Diamond(x, y, 7, RGB(14, 10, 7));
			p.Diamond(x, y, 5, LineColor(s.color));
		}
	}
	if (anyMark) {
		p.Diamond(plotX + plotW - 90, plotY + 10, 5, pal::goldLight);
		p.Font(3);
		p.Text(plotX + plotW - 80, plotY, 80, 20, "Eliminated", kDim);
	}

	const int mouseX = static_cast<int>((v.mouseX - p.ox) / p.s), mouseY = static_cast<int>((v.mouseY - p.oy) / p.s);
	if (mouseX < plotX || mouseX > plotX + plotW || mouseY < plotY || mouseY > plotY + plotH) return;
	const uint32_t at = from + static_cast<uint32_t>(static_cast<uint64_t>(length) * (mouseX - plotX) / plotW);
	p.Fill(mouseX, plotY, 1, plotH, RGB(150, 122, 70));

	struct Line {
		const Series *series;
		int value;
	};
	std::vector<Line> lines;
	for (const Series &s : series) {
		bool found = false;
		const int value = ValueAt(s.points, at, found);
		if (found) lines.push_back({&s, value});
	}
	std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.value > b.value; });
	constexpr int kTipW = 230, kTipRow = 22;
	const int tipH = 30 + static_cast<int>(lines.size()) * kTipRow + 6;
	const int tipX = mouseX + 14 + kTipW > plotX + plotW ? mouseX - 14 - kTipW : mouseX + 14;
	const int tipY = std::max(plotY, std::min(mouseY - tipH / 2, plotY + plotH - tipH));
	p.Fill(tipX, tipY, kTipW, tipH, pal::dialog);
	p.Frame(tipX, tipY, kTipW, tipH, kGold);
	p.Font(3);
	p.Text(tipX + 10, tipY + 5, kTipW - 20, 20, StatsFormatTime(at), pal::goldLight);
	int rowY = tipY + 28;
	for (const Line &line : lines) {
		p.Fill(tipX + 10, rowY + 5, 11, 11, line.series->color);
		p.Text(tipX + 27, rowY, kTipW - 27 - 66, kTipRow, line.series->name, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(tipX + kTipW - 64, rowY, 54, kTipRow, StatsFormatShort(line.value), pal::goldLight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		rowY += kTipRow;
	}
}

std::vector<StatsPlayerInfo> ShownPlayers(const View &v, const std::vector<const StatsPlayerInfo *> &players)
{
	std::vector<StatsPlayerInfo> shown;
	for (const StatsPlayerInfo *info : players) {
		if (Shown(v, *info)) shown.push_back(*info);
	}
	return shown;
}

void PaintPlayerChart(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	const StatMetric metric = kCategories[v.stats.category].metrics[v.stats.metric].id;
	const StatsHistory &history = v.statsData->history;
	const std::vector<StatsPlayerInfo> shown = ShownPlayers(v, players);
	const std::vector<StatsElimination> eliminations = StatsEliminations(history, shown);
	std::vector<Series> series;
	for (const StatsPlayerInfo &info : shown) {
		Series s;
		s.name = info.name;
		s.color = ColorOf(info);
		s.thick = info.local;
		for (const StatSample &sample : history.Samples(info.index)) {
			StatsPoint point;
			point.ms = sample.ms;
			point.value = sample.values[metric];
			s.points.push_back(point);
		}
		for (const StatsElimination &e : eliminations) {
			if (e.player != info.index) continue;
			if (const StatSample *sample = StatsSampleAt(history.Samples(info.index), e.ms)) {
				StatsPoint mark;
				mark.ms = e.ms;
				mark.value = sample->values[metric];
				s.marks.push_back(mark);
			}
		}
		series.push_back(s);
	}
	PaintLines(p, v, series, top, bottom);
}

std::string TeamName(const StatsTeam &team)
{
	std::string name;
	for (const StatsPlayerInfo &info : team.members) {
		if (!name.empty()) name += " + ";
		name += info.name;
	}
	return name;
}

bool TeamHasLocal(const StatsTeam &team)
{
	return std::any_of(team.members.begin(), team.members.end(), [](const StatsPlayerInfo &info) { return info.local; });
}

void PaintTeams(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	constexpr int kHeadH = 32, kRowH = 30, kNameW = 190, kColGap = 14, kPadIn = 16;
	const Category &category = kCategories[v.stats.category];
	const StatsHistory &history = v.statsData->history;
	const std::vector<StatsTeam> teams = StatsTeams(ShownPlayers(v, players));
	const auto value = [&](const StatsTeam &team, int column) {
		std::vector<int> values;
		for (const StatsPlayerInfo &info : team.members) {
			const std::vector<StatSample> &samples = history.Samples(info.index);
			if (!samples.empty()) values.push_back(samples.back().values[category.metrics[column].id]);
		}
		return StatsCombine(category.metrics[column].id, values);
	};
	const int tableH = kHeadH + std::max(1, static_cast<int>(teams.size())) * kRowH + 4;
	PaintCard(p, kMainX, top, kMainW, tableH);
	const int colX = kMainX + kPadIn + kNameW, colW = (kMainW - kPadIn * 2 - kNameW - kColGap * 3) / 4;
	int best[4] = {};
	for (const StatsTeam &team : teams) {
		for (int c = 0; c < 4; c++) best[c] = std::max(best[c], value(team, c));
	}
	p.Font(4);
	p.Text(kMainX + kPadIn, top + 7, kNameW, 20, "Teams", kGold);
	for (int c = 0; c < 4; c++) p.Text(colX + c * (colW + kColGap), top + 7, colW, 20, category.metrics[c].label, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	int y = top + kHeadH;
	for (const StatsTeam &team : teams) {
		p.Fill(kMainX + 14, y, kMainW - 28, 1, RGB(48, 37, 23));
		p.Fill(kMainX + kPadIn, y + 8, 14, 14, ColorOf(team.members.front()));
		p.Frame(kMainX + kPadIn, y + 8, 14, 14, RGB(10, 8, 6));
		p.Font(3);
		p.Text(kMainX + kPadIn + 20, y, kNameW - 26, kRowH, TeamName(team), TeamHasLocal(team) ? pal::goldLight : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		for (int c = 0; c < 4; c++) {
			const int x = colX + c * (colW + kColGap), number = value(team, c), percent = best[c] > 0 ? number * 100 / best[c] : 0;
			const bool leader = number > 0 && number == best[c];
			p.Font(3);
			p.Text(x, y + 2, colW, 18, StatsFormatShort(number), leader ? pal::goldLight : kText);
			p.Fill(x, y + 21, colW, 4, pal::well);
			const int filled = colW * std::max(0, std::min(100, percent)) / 100;
			if (filled > 0) p.FillH(x, y + 21, filled, 4, pal::bronze, leader ? pal::goldLight : kGold);
		}
		y += kRowH;
	}
	const StatMetric metric = category.metrics[v.stats.metric].id;
	std::vector<Series> series;
	for (const StatsTeam &team : teams) {
		Series s;
		s.name = TeamName(team);
		s.color = ColorOf(team.members.front());
		s.thick = TeamHasLocal(team);
		s.points = StatsTeamSeries(history, team.members, metric);
		series.push_back(s);
	}
	PaintLines(p, v, series, top + tableH + 8, bottom);
}

void PaintSummary(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	const StatsHistory &history = v.statsData->history;
	std::vector<StatsPlayerInfo> all;
	for (const StatsPlayerInfo *info : players) all.push_back(*info);
	const std::vector<StatsPlayerInfo> shown = ShownPlayers(v, players);

	constexpr int kBannerH = 58;
	PaintCard(p, kMainX, top, kMainW, kBannerH);
	const int winner = StatsWinningTeam(history, all);
	std::string title = Tr("No winner yet");
	for (const StatsTeam &team : StatsTeams(all)) {
		if (team.id == winner) title = Tr("Winner: ") + TeamName(team);
	}
	p.Font(2);
	p.Text(kMainX + 16, top, kMainW - 32, kBannerH, title, winner >= 0 ? pal::goldLight : kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	static const struct {
		const char *label;
		StatMetric metric;
	} kAwards[] = {{"Best score", kStatScore}, {"Most kills", kStatKilled}, {"Largest army", kStatUnitsMax}, {"Most damage", kStatDamageDealt}, {"Most gold", kStatGoldTotal}, {"Highest level", kStatLevelMax}};
	constexpr int kAwardGap = 10, kAwardH = 66;
	const int awardW = (kMainW - kAwardGap * 2) / 3;
	for (int i = 0; i < 6; i++) {
		const int x = kMainX + (i % 3) * (awardW + kAwardGap), y = top + kBannerH + kAwardGap + (i / 3) * (kAwardH + kAwardGap);
		PaintCard(p, x, y, awardW, kAwardH);
		p.Font(4);
		p.Text(x + 12, y + 7, awardW - 24, 18, kAwards[i].label, kGold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int best = StatsBestPlayer(history, shown, kAwards[i].metric);
		for (const StatsPlayerInfo &info : shown) {
			if (info.index != best) continue;
			p.Fill(x + 12, y + 37, 13, 13, ColorOf(info));
			p.Font(3);
			p.Text(x + 31, y + 32, awardW - 31 - 66, 24, info.name, info.local ? pal::goldLight : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(1);
			p.Text(x + awardW - 70, y + 32, 58, 24, StatsFormatShort(history.Samples(best).back().values[kAwards[i].metric]), pal::goldLight, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
	}

	const int listY = top + kBannerH + kAwardGap + 2 * (kAwardH + kAwardGap), listH = bottom - listY, listW = (kMainW - kAwardGap) / 2;
	constexpr int kListRow = 24, kListHead = 30;
	PaintCard(p, kMainX, listY, listW, listH);
	p.Font(4);
	p.Text(kMainX + 12, listY + 7, listW - 24, 18, "Ranking", kGold);
	std::vector<StatsPlayerInfo> ranking = shown;
	const auto score = [&](const StatsPlayerInfo &info) {
		const std::vector<StatSample> &samples = history.Samples(info.index);
		return samples.empty() ? 0 : samples.back().values[kStatScore];
	};
	std::stable_sort(ranking.begin(), ranking.end(), [&](const StatsPlayerInfo &a, const StatsPlayerInfo &b) { return score(a) > score(b); });
	int rowY = listY + kListHead, place = 1;
	for (const StatsPlayerInfo &info : ranking) {
		if (rowY + kListRow > listY + listH) break;
		const bool in = StatsStillInGame(history, info.index);
		p.Font(3);
		p.Text(kMainX + 12, rowY, 20, kListRow, std::to_string(place++), kDim);
		p.Fill(kMainX + 34, rowY + 6, 12, 12, ColorOf(info));
		p.Text(kMainX + 52, rowY, listW - 52 - 62, kListRow, info.name, !in ? kDim : info.local ? pal::goldLight : kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(kMainX + listW - 60, rowY, 48, kListRow, StatsFormatShort(score(info)), in ? pal::goldLight : kDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		rowY += kListRow;
	}

	const int momentsX = kMainX + listW + kAwardGap;
	PaintCard(p, momentsX, listY, listW, listH);
	p.Font(4);
	p.Text(momentsX + 12, listY + 7, listW - 24, 18, "Key moments", kGold);
	const std::vector<StatsElimination> eliminations = StatsEliminations(history, all);
	const int fits = (listH - kListHead) / kListRow;
	rowY = listY + kListHead;
	p.Font(3);
	if (eliminations.empty()) p.Text(momentsX + 12, rowY, listW - 24, kListRow, "Nobody has been eliminated yet.", kDim);
	const size_t skip = eliminations.size() > static_cast<size_t>(fits) ? eliminations.size() - fits : 0;
	for (size_t i = skip; i < eliminations.size(); i++) {
		const StatsElimination &e = eliminations[i];
		for (const StatsPlayerInfo &info : all) {
			if (info.index != e.player) continue;
			p.Text(momentsX + 12, rowY, 58, kListRow, StatsFormatTime(e.ms), pal::goldLight);
			p.Fill(momentsX + 72, rowY + 6, 12, 12, ColorOf(info));
			p.Text(momentsX + 90, rowY, listW - 100, kListRow, TrFmt("%s was eliminated", {info.name}), kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		rowY += kListRow;
	}
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
	constexpr int kPadIn = 36, kTextTop = 30, kRuleY = 136, kButtonW = 150, kButtonsY = 158, kButtonsGap = 16;
	PaintWindowFrame(p, kConfirmW, kConfirmH);
	p.Font(1);
	DrawCentered(p, p.R(kPadIn, kTextTop, kConfirmW - kPadIn * 2, kRuleY - kTextTop - 8), v.question, pal::goldLight);
	PaintOrnateRule(p, kRuleY, kConfirmW);
	const int x = (kConfirmW - kButtonW * 2 - kButtonsGap) / 2;
	p.Button(x, kButtonsY, kButtonW, kButtonH, "Yes", kActYes, 0, v.mouseX, v.mouseY, true, true);
	p.Button(x + kButtonW + kButtonsGap, kButtonsY, kButtonW, kButtonH, "No", kActNo, 0, v.mouseX, v.mouseY);
}

void PaintStatistics(Painter &p, const View &v)
{
	PaintWindowFrame(p, kStatsW, kStatsH);
	PaintWindowHeader(p, kStatsW, "STATISTICS");

	const std::vector<const StatsPlayerInfo *> players = ListedPlayers(v);
	PaintRail(p, v, players);
	p.Fill(kPad + kRailW + 7, 92, 1, kFooterY - 92 - 8, pal::lineDim);

	constexpr int kViewW = 92, kRangeW = 112, kChipH = 28, kChipsY = 92, kTabsY = 126, kTabH = 34, kMetricsY = 168, kChartTop = 204, kBottom = kFooterY - 10;
	static const char *const kViewLabels[] = {"Summary", "Chart", "Table", "Teams"};
	for (int i = 0; i < 4; i++) Chip(p, kMainX + i * (kViewW + 6), kChipsY, kViewW, kChipH, kViewLabels[i], v.stats.view == i, kActView, i, v);
	const bool hasData = v.statsData && !v.statsData->history.Empty();
	const bool summary = v.stats.view == kViewSummary;

	if (!hasData) {
		p.Font(0);
		p.Text(kMainX, 200, kMainW, 60, "No data for this period yet.", kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else if (summary) {
		PaintSummary(p, v, players, kTabsY + 4, kBottom);
	}
	else {
		std::vector<TabSpec> tabs;
		for (int i = 0; i < static_cast<int>(std::size(kCategories)); i++) tabs.push_back({kCategories[i].name, kActCategory, i, v.stats.category == i, true});
		PaintTabs(p, kMainX, kTabsY, tabs, v.mouseX, v.mouseY, (kMainW - 6 * 4) / 5, kTabH);
		p.Fill(kMainX, kTabsY + kTabH, kMainW, 1, pal::line);
		if (v.stats.view == kViewTable) {
			PaintTable(p, v, players, kMetricsY + 2);
		}
		else {
			for (int i = 0; i < 3; i++) Chip(p, kMainX + kMainW - (3 - i) * kRangeW - (2 - i) * 6, kChipsY, kRangeW, kChipH, kRangeLabels[i], v.stats.range == i, kActRange, i, v);
			const Category &category = kCategories[v.stats.category];
			const int metricW = (kMainW - 6 * 3) / 4;
			for (int i = 0; i < 4; i++) Chip(p, kMainX + i * (metricW + 6), kMetricsY, metricW, kChipH, category.metrics[i].label, v.stats.metric == i, kActMetric, i, v);
			if (v.stats.view == kViewTeams) PaintTeams(p, v, players, kChartTop, kBottom);
			else PaintPlayerChart(p, v, players, kChartTop, kBottom);
		}
	}

	p.Fill(18, kFooterY, kStatsW - 36, 1, pal::line);
	if (v.statsData) {
		p.Font(0);
		p.Text(kPad + 4, kFooterY + 10, 400, 40, Tr("Game time: ") + StatsFormatTime(v.statsData->history.LastMs()), kDim);
	}
	p.Button(kStatsW - kPad - 160, kFooterY + 10, 160, 40, "Close", kActButton, kStatsClose, v.mouseX, v.mouseY, true, true);
}

}
