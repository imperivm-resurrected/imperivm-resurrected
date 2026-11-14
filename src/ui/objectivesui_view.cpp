// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/objectivesui_view.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "i18n/tr.h"
#include "ui/fullscreen.h"
#include "ui/ornaments.h"
#include "ui/widgets.h"

using namespace ui;

namespace objectivesui {
namespace {

// The list on the left with Close under it, as the help has; the note on the right.
constexpr int kMargin = 24, kTop = 96, kBottom = kScreenH - 28, kCloseH = 38, kCloseY = kBottom - kCloseH, kListBottom = kCloseY - 14;
constexpr int kListX = kMargin, kListW = 296, kDividerX = 338, kPaneX = 356, kPaneW = kScreenW - kMargin - kPaneX;
constexpr int kGroupH = 30, kGroupGap = 12, kRowH = 54, kEmptyRowH = 40, kIconBox = 20;
// A title too long for one line takes a second one, and what is under it moves down by kTitleLineH.
constexpr int kTitleY = 98, kTitleH = 48, kTitleLineH = 28, kStatusY = 152, kStatusH = 30, kRuleY = 196, kTextTop = 212;
constexpr int kLineH = 23, kGapLine = 10, kScrollW = 4, kMapButtonW = 176;
const COLORREF kPlain = pal::body, kCurrentBg = pal::selected, kHotBg = pal::raised;
const COLORREF kDoneText = pal::ok, kDoneDim = RGB(92, 112, 78), kDoneMark = pal::ok, kTagText = pal::onRed;

std::wstring Upper(std::wstring w)
{
	if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
	return w;
}

int PixelWidth(Painter &p, const std::wstring &w, int spacing)
{
	SetTextCharacterExtra(p.dc, p.S(spacing));
	SIZE sz = {};
	GetTextExtentPoint32W(p.dc, w.c_str(), static_cast<int>(w.size()), &sz);
	SetTextCharacterExtra(p.dc, 0);
	return static_cast<int>(sz.cx) - (w.empty() ? 0 : p.S(spacing));
}

void SpacedLine(Painter &p, int x, int y, int h, const std::wstring &w, int spacing, COLORREF c)
{
	SetTextColor(p.dc, c);
	RECT r = {p.S(x), p.S(y), p.S(x) + PixelWidth(p, w, spacing) + p.S(spacing) + 4, p.S(y + h)};
	SetTextCharacterExtra(p.dc, p.S(spacing));
	DrawTextW(p.dc, w.c_str(), static_cast<int>(w.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
	SetTextCharacterExtra(p.dc, 0);
}

// The first line of a text, in one line: for the row under a title.
std::string Excerpt(const std::string &text)
{
	std::string out;
	for (const char c : text) {
		if (c == '\r') continue;
		out += c == '\n' || c == '\t' ? ' ' : c;
		if (out.size() > 240) break;
	}
	return out;
}

// The icons are shapes over a box from 0 to 1, drawn with smooth edges; the pin and the flag are the chat's.
bool InCircle(double x, double y, double cx, double cy, double r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

bool InPolygon(double x, double y, const double (*pts)[2], int n)
{
	bool in = false;
	for (int i = 0, j = n - 1; i < n; j = i++) {
		if ((pts[i][1] > y) != (pts[j][1] > y) && x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]) in = !in;
	}
	return in;
}

double SegmentDistance(double x, double y, double ax, double ay, double bx, double by)
{
	const double dx = bx - ax, dy = by - ay, t = std::max(0.0, std::min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)));
	const double px = ax + t * dx - x, py = ay + t * dy - y;
	return std::sqrt(px * px + py * py);
}

// A map pin: a round head with a hole, its sides running from the tip along the tangents of the head.
bool InPin(double x, double y)
{
	static const double taper[][2] = {{0.239, 0.507}, {0.761, 0.507}, {0.5, 0.97}};
	if (InCircle(x, y, 0.5, 0.36, 0.115)) return false;
	return InCircle(x, y, 0.5, 0.36, 0.3) || InPolygon(x, y, taper, 3);
}

// A banner flying from its pole.
bool InFlag(double x, double y)
{
	static const double cloth[][2] = {{0.3, 0.08}, {0.6, 0.15}, {0.9, 0.09}, {0.9, 0.5}, {0.6, 0.57}, {0.3, 0.5}};
	const bool pole = x >= 0.16 && x <= 0.27 && y >= 0.04 && y <= 0.96;
	return pole || InCircle(x, y, 0.215, 0.05, 0.07) || InPolygon(x, y, cloth, 6);
}

// A round seal with a check cut through it.
bool InCheck(double x, double y)
{
	if (!InCircle(x, y, 0.5, 0.5, 0.47)) return false;
	const double stroke = 0.075;
	return SegmentDistance(x, y, 0.27, 0.5, 0.43, 0.66) > stroke && SegmentDistance(x, y, 0.43, 0.66, 0.74, 0.34) > stroke;
}

void NoteIcon(Painter &p, const ObjectiveEntry &e, int x, int y, bool selected)
{
	if (e.done) p.Shape(x, y, kIconBox, kIconBox, InCheck, kDoneMark);
	else p.Shape(x, y, kIconBox, kIconBox, e.note.onMap ? InPin : InFlag, selected ? pal::text : pal::redText);
}

int TagWidth(Painter &p, const std::string &label)
{
	p.Font(4);
	return static_cast<int>(PixelWidth(p, Upper(W(label)), 1) / p.s) + 12;
}

int Tag(Painter &p, int x, int y, const std::string &label)
{
	const int w = TagWidth(p, label), h = 17;
	p.Fill(x, y, w, h, pal::red);
	SpacedLine(p, x + 6, y, h, Upper(W(label)), 1, kTagText);
	return w;
}

std::vector<std::wstring> Wrap(Painter &p, const std::string &text, int width)
{
	std::vector<std::wstring> out;
	for (size_t from = 0;;) {
		const size_t nl = text.find('\n', from);
		std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) out.push_back(std::wstring());
		else {
			for (const std::wstring &l : p.WrapLines(line, 0, width)) out.push_back(l);
		}
		if (nl == std::string::npos) break;
		from = nl + 1;
	}
	while (!out.empty() && out.back().empty()) out.pop_back();
	return out;
}

int LinesHeight(const std::vector<std::wstring> &lines)
{
	int h = 0;
	for (const std::wstring &l : lines) h += l.empty() ? kGapLine : kLineH;
	return h;
}

void ScrollBar(Painter &p, const View &v, int id, int x, int top, int viewH, int total, int scroll)
{
	if (total <= viewH) return;
	ScrollbarLook look;
	look.width = kScrollW;
	look.minThumb = 30;
	look.track = pal::bg;
	PaintScrollbar(p, id, x, top, viewH, {scroll, total - viewH, viewH}, v.bar, v.mouseX, v.mouseY, look);
}

struct Row {
	int kind = 0;  // 0 group, 1 note, 2 nothing in progress
	int index = -1, y = 0, h = 0, count = 0;
	bool done = false;
};

std::vector<Row> Layout(const View &v, int &height)
{
	std::vector<Row> rows;
	const int progress = ObjectiveCountInProgress(v.entries), done = static_cast<int>(v.entries.size()) - progress;
	int y = 0;
	rows.push_back({0, -1, y, kGroupH, progress, false});
	y += kGroupH;
	if (!progress) {
		rows.push_back({2, -1, y, kEmptyRowH, 0, false});
		y += kEmptyRowH;
	}
	for (size_t i = 0; i < v.entries.size(); i++) {
		if (v.entries[i].done) continue;
		rows.push_back({1, static_cast<int>(i), y, kRowH, 0, false});
		y += kRowH;
	}
	if (done) {
		y += kGroupGap;
		rows.push_back({0, -1, y, kGroupH, done, true});
		y += kGroupH;
		for (size_t i = 0; i < v.entries.size(); i++) {
			if (!v.entries[i].done) continue;
			rows.push_back({1, static_cast<int>(i), y, kRowH, 0, true});
			y += kRowH;
		}
	}
	height = y;
	return rows;
}

void PaintGroup(Painter &p, int y, const Row &r)
{
	p.Font(4);
	const std::wstring up = Upper(W(Tr(r.done ? "Completed" : "In progress")));
	SpacedLine(p, kListX + 4, y + 8, 18, up, 2, pal::caption);
	const int tw = static_cast<int>(PixelWidth(p, up, 2) / p.s) + 12;
	const std::string count = std::to_string(r.count);
	const int cw = p.TextWidth(count);
	p.Text(kListX + kListW - cw - 4, y + 8, cw + 2, 18, count, pal::textDim);
	const int lineW = kListW - tw - cw - 22;
	if (lineW > 0) p.Fill(kListX + 4 + tw, y + 17, lineW, 1, pal::lineStrong);
}

void PaintNoteRow(Painter &p, const View &v, int y, const Row &r, bool inside, int hitTop, int hitH)
{
	const ObjectiveEntry &e = v.entries[r.index];
	const bool selected = r.index == v.selected;
	const bool hot = inside && !selected && p.Hover(kListX, y, kListW, r.h, v.mouseX, v.mouseY);
	if (selected) {
		p.Fill(kListX, y, kListW, r.h, kCurrentBg);
		p.Fill(kListX, y + 4, 3, r.h - 8, pal::redText);
	}
	else if (hot) {
		p.Fill(kListX, y, kListW, r.h, kHotBg);
	}
	NoteIcon(p, e, kListX + 12, y + 9, selected);
	const int textX = kListX + 42;
	int textW = kListX + kListW - 10 - textX;
	if (e.fresh && !e.done) {
		const int tagW = TagWidth(p, Tr("New"));
		Tag(p, kListX + kListW - 8 - tagW, y + 10, Tr("New"));
		textW -= tagW + 8;
	}
	p.Font(selected ? 1 : 0);
	const std::string title = e.note.title.empty() ? e.note.id : e.note.title;
	const COLORREF titleColor = e.done ? kDoneText : selected ? pal::text : hot ? pal::text : kPlain;
	p.Text(textX, y + 7, textW, 22, title, titleColor);
	if (e.done) {
		const int struck = std::min(textW, p.TextWidth(title));
		p.Fill(textX, y + 18, struck, 1, kDoneText);
	}
	p.Font(3);
	const std::string excerpt = Excerpt(e.note.text);
	if (!excerpt.empty()) p.Text(textX, y + 29, kListX + kListW - 10 - textX, 18, excerpt, e.done ? kDoneDim : selected ? pal::text : pal::textDim);
	if (hitH > 0) p.AddHit(kListX, hitTop, kListW, hitH, kActSelect, r.index);
}

void PaintList(Painter &p, const View &v, PaintOut &out)
{
	int height = 0;
	const std::vector<Row> rows = Layout(v, height);
	const int viewH = kListBottom - kTop;
	out.listH = height;
	out.listViewH = viewH;
	int scroll = std::max(0, std::min(v.listScroll, std::max(0, height - viewH)));
	if (v.revealSelected) {
		for (size_t i = 0; i < rows.size(); i++) {
			const Row &r = rows[i];
			if (r.kind != 1 || r.index != v.selected) continue;
			// The first row of a group brings its heading with it.
			const int top = i > 0 && rows[i - 1].kind == 0 ? rows[i - 1].y : r.y;
			if (top < scroll) scroll = std::max(0, top);
			else if (r.y + r.h > scroll + viewH) scroll = std::min(std::max(0, height - viewH), r.y + r.h - viewH);
		}
	}
	out.listScroll = scroll;
	RECT list = p.R(kListX, kTop, kListW, viewH);
	OffsetRect(&list, p.ox, p.oy);
	out.list = list;
	const POINT mouse = {v.mouseX, v.mouseY};
	const bool inside = PtInRect(&list, mouse) != 0;
	p.Clip(kListX - 2, kTop, kListW + 4, viewH);
	for (const Row &r : rows) {
		const int y = kTop + r.y - scroll;
		if (y + r.h <= kTop || y >= kListBottom) continue;
		const int hitTop = std::max(y, kTop), hitH = std::min(y + r.h, kListBottom) - hitTop;
		if (r.kind == 0) PaintGroup(p, y, r);
		else if (r.kind == 2) {
			p.Font(3);
			p.Text(kListX + 14, y, kListW - 20, r.h, Tr("Nothing in progress right now"), pal::textDim);
		}
		else PaintNoteRow(p, v, y, r, inside, hitTop, hitH);
	}
	p.Unclip();
	ScrollBar(p, v, kBarList, kListX + kListW + 5, kTop, viewH, height, scroll);
}

// Returns how much lower than usual the rest of the note starts.
int PaintTitle(Painter &p, const std::string &title, COLORREF c)
{
	const std::wstring w = W(title);
	p.Font(5);
	SetTextColor(p.dc, c);
	if (PixelWidth(p, w, 0) <= p.S(kPaneW)) {
		RECT r = p.R(kPaneX, kTitleY, kPaneW, kTitleH);
		DrawTextW(p.dc, w.c_str(), static_cast<int>(w.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		return 0;
	}
	p.Font(2);
	const std::vector<std::wstring> lines = p.WrapLines(title, 0, kPaneW);
	if (lines.size() == 1) {
		RECT r = p.R(kPaneX, kTitleY, kPaneW, kTitleH);
		DrawTextW(p.dc, w.c_str(), static_cast<int>(w.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		return 0;
	}
	std::wstring rest;
	for (size_t i = 1; i < lines.size(); i++) rest += (i > 1 ? L" " : L"") + lines[i];
	RECT first = p.R(kPaneX, kTitleY + 8, kPaneW, kTitleLineH), second = p.R(kPaneX, kTitleY + 8 + kTitleLineH, kPaneW, kTitleLineH);
	DrawTextW(p.dc, lines[0].c_str(), static_cast<int>(lines[0].size()), &first, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	DrawTextW(p.dc, rest.c_str(), static_cast<int>(rest.size()), &second, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	return kTitleLineH;
}

// The state of the note in a line: new, in progress or completed, and whether it has a place on this map.
void PaintStatus(Painter &p, const View &v, const ObjectiveEntry &e, int dy)
{
	int x = kPaneX;
	const int top = kStatusY + dy, y = top + (kStatusH - kIconBox) / 2;
	p.Font(3);
	if (e.done) {
		p.Shape(x, y, kIconBox, kIconBox, InCheck, kDoneMark);
		p.Text(x + kIconBox + 8, top, kPaneW - kIconBox - 8, kStatusH, Tr("Objective completed"), kDoneText);
		return;
	}
	if (e.fresh) x += Tag(p, x, top + 7, Tr("New")) + 10;
	p.Font(3);
	const std::string state = Tr("In progress");
	p.Text(x, top, p.TextWidth(state) + 4, kStatusH, state, pal::text);
	x += p.TextWidth(state) + 10;
	if (e.note.onMap) {
		p.Text(x, top, 12, kStatusH, "\xC2\xB7", pal::textDim);
		x += 14;
		p.Shape(x, y, kIconBox, kIconBox, InPin, pal::redText);
		x += kIconBox + 6;
		const std::string marked = Tr("Marked on the map");
		p.Text(x, top, kPaneX + kPaneW - kMapButtonW - 12 - x, kStatusH, marked, pal::textDim);
		p.Button(kPaneX + kPaneW - kMapButtonW, top, kMapButtonW, kStatusH, Tr("Show on map"), kActShowOnMap, 0, v.mouseX, v.mouseY, true, true, 4);
	}
}

void PaintText(Painter &p, const View &v, const ObjectiveEntry &e, PaintOut &out, int dy)
{
	const int textTop = kTextTop + dy, viewH = kBottom - textTop, width = kPaneW - 18;
	p.Font(0);
	const std::vector<std::wstring> lines = Wrap(p, e.note.text, width);
	const int height = LinesHeight(lines);
	out.textH = height;
	out.textViewH = viewH;
	const int scroll = std::max(0, std::min(v.textScroll, std::max(0, height - viewH)));
	out.textScroll = scroll;
	RECT text = p.R(kPaneX, textTop, kPaneW, viewH);
	OffsetRect(&text, p.ox, p.oy);
	out.text = text;
	p.Clip(kPaneX, textTop, kPaneW, viewH);
	int y = textTop - scroll;
	for (const std::wstring &l : lines) {
		if (l.empty()) {
			y += kGapLine;
			continue;
		}
		if (y + kLineH > textTop && y < kBottom) p.LineW(kPaneX, y + 2, width, kLineH, l, e.done ? pal::textDim : kPlain);
		y += kLineH;
	}
	p.Unclip();
	ScrollBar(p, v, kBarText, kPaneX + kPaneW - kScrollW, textTop, viewH, height, scroll);
}

void PaintEmpty(Painter &p)
{
	const int cx = kPaneX + kPaneW / 2, top = 236;
	const int big = 3 * kIconBox;
	// The flag of the list, three times larger.
	p.Shape(cx - big / 2, top, big, big, InFlag, pal::edge);
	p.Font(2);
	p.Text(kPaneX, top + 80, kPaneW, 32, Tr("No objectives yet"), pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	p.Font(0);
	p.Text(kPaneX + 40, top + 116, kPaneW - 80, 48, Tr("When the mission gives you an objective, it shows here."), pal::textDim, DT_CENTER | DT_TOP | DT_WORDBREAK);
}

}

void PaintObjectivesScreen(Painter &p, const View &v, PaintOut &out)
{
	out = PaintOut();
	// The notes are the game's words, already translated; the screen's own words go through Tr here.
	p.raw = true;
	PaintWindowCard(p, kScreenW, kScreenH);
	PaintWindowHeading(p, kScreenW, Tr("Objectives"));
	p.Fill(kDividerX, kTop + 4, 1, kBottom - kTop - 4, pal::hairline);
	PaintList(p, v, out);
	p.Fill(kListX, kListBottom + 6, kListW, 1, pal::lineStrong);
	p.Button(kListX, kCloseY, kListW, kCloseH, Tr("Close"), kActClose, 0, v.mouseX, v.mouseY);
	if (v.selected < 0 || v.selected >= static_cast<int>(v.entries.size())) {
		PaintEmpty(p);
		p.raw = false;
		return;
	}
	const ObjectiveEntry &e = v.entries[v.selected];
	const int dy = PaintTitle(p, e.note.title.empty() ? e.note.id : e.note.title, e.done ? kDoneText : pal::text);
	PaintStatus(p, v, e, dy);
	p.Fill(kPaneX, kRuleY + dy, kPaneW, 1, pal::hairline);
	PaintText(p, v, e, out, dy);
	p.raw = false;
}

}
