// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>

#include "ui/uikit.h"

namespace ui {

// The bars of the panes go into the hits with these ids and up, so they never meet the ids a screen gives its own bars.
constexpr int kPaneIdBase = 0x7400;

inline int PaneMax(int contentH, int viewH) { return std::max(0, contentH - std::max(0, viewH)); }

// How far a notch of the wheel moves a pane `viewH` tall: an eighth of it, within what reads as one step.
inline int PaneWheelStep(int viewH) { return std::max(40, std::min(120, viewH / 8)); }

// How tall each of `rows` rows can be for `fixed + rows * height` to fit `avail`: never taller than `rowH` nor shorter than `minRowH`.
// What still doesn't fit at `minRowH` scrolls.
inline int FitRowHeight(int avail, int fixed, int rows, int rowH, int minRowH)
{
	if (rows <= 0) return rowH;
	return std::max(minRowH, std::min(rowH, (avail - fixed) / rows));
}

// The panes of the last frame under `at`, innermost first: a pane inside another is published after it.
inline const PaneArea *PaneAt(const std::vector<PaneArea> &panes, POINT at)
{
	for (auto it = panes.rbegin(); it != panes.rend(); ++it) {
		if (it->max > 0 && PtInRect(&it->r, at)) return &*it;
	}
	return nullptr;
}

// Where a screen's panes are scrolled, in design units, by the id each was painted with.
struct PaneScrolls {
	std::map<int, int> pos;
	int wheelRest = 0;

	int Of(int id) const
	{
		const auto it = pos.find(id);
		return it == pos.end() ? 0 : it->second;
	}
	void Set(int id, int value, int max) { pos[id] = ScrollClamp(value, max); }
	void Reset()
	{
		pos.clear();
		wheelRest = 0;
	}
	// A WM_MOUSEWHEEL over a pane: true when one was under the mouse and took it.
	bool Wheel(const std::vector<PaneArea> &panes, POINT at, int delta)
	{
		const PaneArea *pane = PaneAt(panes, at);
		if (!pane) return false;
		const int steps = WheelSteps(wheelRest, delta);
		Set(pane->id, Of(pane->id) - steps * PaneWheelStep(pane->page), pane->max);
		return true;
	}
	// Where ScrollbarMouse sent a pane's bar: true when `to` was one of them.
	bool Apply(const std::vector<PaneArea> &panes, const ScrollTo &to)
	{
		if (to.id < 0) return false;
		for (const PaneArea &pane : panes) {
			if (pane.id != to.id) continue;
			Set(pane.id, to.pos, pane.max);
			return true;
		}
		return false;
	}
	// Scrolls a pane as little as it takes to show the content from `top` to `top + h` (in the content's units).
	void Reveal(const std::vector<PaneArea> &panes, int id, int top, int h)
	{
		for (const PaneArea &pane : panes) {
			if (pane.id != id) continue;
			int p = Of(id);
			if (top < p) p = top;
			else if (top + h > p + pane.page) p = top + h - pane.page;
			Set(id, p, pane.max);
			return;
		}
	}
};

// The hits a pane leaves: those inside it, cut to it, so what scrolled out of view can't be clicked through what is around it.
inline void KeepHitsInside(std::vector<Hit> &hits, size_t from, const RECT &view)
{
	size_t out = from;
	for (size_t i = from; i < hits.size(); i++) {
		RECT r;
		if (!IntersectRect(&r, &hits[i].r, &view)) continue;
		hits[out] = hits[i];
		// A bar keeps its whole track: its drag maps the mouse along it.
		if (hits[i].action != kHitScrollbar) hits[out].r = r;
		out++;
	}
	hits.resize(out);
}

struct PaneSpec {
	int id = kPaneIdBase;
	// The part of the screen it shows, in design units.
	int x = 0, y = 0, w = 0, h = 0;
	int contentH = 0;
	// Where the bar's left edge goes: by default inside the right edge.
	int barX = -1;
	const ScrollDrag *drag = nullptr;
	int mx = -10000, my = -10000;
	ScrollbarLook look = {};
};

// A part of the screen that what is painted inside can't leave: the painting is clipped to it (a Clip inside only narrows it), the
// mouse is over nothing outside it, and the hits that fall outside are dropped. They can nest.
class ClipArea {
public:
	ClipArea(Painter &p, int x, int y, int w, int h) : p_(p)
	{
		wasBounded_ = p.bounded;
		oldBounds_ = p.bounds;
		RECT view = p.R(x, y, w, h);
		if (p.bounded && !IntersectRect(&view, &view, &p.bounds)) view = {0, 0, 0, 0};
		p.bounded = true;
		p.bounds = view;
		p.Unclip();
		hitsFrom_ = p.hits ? p.hits->size() : 0;
	}
	~ClipArea() { End(); }
	ClipArea(const ClipArea &) = delete;
	ClipArea &operator=(const ClipArea &) = delete;

	// Where it is on the screen, in pixels.
	RECT OnScreen() const
	{
		RECT r = p_.bounded ? p_.bounds : RECT{};
		OffsetRect(&r, p_.ox, p_.oy);
		return r;
	}
	void End()
	{
		if (ended_) return;
		ended_ = true;
		if (p_.hits) KeepHitsInside(*p_.hits, hitsFrom_, OnScreen());
		p_.bounded = wasBounded_;
		p_.bounds = oldBounds_;
		p_.Unclip();
	}

private:
	Painter &p_;
	size_t hitsFrom_ = 0;
	bool wasBounded_ = false, ended_ = false;
	RECT oldBounds_ = {};
};

// How tall a block comes out, painted where nothing shows and nothing can be clicked: for blocks that only know their height once
// painted, to size the pane they go in.
template <class Paint> int MeasuredHeight(Painter &p, Paint paint)
{
	ClipArea nowhere(p, 0, 0, 0, 0);
	return paint();
}

// Content taller than its part of the screen, painted scrolled: put it at Top() and below, as if the pane were as tall as the content.
// It keeps to its part of the screen as a ClipArea does, and when it ends it paints its bar. Panes can nest.
class ScrollPane {
public:
	ScrollPane(Painter &p, const PaneSpec &spec, int pos) : p_(p), spec_(spec), clip_(p, spec.x, spec.y, spec.w, spec.h)
	{
		max_ = PaneMax(spec.contentH, spec.h);
		pos_ = ScrollClamp(pos, max_);
		if (p.panes) p.panes->push_back({clip_.OnScreen(), spec.id, max_, spec.h});
	}
	~ScrollPane() { End(); }
	ScrollPane(const ScrollPane &) = delete;
	ScrollPane &operator=(const ScrollPane &) = delete;

	// Where the top of the content goes.
	int Top() const { return spec_.y - pos_; }
	int Pos() const { return pos_; }
	int Max() const { return max_; }
	bool Scrolls() const { return max_ > 0; }
	// Whether a block painted from `top`, `h` tall, shows in the pane: the ones that don't can be left out.
	bool Shows(int top, int h) const { return top < spec_.y + spec_.h && top + h > spec_.y; }

	void End()
	{
		if (ended_) return;
		ended_ = true;
		clip_.End();
		if (max_ <= 0) return;
		const int barX = spec_.barX >= 0 ? spec_.barX : spec_.x + spec_.w - spec_.look.width;
		PaintScrollbar(p_, spec_.id, barX, spec_.y, spec_.h, {pos_, max_, spec_.h}, spec_.drag ? *spec_.drag : ScrollDrag(), spec_.mx, spec_.my, spec_.look);
	}

private:
	Painter &p_;
	PaneSpec spec_;
	ClipArea clip_;
	int pos_ = 0, max_ = 0;
	bool ended_ = false;
};

}
