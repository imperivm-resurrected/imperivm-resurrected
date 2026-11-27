// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/gamemenuui_view.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

#include "ui/fullscreen.h"
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

constexpr size_t kMaxListedPlayers = 8;

std::vector<const StatsPlayerInfo *> ListedPlayers(const View &v)
{
	std::vector<const StatsPlayerInfo *> players;
	if (!v.statsData) return players;
	for (const StatsPlayerInfo &info : v.statsData->players) {
		if (v.statsPlayers.empty() || std::find(v.statsPlayers.begin(), v.statsPlayers.end(), info.index) != v.statsPlayers.end()) players.push_back(&info);
		// The rail and the table hold the 8 players of a match, as many as the game's own statistics list.
		if (players.size() == kMaxListedPlayers) break;
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
	p.WellSegment(x, y, w, h, active, hot);
	p.Font(3);
	p.Text(x + 4, y, w - 8, h, label, active || hot ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	if (!active) p.AddHit(x, y, w, h, action, arg);
}

void PaintRail(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players)
{
	const int chipW = (kRailW - 6) / 2;
	for (int i = 0; i < 4; i++) {
		Chip(p, kPad + (i % 2) * (chipW + 6), 92 + (i / 2) * 34, chipW, 28, kGroupLabels[i], v.stats.group == i, kActGroup, i, v);
	}
	constexpr int kListY = 166, kRowH = 42;
	PaintGlassCard(p, kPad, kListY, kRailW, std::max(1, static_cast<int>(players.size())) * kRowH + 4);
	int y = kListY + 2;
	for (const StatsPlayerInfo *info : players) {
		const bool hidden = (v.stats.hidden >> info->index & 1) != 0, inGroup = InGroup(*info, v.stats.group), on = !hidden && inGroup;
		const bool hot = inGroup && p.Hover(kPad, y, kRailW, kRowH, v.mouseX, v.mouseY);
		if (hot) p.Fill(kPad + 1, y, kRailW - 2, kRowH, pal::selected);
		const COLORREF color = ColorOf(*info);
		if (on) p.Fill(kPad + 12, y + 13, 16, 16, color);
		else p.Fill(kPad + 12, y + 13, 16, 16, pal::bg);
		p.Frame(kPad + 12, y + 13, 16, 16, on ? pal::bg : color);
		p.Font(0);
		TextAsIs(p, kPad + 36, y, kRailW - 44, kRowH, info->name, !on ? pal::textDim : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (inGroup) p.AddHit(kPad, y, kRailW, kRowH, kActPlayer, info->index);
		y += kRowH;
	}
}

StatsWindow ChartLimits(const View &v)
{
	StatsWindow limits;
	limits.to = v.statsData->history.LastMs();
	limits.from = limits.to;
	for (const StatsPlayerInfo *info : ListedPlayers(v)) {
		const std::vector<StatSample> &samples = v.statsData->history.Samples(info->index);
		if (!samples.empty()) limits.from = std::min(limits.from, samples.front().ms);
	}
	return limits;
}

StatsWindow ChartWindow(const View &v)
{
	return v.stats.zoom.to > v.stats.zoom.from ? v.stats.zoom : ChartLimits(v);
}

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
	PaintGlassCard(p, kMainX, top, kMainW, bottom - top);
	// A strip above the plot for its notes, so that no line ever runs under them.
	constexpr int kNotesH = 24;
	const int plotX = kMainX + 62, plotW = kMainW - 62 - 18, plotY = top + 12 + kNotesH, plotH = bottom - plotY - 32, notesY = top + 7;
	const StatsWindow window = ChartWindow(v);
	const uint32_t from = window.from, length = std::max<uint32_t>(window.to > from ? window.to - from : 0, 1000);

	int maximum = 0;
	size_t mostPoints = 0;
	for (const Series &s : series) {
		size_t points = 0;
		for (const StatsPoint &point : s.points) {
			if (point.ms < from || point.ms > window.to) continue;
			maximum = std::max(maximum, point.value);
			points++;
		}
		mostPoints = std::max(mostPoints, points);
	}
	if (series.empty() || mostPoints < 2) {
		p.Font(0);
		p.Text(plotX, plotY, plotW, plotH, series.empty() ? "No player matches the filters." : "Not enough history to draw yet.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	const StatsAxis axis = StatsNiceAxis(maximum);

	p.Font(3);
	for (int value = 0; value <= axis.top; value += axis.step) {
		const int y = plotY + plotH - static_cast<int>(static_cast<int64_t>(plotH) * value / axis.top);
		p.Fill(plotX, y, plotW, 1, value == 0 ? pal::lineStrong : pal::hairline);
		p.Text(kMainX + 6, y - 10, 50, 20, StatsFormatShort(value), pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	}
	constexpr int kTimeLabels = 5;
	for (int i = 0; i <= kTimeLabels; i++) {
		const int x = plotX + plotW * i / kTimeLabels;
		const uint32_t ms = from + static_cast<uint32_t>(static_cast<uint64_t>(length) * i / kTimeLabels);
		p.Fill(x, plotY + plotH, 1, 5, pal::lineStrong);
		const UINT align = i == 0 ? DT_LEFT : i == kTimeLabels ? DT_RIGHT : DT_CENTER;
		const int labelX = i == 0 ? x : i == kTimeLabels ? x - 70 : x - 35;
		p.Text(labelX, plotY + plotH + 6, 70, 20, StatsFormatTime(ms), pal::textDim, align | DT_VCENTER | DT_SINGLELINE);
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
			if (point.ms < from || point.ms > window.to) continue;
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
			if (mark.ms < from || mark.ms > window.to) continue;
			anyMark = true;
			const int x = static_cast<int>(xOf(mark.ms)), y = static_cast<int>(yOf(mark.value));
			p.Diamond(x, y, 7, pal::bg);
			p.Diamond(x, y, 5, LineColor(s.color));
		}
	}
	p.Font(3);
	if (anyMark) {
		p.Diamond(plotX + plotW - 90, notesY + 10, 5, pal::text);
		p.Text(plotX + plotW - 80, notesY, 80, 20, "Eliminated", pal::textDim);
	}
	p.Text(plotX, notesY, plotW - 110, 20, "Drag over the chart to zoom in", pal::textDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	p.AddHit(plotX, plotY, plotW, plotH, kActChartArea, 0);

	const int mouseX = static_cast<int>((v.mouseX - p.ox) / p.s), mouseY = static_cast<int>((v.mouseY - p.oy) / p.s);
	if (v.stats.dragStartX >= 0) {
		const int startX = static_cast<int>((v.stats.dragStartX - p.ox) / p.s);
		const int bandLeft = std::max(plotX, std::min(startX, mouseX)), bandRight = std::min(plotX + plotW, std::max(startX, mouseX));
		if (direct) {
			surface.Open(p.dc);
			for (int y = p.S(plotY); y < p.S(plotY + plotH); y++) {
				for (int x = p.S(bandLeft); x < p.S(bandRight); x++) surface.Blend(x, y, pal::redText, 0.22f);
			}
		}
		p.Fill(bandLeft, plotY, 1, plotH, pal::redText);
		p.Fill(bandRight, plotY, 1, plotH, pal::redText);
		return;
	}
	if (mouseX < plotX || mouseX > plotX + plotW || mouseY < plotY || mouseY > plotY + plotH) return;
	const uint32_t at = from + static_cast<uint32_t>(static_cast<uint64_t>(length) * (mouseX - plotX) / plotW);
	p.Fill(mouseX, plotY, 1, plotH, pal::redText);

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
	constexpr int kTipW = 230, kTipRow = 20;
	const int tipH = 30 + static_cast<int>(lines.size()) * kTipRow + 6;
	const int tipX = mouseX + 14 + kTipW > plotX + plotW ? mouseX - 14 - kTipW : mouseX + 14;
	const int tipY = std::max(92, std::min(mouseY - tipH / 2, plotY + plotH - tipH));
	p.Fill(tipX, tipY, kTipW, tipH, pal::panel);
	p.Frame(tipX, tipY, kTipW, tipH, pal::lineStrong);
	p.Font(3);
	p.Text(tipX + 10, tipY + 5, kTipW - 20, 20, StatsFormatTime(at), pal::text);
	int rowY = tipY + 28;
	for (const Line &line : lines) {
		p.Fill(tipX + 10, rowY + 4, 11, 11, line.series->color);
		TextAsIs(p, tipX + 27, rowY, kTipW - 27 - 66, kTipRow, line.series->name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(tipX + kTipW - 64, rowY, 54, kTipRow, StatsFormatShort(line.value), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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

// One row of the board: a player, or a team with all its members.
struct BoardRow {
	std::string name;
	COLORREF color = 0;
	bool mine = false, out = false;
	std::vector<StatsPlayerInfo> members;
	int values[4] = {};
};

// The table on top says the totals; the chart below shows how the selected column got there.
void PaintBoard(Painter &p, const View &v, std::vector<BoardRow> rows, bool marks, int top, int bottom)
{
	constexpr int kHeadH = 32, kRowH = 29, kNameW = 190, kColGap = 14, kPadIn = 16;
	const Category &category = kCategories[v.stats.category];
	const StatsHistory &history = v.statsData->history;
	const int selected = v.stats.metric;
	std::stable_sort(rows.begin(), rows.end(), [&](const BoardRow &a, const BoardRow &b) {
		return v.stats.sortAscending ? a.values[selected] < b.values[selected] : a.values[selected] > b.values[selected];
	});
	const int tableH = kHeadH + std::max(1, static_cast<int>(rows.size())) * kRowH + 4;
	PaintGlassCard(p, kMainX, top, kMainW, tableH);
	const int colX = kMainX + kPadIn + kNameW, colW = (kMainW - kPadIn * 2 - kNameW - kColGap * 3) / 4;
	int best[4] = {}, lowest[4] = {};
	for (int c = 0; c < 4 && !rows.empty(); c++) lowest[c] = rows.front().values[c];
	for (const BoardRow &row : rows) {
		for (int c = 0; c < 4; c++) {
			best[c] = std::max(best[c], row.values[c]);
			lowest[c] = std::min(lowest[c], row.values[c]);
		}
	}
	const auto leads = [&](const BoardRow &row, int c) {
		if (rows.size() < 2 || best[c] == lowest[c]) return false;
		const StatMetric metric = category.metrics[c].id;
		// For these, having less is what is good.
		if (metric == kStatLost || metric == kStatDamageTaken) return row.values[c] == lowest[c];
		return row.values[c] == best[c];
	};
	for (int c = 0; c < 4; c++) {
		const int x = colX + c * (colW + kColGap);
		const bool on = c == selected, hot = !on && p.Hover(x - 6, top, colW + 12, kHeadH, v.mouseX, v.mouseY);
		if (on) p.Fill(x - 6, top + 1, colW + 12, tableH - 2, pal::raised);
		else if (hot) p.Fill(x - 6, top + 1, colW + 12, kHeadH - 2, pal::selected);
		p.Font(4);
		p.Text(x, top + 7, colW - 14, 20, category.metrics[c].label, on || hot ? pal::text : pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		if (on) {
			if (v.stats.sortAscending) p.ArrowUp(x + colW - 11, top + 13, 9, 7, pal::text);
			else p.ArrowDown(x + colW - 11, top + 13, 9, 7, pal::text);
		}
		p.AddHit(x - 6, top, colW + 12, kHeadH, kActMetric, c);
	}
	int y = top + kHeadH;
	if (rows.empty()) {
		p.Font(0);
		p.Text(kMainX + kPadIn, y, kMainW - kPadIn * 2, kRowH, "No player matches the filters.", pal::textDim);
	}
	std::vector<Series> series;
	for (const BoardRow &row : rows) {
		p.Fill(kMainX + 14, y, kMainW - 28, 1, pal::hairline);
		// The highlight of the selected column is as light as the line, which would vanish on it.
		p.Fill(colX + selected * (colW + kColGap) - 6, y, colW + 12, 1, pal::red);
		p.Fill(kMainX + kPadIn, y + 8, 14, 14, row.color);
		p.Frame(kMainX + kPadIn, y + 8, 14, 14, pal::bg);
		p.Font(3);
		TextAsIs(p, kMainX + kPadIn + 20, y, kNameW - 26, kRowH, row.name, row.out ? pal::textDim : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		for (int c = 0; c < 4; c++) {
			const int x = colX + c * (colW + kColGap), percent = best[c] > 0 ? row.values[c] * 100 / best[c] : 0;
			const bool leader = leads(row, c);
			if (leader) {
				p.Fill(x - 6, y + 1, colW + 12, kRowH - 2, pal::selected);
				p.Frame(x - 6, y + 1, colW + 12, kRowH - 2, pal::redText);
			}
			p.Font(leader ? 1 : 3);
			p.Text(x, y + 2, colW, 18, StatsFormatShort(row.values[c]), pal::text);
			p.Fill(x, y + 21, colW, 4, leader ? pal::raised : pal::bg);
			const int filled = colW * std::max(0, std::min(100, percent)) / 100;
			if (filled > 0) p.Fill(x, y + 21, filled, 4, leader ? pal::redText : pal::red);
		}
		y += kRowH;

		Series s;
		s.name = row.name;
		s.color = row.color;
		s.thick = row.mine;
		const StatMetric metric = category.metrics[selected].id;
		s.points = StatsTeamSeries(history, row.members, metric);
		if (marks) {
			for (const StatsElimination &e : StatsEliminations(history, row.members)) {
				bool found = false;
				StatsPoint mark;
				mark.ms = e.ms;
				mark.value = ValueAt(s.points, e.ms, found);
				if (found) s.marks.push_back(mark);
			}
		}
		series.push_back(s);
	}
	PaintLines(p, v, series, top + tableH + 8, bottom);
}

void PaintPlayers(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	const Category &category = kCategories[v.stats.category];
	std::vector<BoardRow> rows;
	for (const StatsPlayerInfo &info : ShownPlayers(v, players)) {
		BoardRow row;
		row.name = info.name;
		row.color = ColorOf(info);
		row.mine = info.local;
		row.out = !StatsStillInGame(v.statsData->history, info.index);
		row.members.push_back(info);
		const std::vector<StatSample> &samples = v.statsData->history.Samples(info.index);
		for (int c = 0; c < 4 && !samples.empty(); c++) row.values[c] = samples.back().values[category.metrics[c].id];
		rows.push_back(row);
	}
	PaintBoard(p, v, rows, true, top, bottom);
}

void PaintTeams(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	const Category &category = kCategories[v.stats.category];
	std::vector<BoardRow> rows;
	for (const StatsTeam &team : StatsTeams(ShownPlayers(v, players))) {
		BoardRow row;
		row.name = TeamName(team);
		row.color = ColorOf(team.members.front());
		row.mine = TeamHasLocal(team);
		row.members = team.members;
		row.out = true;
		for (const StatsPlayerInfo &info : team.members) row.out = row.out && !StatsStillInGame(v.statsData->history, info.index);
		for (int c = 0; c < 4; c++) {
			std::vector<int> values;
			for (const StatsPlayerInfo &info : team.members) {
				const std::vector<StatSample> &samples = v.statsData->history.Samples(info.index);
				if (!samples.empty()) values.push_back(samples.back().values[category.metrics[c].id]);
			}
			row.values[c] = StatsCombine(category.metrics[c].id, values);
		}
		rows.push_back(row);
	}
	PaintBoard(p, v, rows, false, top, bottom);
}

void PaintSummary(Painter &p, const View &v, const std::vector<const StatsPlayerInfo *> &players, int top, int bottom)
{
	const StatsHistory &history = v.statsData->history;
	std::vector<StatsPlayerInfo> all;
	for (const StatsPlayerInfo *info : players) all.push_back(*info);
	const std::vector<StatsPlayerInfo> shown = ShownPlayers(v, players);

	constexpr int kBannerH = 58;
	PaintGlassCard(p, kMainX, top, kMainW, kBannerH);
	const int winner = StatsWinningTeam(history, all);
	std::string title = Tr("No winner yet");
	for (const StatsTeam &team : StatsTeams(all)) {
		if (team.id == winner) title = Tr("Winner: ") + TeamName(team);
	}
	p.Font(2);
	TextAsIs(p, kMainX + 16, top, kMainW - 32, kBannerH, title, winner >= 0 ? pal::text : pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	static const struct {
		const char *label;
		StatMetric metric;
	} kAwards[] = {{"Best score", kStatScore}, {"Most kills", kStatKilled}, {"Largest army", kStatUnitsMax}, {"Most damage", kStatDamageDealt}, {"Most gold", kStatGoldTotal}, {"Highest level", kStatLevelMax}};
	constexpr int kAwardGap = 10, kAwardH = 66;
	const int awardW = (kMainW - kAwardGap * 2) / 3;
	for (int i = 0; i < 6; i++) {
		const int x = kMainX + (i % 3) * (awardW + kAwardGap), y = top + kBannerH + kAwardGap + (i / 3) * (kAwardH + kAwardGap);
		PaintGlassCard(p, x, y, awardW, kAwardH);
		p.Font(4);
		p.Text(x + 12, y + 7, awardW - 24, 18, kAwards[i].label, pal::redText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		const int best = StatsBestPlayer(history, shown, kAwards[i].metric);
		for (const StatsPlayerInfo &info : shown) {
			if (info.index != best) continue;
			p.Fill(x + 12, y + 37, 13, 13, ColorOf(info));
			p.Font(3);
			TextAsIs(p, x + 31, y + 32, awardW - 31 - 66, 24, info.name, pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			p.Font(1);
			p.Text(x + awardW - 70, y + 32, 58, 24, StatsFormatShort(history.Samples(best).back().values[kAwards[i].metric]), pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
	}

	const int listY = top + kBannerH + kAwardGap + 2 * (kAwardH + kAwardGap), listH = bottom - listY, listW = (kMainW - kAwardGap) / 2;
	constexpr int kListRow = 24, kListHead = 30;
	PaintGlassCard(p, kMainX, listY, listW, listH);
	p.Font(4);
	p.Text(kMainX + 12, listY + 7, listW - 24, 18, "Ranking", pal::redText);
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
		p.Text(kMainX + 12, rowY, 20, kListRow, std::to_string(place++), pal::textDim);
		p.Fill(kMainX + 34, rowY + 6, 12, 12, ColorOf(info));
		TextAsIs(p, kMainX + 52, rowY, listW - 52 - 62, kListRow, info.name, !in ? pal::textDim : pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		p.Text(kMainX + listW - 60, rowY, 48, kListRow, StatsFormatShort(score(info)), in ? pal::text : pal::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		rowY += kListRow;
	}

	const int momentsX = kMainX + listW + kAwardGap;
	PaintGlassCard(p, momentsX, listY, listW, listH);
	p.Font(4);
	p.Text(momentsX + 12, listY + 7, listW - 24, 18, "Key moments", pal::redText);
	const std::vector<StatsElimination> eliminations = StatsEliminations(history, all);
	const int fits = (listH - kListHead) / kListRow;
	rowY = listY + kListHead;
	p.Font(3);
	if (eliminations.empty()) p.Text(momentsX + 12, rowY, listW - 24, kListRow, "Nobody has been eliminated yet.", pal::textDim);
	const size_t skip = eliminations.size() > static_cast<size_t>(fits) ? eliminations.size() - fits : 0;
	for (size_t i = skip; i < eliminations.size(); i++) {
		const StatsElimination &e = eliminations[i];
		for (const StatsPlayerInfo &info : all) {
			if (info.index != e.player) continue;
			p.Text(momentsX + 12, rowY, 58, kListRow, StatsFormatTime(e.ms), pal::text);
			p.Fill(momentsX + 72, rowY + 6, 12, 12, ColorOf(info));
			TextAsIs(p, momentsX + 90, rowY, listW - 100, kListRow, TrFmt("%s was eliminated", {info.name}), pal::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		rowY += kListRow;
	}
}
}

namespace {
// How tall a note is: the game's end messages can run longer than the two lines kNoteH holds, and the window grows with them.
int NoteHeight(const std::string &note)
{
	HDC dc = CreateCompatibleDC(nullptr);
	if (!dc) return kNoteH;
	// The window's small type at scale 1, without touching the shared fonts the frame is being painted with.
	HGDIOBJ old = SelectObject(dc, BrandFont(BrandFace::Ui, 13, 1.0f));
	const std::wstring wide = W(Tr(note));
	RECT r = {0, 0, kMenuW - (kMarginX - 10) * 2, 0};
	DrawTextW(dc, wide.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	SelectObject(dc, old);
	DeleteDC(dc);
	return std::max(kNoteH, static_cast<int>(r.bottom - r.top) + 10);
}
}

int MenuHeight(const View &v)
{
	int h = kWindowHeaderH + kBottomPad - kButtonGap;
	for (const std::string &note : v.notes) h += NoteHeight(note);
	for (const Row &r : v.rows) h += kButtonH + kButtonGap + (r.startsGroup ? kGroupGap : 0);
	return h;
}

void PaintMenu(Painter &p, const View &v)
{
	PaintWindowCard(p, kMenuW, MenuHeight(v));
	int y = PaintWindowHeading(p, kMenuW, v.title, std::string(), {}, 0, v.mouseX, v.mouseY, kWindowHeaderH, v.titleSpacing);
	for (const std::string &note : v.notes) {
		p.Font(3);
		const int noteH = NoteHeight(note);
		DrawCentered(p, p.R(kMarginX - 10, y, kMenuW - (kMarginX - 10) * 2, noteH - 6), note, pal::text);
		y += noteH;
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
	PaintWindowCard(p, kConfirmW, kConfirmH);
	p.Font(1);
	DrawCentered(p, p.R(kPadIn, kTextTop, kConfirmW - kPadIn * 2, kRuleY - kTextTop - 8), v.question, pal::text);
	PaintRule(p, kRuleY, kConfirmW);
	const int x = (kConfirmW - kButtonW * 2 - kButtonsGap) / 2;
	p.Button(x, kButtonsY, kButtonW, kButtonH, "Yes", kActYes, 0, v.mouseX, v.mouseY, true, true);
	p.Button(x + kButtonW + kButtonsGap, kButtonsY, kButtonW, kButtonH, "No", kActNo, 0, v.mouseX, v.mouseY);
}

void FinishChartDrag(View &v, int mouseX)
{
	const int startX = v.stats.dragStartX, left = v.stats.dragLeft, right = v.stats.dragRight;
	v.stats.dragStartX = -1;
	constexpr int kMinDragPixels = 8;
	if (!v.statsData || startX < 0 || right <= left || std::abs(mouseX - startX) < kMinDragPixels) return;
	const double width = right - left;
	const uint32_t minSpan = v.statsData->history.IntervalMs() * 3;
	v.stats.zoom = StatsZoom(ChartWindow(v), (startX - left) / width, (mouseX - left) / width, minSpan, ChartLimits(v));
}

void PaintStatistics(Painter &p, const View &v)
{
	PaintWindowCard(p, kStatsW, kStatsH);
	PaintWindowHeading(p, kStatsW, "Statistics");

	const std::vector<const StatsPlayerInfo *> players = ListedPlayers(v);
	PaintRail(p, v, players);
	p.Fill(kPad + kRailW + 7, 92, 1, kFooterY - 92 - 8, pal::hairline);

	constexpr int kViewW = 100, kZoomOutW = 190, kChipH = 28, kChipsY = 92, kTabsY = 126, kTabH = 34, kBoardTop = 168, kBottom = kFooterY - 10;
	const bool hasData = v.statsData && !v.statsData->history.Empty();
	// Without allies a team is just a player, and this view would repeat the previous one.
	bool anyTeam = false;
	if (hasData) {
		for (const StatsTeam &team : StatsTeams(v.statsData->players)) anyTeam = anyTeam || team.members.size() > 1;
	}
	static const char *const kViewLabels[] = {"Summary", "Players", "Teams"};
	const int viewCount = anyTeam ? 3 : 2, view = !anyTeam && v.stats.view == kViewTeams ? kViewPlayers : v.stats.view;
	for (int i = 0; i < viewCount; i++) Chip(p, kMainX + i * (kViewW + 6), kChipsY, kViewW, kChipH, kViewLabels[i], view == i, kActView, i, v);

	if (!hasData) {
		p.Font(0);
		p.Text(kMainX, 200, kMainW, 60, "No data for this period yet.", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	else if (view == kViewSummary) {
		PaintSummary(p, v, players, kTabsY + 4, kBottom);
	}
	else {
		if (v.stats.zoom.to > v.stats.zoom.from) {
			const int buttonX = kMainX + kMainW - kZoomOutW;
			const std::string selection = StatsFormatTime(v.stats.zoom.from) + " - " + StatsFormatTime(v.stats.zoom.to);
			p.Font(3);
			p.Text(kMainX + viewCount * (kViewW + 6), kChipsY, buttonX - 10 - kMainX - viewCount * (kViewW + 6), kChipH, selection, pal::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			Chip(p, buttonX, kChipsY, kZoomOutW, kChipH, "See the whole game", false, kActZoomOut, 0, v);
		}
		std::vector<TabSpec> tabs;
		for (int i = 0; i < static_cast<int>(std::size(kCategories)); i++) tabs.push_back({kCategories[i].name, kActCategory, i, v.stats.category == i, true});
		PaintTabs(p, kMainX, kTabsY, tabs, v.mouseX, v.mouseY, (kMainW - 6 * 4) / 5, kTabH);
		p.Fill(kMainX, kTabsY + kTabH, kMainW, 1, pal::lineStrong);
		if (view == kViewTeams) PaintTeams(p, v, players, kBoardTop, kBottom);
		else PaintPlayers(p, v, players, kBoardTop, kBottom);
	}

	p.Fill(18, kFooterY, kStatsW - 36, 1, pal::lineStrong);
	if (v.statsData) {
		p.Font(0);
		TextAsIs(p, kPad + 4, kFooterY + 10, 400, 40, Tr("Game time: ") + StatsFormatTime(v.statsData->history.LastMs()), pal::textDim);
	}
	p.Button(kStatsW - kPad - 160, kFooterY + 10, 160, 40, "Back", kActButton, kStatsClose, v.mouseX, v.mouseY, true, true);
}

}
