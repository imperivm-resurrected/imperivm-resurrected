// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ui/uikit.h"

namespace ui {

struct ListLine {
	std::string text;
	COLORREF color = pal::textDim;
	bool heading = false;
};

struct ListState {
	int scroll = 0;
	bool drag = false;
	int grab = 0;
};

struct ListGeom {
	RECT area = {};
	RECT track = {};
	RECT thumb = {};
	int total = 0, visible = 0, maxScroll = 0;
};

inline int ListMaxScroll(int total, int visible) { return std::max(0, total - visible); }

inline void ScrollbarPaint(Painter &p, int tx, int ty, int th, int total, int visible, const ListState &st, ListGeom &geom, int mx = -10000,
	int my = -10000)
{
	geom.total = total;
	geom.visible = visible;
	geom.maxScroll = ListMaxScroll(total, visible);
	geom.track = geom.thumb = RECT();
	if (geom.maxScroll <= 0 || th <= 0) return;
	const int thumbH = std::max(20, th * visible / total);
	const int scroll = std::max(0, std::min(st.scroll, geom.maxScroll));
	const int thumbY = ty + (th - thumbH) * scroll / geom.maxScroll;
	const bool hot = st.drag || p.Hover(tx - 4, ty, 14, th, mx, my);
	p.Fill(tx, ty, 6, th, pal::field);
	p.Fill(tx, thumbY, 6, thumbH, hot ? pal::edgeHot : pal::lineStrong);
	RECT t = p.R(tx - 4, ty, 14, th);
	OffsetRect(&t, p.ox, p.oy);
	geom.track = t;
	RECT c = p.R(tx - 4, thumbY, 14, thumbH);
	OffsetRect(&c, p.ox, p.oy);
	geom.thumb = c;
}

inline void ListPaint(Painter &p, int x, int y, int w, int h, const std::vector<ListLine> &lines, ListState &st, ListGeom &geom, int font = 0,
	int rowH = 22, int mx = -10000, int my = -10000)
{
	const int total = static_cast<int>(lines.size());
	const int visible = std::max(1, (h - 4) / rowH);
	const int maxScroll = ListMaxScroll(total, visible);
	st.scroll = std::max(0, std::min(st.scroll, maxScroll));
	const bool bar = maxScroll > 0;
	p.Fill(x, y, w, h, pal::bg);
	p.Frame(x, y, w, h, pal::lineStrong);
	const int textW = w - 12 - (bar ? 12 : 0);
	for (int i = 0; i < visible && st.scroll + i < total; i++) {
		const ListLine &l = lines[st.scroll + i];
		p.Font(l.heading ? 1 : font);
		p.Text(x + 8 + (l.heading ? 0 : 6), y + 2 + i * rowH, textW - (l.heading ? 0 : 6), rowH, l.text, l.color,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	geom = ListGeom();
	RECT a = p.R(x, y, w, h);
	OffsetRect(&a, p.ox, p.oy);
	geom.area = a;
	ScrollbarPaint(p, x + w - 10, y + 3, h - 6, total, visible, st, geom, mx, my);
}

inline bool ListWheel(ListState &st, const ListGeom &g, POINT pt, int wheelDelta, int step = 3)
{
	if (g.maxScroll <= 0 || !PtInRect(&g.area, pt)) return false;
	st.scroll = std::max(0, std::min(g.maxScroll, st.scroll - (wheelDelta / WHEEL_DELTA) * step));
	return true;
}

inline int ListScrollFor(const ListGeom &g, int thumbTop)
{
	const int range = static_cast<int>((g.track.bottom - g.track.top) - (g.thumb.bottom - g.thumb.top));
	if (range <= 0) return 0;
	const int rel = std::max(0, std::min(range, thumbTop - static_cast<int>(g.track.top)));
	return (rel * g.maxScroll + range / 2) / range;
}

inline bool ListDown(ListState &st, const ListGeom &g, POINT pt)
{
	if (g.maxScroll <= 0 || !PtInRect(&g.track, pt)) return false;
	const int thumbH = static_cast<int>(g.thumb.bottom - g.thumb.top);
	if (PtInRect(&g.thumb, pt)) {
		st.grab = static_cast<int>(pt.y - g.thumb.top);
	}
	else {
		st.grab = thumbH / 2;
		st.scroll = ListScrollFor(g, pt.y - st.grab);
	}
	st.drag = true;
	return true;
}

inline bool ListMove(ListState &st, const ListGeom &g, POINT pt)
{
	if (!st.drag) return false;
	st.scroll = ListScrollFor(g, pt.y - st.grab);
	return true;
}

inline bool ListUp(ListState &st)
{
	const bool was = st.drag;
	st.drag = false;
	return was;
}

}

namespace ui {

inline std::string ShortList(const std::vector<std::string> &names, size_t max = 3)
{
	std::string out;
	for (size_t i = 0; i < names.size() && i < max; i++) out += (i ? ", " : "") + names[i];
	if (names.size() > max) out += Tr(" and ") + std::to_string(names.size() - max) + Tr(" more");
	return out;
}

}
