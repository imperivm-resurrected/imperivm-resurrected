// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <cstdint>

#include "ui/listbox.h"
#include "ui/ornaments.h"
#include "ui/textedit.h"
#include "ui/uikit.h"

namespace ui {

inline float FitScale(int w, int h, int designW, int designH, int margin = 24)
{
	return std::max(0.6f, std::min(1.6f, std::min((w - margin) / static_cast<float>(designW), (h - margin) / static_cast<float>(designH))));
}

inline void DimBackdrop(uint32_t *buf, int w, int h)
{
	for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; i++) buf[i] = 0xff000000u | ((buf[i] >> 1) & 0x7f7f7f);
}

struct PanelRender {
	Canvas &canvas;
	uint32_t *buf;
	int w, h;
	float s = 1.0f;
	int ox = 0, oy = 0, pw = 0, ph = 0;
	std::vector<Hit> hits;
	Painter p{};

	PanelRender(Canvas &c, uint32_t *b, int bw, int bh) : canvas(c), buf(b), w(bw), h(bh) {}
	PanelRender(const PanelRender &) = delete;
	PanelRender &operator=(const PanelRender &) = delete;

	bool Begin(int designW, int designH, int margin = 24, bool dim = true, float forceScale = 0.0f)
	{
		s = forceScale > 0.0f ? forceScale : FitScale(w, h, designW, designH, margin);
		if (dim) DimBackdrop(buf, w, h);
		pw = static_cast<int>(designW * s + 0.5f);
		ph = static_cast<int>(designH * s + 0.5f);
		ox = (w - pw) / 2;
		oy = (h - ph) / 2;
		if (!canvas.Ensure(pw, ph)) return false;
		EnsureFonts(s);
		FieldsBeginFrame();
		p = Painter{canvas.dc, s, ox, oy, &hits};
		std::copy(std::begin(g_fonts), std::end(g_fonts), std::begin(p.fonts));
		return true;
	}

	void Focus(int action, int arg)
	{
		p.focusAction = action;
		p.focusArg = arg;
	}

	void Press(const std::vector<Hit> &previous, bool mouseDown, POINT mouse)
	{
		if (!mouseDown) return;
		for (auto it = previous.rbegin(); it != previous.rend(); ++it) {
			if (PtInRect(&it->r, mouse)) {
				p.pressAction = it->action;
				p.pressArg = it->arg;
				return;
			}
		}
	}

	void End(int alpha = 255)
	{
		GdiFlush();
		Blend(buf, w, h, canvas, ox, oy, alpha);
	}
};

// dirX and dirY say where the corner opens: +1 to the right or down, -1 to the left or up.
inline void PaintCorner(Painter &p, int x, int y, int dirX, int dirY, int size = 22)
{
	const auto bar = [&](int ax, int ay, int w, int h, COLORREF c) {
		p.Fill(dirX > 0 ? x + ax : x - ax - w, dirY > 0 ? y + ay : y - ay - h, w, h, c);
	};
	bar(0, 0, size, 2, pal::goldLight);
	bar(0, 0, 2, size, pal::goldLight);
	bar(6, 6, size - 10, 1, kGold);
	bar(6, 6, 1, size - 10, kGold);
	p.Diamond(x + dirX * 6, y + dirY * 6, 3, pal::goldLight);
}

inline void PaintFrameCorners(Painter &p, int x, int y, int w, int h, int size = 22)
{
	PaintCorner(p, x, y, 1, 1, size);
	PaintCorner(p, x + w, y, -1, 1, size);
	PaintCorner(p, x, y + h, 1, -1, size);
	PaintCorner(p, x + w, y + h, -1, -1, size);
}

inline void PaintWindowFrame(Painter &p, int w, int h)
{
	const int glow = std::min(h, 120);
	p.FillV(0, 0, w, glow, pal::panelTop, pal::panel);
	p.Fill(0, glow, w, h - glow, pal::panel);
	p.Frame(0, 0, w, h, kGold);
	p.Frame(4, 4, w - 8, h - 8, pal::lineDim);
	PaintFrameCorners(p, 0, 0, w, h);
}

inline void PaintMeander(Painter &p, int x, int y, int w, COLORREF c = pal::line)
{
	constexpr int tile = 24;
	const int count = w / tile;
	x += (w - count * tile) / 2;
	for (int i = 0; i < count; i++, x += tile) {
		p.Fill(x, y + 11, tile, 1, c);
		p.Fill(x + 19, y + 2, 1, 10, c);
		p.Fill(x + 6, y + 2, 14, 1, c);
		p.Fill(x + 6, y + 2, 1, 7, c);
		p.Fill(x + 6, y + 8, 9, 1, c);
		p.Fill(x + 14, y + 5, 1, 4, c);
		p.Fill(x + 10, y + 5, 5, 1, c);
	}
}

// Premultiplied 32-bit bitmaps for GdiAlphaBlend, one per size, side and colour; there are only a handful.
inline HBITMAP LaurelBranchBitmap(int w, int h, bool flip, COLORREF color)
{
	static std::map<std::tuple<int, int, bool, COLORREF>, HBITMAP> cache;
	const auto key = std::make_tuple(w, h, flip, color);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (bmp && bits) {
		const std::vector<uint8_t> mask = LaurelBranchMask(w, h);
		uint32_t *px = static_cast<uint32_t *>(bits);
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				const uint32_t a = mask[static_cast<size_t>(y) * w + (flip ? w - 1 - x : x)];
				px[static_cast<size_t>(y) * w + x] = (a << 24) | ((GetRValue(color) * a / 255) << 16) | ((GetGValue(color) * a / 255) << 8) | (GetBValue(color) * a / 255);
			}
		}
	}
	cache[key] = bmp;
	return bmp;
}

inline void PaintLaurelBranch(Painter &p, int x, int y, int w, int h, bool flip, COLORREF color = pal::goldLight)
{
	const RECT r = p.R(x, y, w, h);
	const int pw = r.right - r.left, ph = r.bottom - r.top;
	HBITMAP bmp = pw > 0 && ph > 0 ? LaurelBranchBitmap(pw, ph, flip, color) : nullptr;
	if (!bmp) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, pw, ph, mem, 0, 0, pw, ph, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

constexpr int kWindowHeaderH = 84;

// The title between laurels over a Greek key band. Returns where the content can start.
inline int PaintWindowHeader(Painter &p, int w, const std::string &title)
{
	p.Font(5);
	SetTextCharacterExtra(p.dc, p.S(6));
	const int textW = p.TextWidth(title);
	p.Text(0, 12, w, 48, title, pal::goldLight, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SetTextCharacterExtra(p.dc, 0);
	constexpr int branchW = 66, branchH = 42, gap = 30;
	PaintLaurelBranch(p, (w - textW) / 2 - gap - branchW, 15, branchW, branchH, false);
	PaintLaurelBranch(p, (w + textW) / 2 + gap, 15, branchW, branchH, true);
	PaintMeander(p, 18, 64, w - 36);
	return kWindowHeaderH;
}

inline void PaintSlider(Painter &p, int x, int y, int w, int h, float fraction, bool enabled, bool hot)
{
	constexpr int trackH = 5;
	const int cy = y + h / 2, filled = static_cast<int>(w * std::max(0.0f, std::min(1.0f, fraction)));
	p.Fill(x, cy - trackH / 2, w, trackH, pal::well);
	p.Frame(x, cy - trackH / 2, w, trackH, pal::lineDim);
	if (filled > 0) {
		if (enabled) p.FillH(x, cy - trackH / 2, filled, trackH, pal::bronze, pal::goldLight);
		else p.Fill(x, cy - trackH / 2, filled, trackH, pal::disabledEdge);
	}
	if (!enabled) return;
	p.Diamond(x + filled, cy, hot ? 8 : 7, RGB(62, 44, 14));
	p.Diamond(x + filled, cy, hot ? 7 : 6, hot ? pal::focus : pal::goldLight);
}

inline void PaintTitle(Painter &p, int x, int y, int w, const std::string &title)
{
	p.Font(2);
	p.Text(x, y, w, 36, title, kGold);
}

inline void PaintRule(Painter &p, int y, int w, COLORREF c = pal::line)
{
	p.Fill(16, y, w - 32, 1, c);
}

inline void PaintCombo(Painter &p, int x, int y, int w, int h, const std::string &text, bool open, bool enabled, int mx, int my, int action,
	int arg, RECT *anchor = nullptr)
{
	const bool hov = enabled && p.Hover(x, y, w, h, mx, my);
	p.Fill(x, y, w, h, open ? pal::fieldOpen : hov ? pal::fieldHover : pal::field);
	p.Frame(x, y, w, h, !enabled ? pal::disabledEdge : open || hov ? pal::edgeHot : pal::edge);
	p.Font(0);
	p.Text(x + 8, y, w - 30, h, text, enabled ? kText : kDim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	if (enabled) {
		p.Fill(x + w - 24, y + 4, 1, h - 8, pal::divider);
		p.ArrowDown(x + w - 17, y + h / 2 - 2, 10, 5, open || hov ? pal::focus : kGold);
		p.AddHit(x, y, w, h, action, arg);
	}
	if (open && anchor) *anchor = {x, y, x + w, y + h};
}

struct DropItem {
	std::string text;
	COLORREF color = kText;
	bool disabled = false;
	std::string tip;
};

inline std::vector<DropItem> ToDropItems(const std::vector<std::string> &texts)
{
	std::vector<DropItem> out;
	out.reserve(texts.size());
	for (const std::string &t : texts) out.push_back({t});
	return out;
}

struct DropLayout {
	int x = 0, y = 0, w = 0, h = 0;
	int rows = 0, count = 0, scroll = 0;
};

constexpr int kMenuRowH = 24, kMenuMaxRows = 12;

inline int DropMaxScroll(int count, int rows) { return std::max(0, count - rows); }

inline DropLayout DropMenuLayout(const RECT &anchor, int count, int scroll, int canvasW, int canvasH, int minW = 200)
{
	DropLayout d;
	d.count = count;
	d.rows = std::min(count, kMenuMaxRows);
	d.w = std::max<int>(anchor.right - anchor.left, minW);
	d.h = d.rows * kMenuRowH + 4;
	d.x = std::max(8, std::min<int>(anchor.left, canvasW - 16 - d.w));
	d.y = anchor.bottom + 2;
	if (d.y + d.h > canvasH - 8) d.y = std::max<int>(8, anchor.top - 2 - d.h);
	d.scroll = std::max(0, std::min(scroll, DropMaxScroll(count, d.rows)));
	return d;
}

inline DropLayout PaintDropMenu(Painter &p, const RECT &anchor, const std::vector<DropItem> &items, int selected, int scroll, int canvasW,
	int canvasH, int pickAction, int closeAction, int mx, int my, int minW = 200)
{
	const int count = static_cast<int>(items.size());
	const DropLayout d = DropMenuLayout(anchor, count, scroll, canvasW, canvasH, minW);
	if (!count) return d;
	p.hits->clear();
	p.AddHit(0, 0, canvasW, canvasH, closeAction, 0);
	p.Fill(d.x + 4, d.y + 4, d.w, d.h, pal::shadow);
	p.Fill(d.x, d.y, d.w, d.h, pal::dialog);
	p.Frame(d.x, d.y, d.w, d.h, kGold);
	p.Font(0);
	const int textW = d.w - 26;
	int tipRow = -1;
	for (int r = 0; r < d.rows; r++) {
		const int i = d.scroll + r;
		const int ry = d.y + 2 + r * kMenuRowH;
		const DropItem &it = items[i];
		const bool hov = !it.disabled && p.Hover(d.x + 2, ry, d.w - 4, kMenuRowH, mx, my);
		if (i == selected) p.Fill(d.x + 2, ry, d.w - 4, kMenuRowH, pal::rowSelected);
		else if (hov) p.Fill(d.x + 2, ry, d.w - 4, kMenuRowH, pal::rowHover);
		const COLORREF c = it.disabled ? kDim : i == selected ? pal::focus : it.color;
		p.Text(d.x + 10, ry, textW, kMenuRowH, it.text.empty() ? "—" : it.text, c);
		if (!it.disabled) p.AddHit(d.x + 2, ry, d.w - 4, kMenuRowH, pickAction, i);
		if (hov && !it.tip.empty()) tipRow = r;
	}
	if (tipRow >= 0) {
		const DropItem &it = items[d.scroll + tipRow];
		p.Font(3);
		const int tw = 300, th = p.Measure(tw - 20, it.tip) + 14;
		int tx = d.x + d.w + 8;
		if (tx + tw > canvasW - 8) tx = d.x - 8 - tw;
		const int ty = std::max(8, std::min(d.y + 2 + tipRow * kMenuRowH, canvasH - 8 - th));
		p.Fill(tx + 3, ty + 3, tw, th, pal::shadow);
		p.Fill(tx, ty, tw, th, pal::dialog);
		p.Frame(tx, ty, tw, th, pal::edge);
		p.TextWrapped(tx + 10, ty + 7, tw - 20, th - 10, it.tip, kText);
		p.Font(0);
	}
	if (count > d.rows) {
		const int bh = d.h - 4, th = std::max(16, bh * d.rows / count), ty = d.y + 2 + (bh - th) * d.scroll / (count - d.rows);
		p.Fill(d.x + d.w - 8, d.y + 2, 5, bh, pal::well);
		p.Fill(d.x + d.w - 8, ty, 5, th, pal::thumb);
	}
	return d;
}

struct TabSpec {
	std::string label;
	int action = 0;
	int arg = 0;
	bool active = false;
	bool enabled = true;
};

constexpr int kTabH = 46;
constexpr int kTabsY = 56, kTabsH = 34;

inline void PaintTabs(Painter &p, int x, int y, const std::vector<TabSpec> &tabs, int mx, int my, int tabW = 190, int h = kTabH)
{
	for (const TabSpec &t : tabs) {
		const bool hov = t.enabled && p.Hover(x, y, tabW, h, mx, my);
		if (t.active) p.FillV(x, y, tabW, h, RGB(30, 22, 14), RGB(64, 46, 25));
		else if (hov) p.Fill(x, y, tabW, h, RGB(38, 30, 21));
		p.Font(1);
		p.Text(x, y, tabW, h - 4, t.label, !t.enabled ? RGB(110, 96, 76) : t.active ? kGold : hov ? kText : kDim,
			DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (t.active) p.Fill(x + 16, y + h - 4, tabW - 32, 3, kGold);
		if (t.enabled && !t.active) p.AddHit(x, y, tabW, h, t.action, t.arg);
		x += tabW + 6;
	}
}

inline void PaintTabBar(Painter &p, int w, const std::vector<TabSpec> &tabs, int mx, int my, int tabW = 190)
{
	PaintTabs(p, 16, kTabsY, tabs, mx, my, tabW, kTabsH);
	PaintRule(p, kTabsY + kTabsH, w);
}

struct Dialog {
	int x = 0, y = 0, w = 0, h = 0;
	int pad = 24;
	int BodyX() const { return x + pad; }
	int BodyW() const { return w - pad * 2; }
	int BodyTop() const { return y + 60; }
	int FooterTop() const { return y + h - 56; }
};

constexpr int kDialogBtnH = 40;

inline Dialog DialogBegin(Painter &p, int canvasW, int canvasH, int w, int h, const std::string &title, COLORREF titleColor = kGold,
	int titleIndent = 0)
{
	Dialog d;
	d.w = w;
	d.h = h;
	d.x = (canvasW - w) / 2;
	d.y = (canvasH - h) / 2;
	p.hits->clear();
	p.noHover = false;
	p.Fill(d.x + 6, d.y + 6, w, h, pal::shadow);
	p.FillV(d.x, d.y, w, std::min(h, 72), pal::dialogTop, pal::dialog);
	if (h > 72) p.Fill(d.x, d.y + 72, w, h - 72, pal::dialog);
	p.Frame(d.x, d.y, w, h, kGold);
	PaintFrameCorners(p, d.x, d.y, w, h, 14);
	p.Font(2);
	p.Text(d.x + d.pad + titleIndent, d.y + 16, w - d.pad * 2 - titleIndent, 32, title, titleColor, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	return d;
}

inline int DialogText(Painter &p, const Dialog &d, int yOffset, const std::string &text, COLORREF c = kText, int font = 0)
{
	p.Font(font);
	const int top = d.y + yOffset;
	return p.TextWrapped(d.BodyX(), top, d.BodyW(), std::max(20, d.FooterTop() - top - 8), text, c);
}

struct DialogButton {
	std::string label;
	int action = 0;
	int arg = 0;
	bool strong = false;
	bool enabled = true;
};

inline void DialogButtons(Painter &p, const Dialog &d, const std::vector<DialogButton> &buttons, int mx, int my, bool center = false)
{
	constexpr int gap = 12, minW = 120;
	p.Font(1);
	std::vector<int> widths;
	int total = -gap;
	for (const DialogButton &b : buttons) {
		widths.push_back(std::max(minW, p.TextWidth(b.label) + 44));
		total += widths.back() + gap;
	}
	int x = center ? d.x + (d.w - total) / 2 : d.x + d.w - d.pad - total;
	for (size_t i = 0; i < buttons.size(); i++) {
		const DialogButton &b = buttons[i];
		p.Button(x, d.FooterTop(), widths[i], kDialogBtnH, b.label, b.action, b.arg, mx, my, b.enabled, b.strong);
		x += widths[i] + gap;
	}
}

inline void PaintSwitch(Painter &p, int x, int y, bool on, const std::string &label, bool enabled, int action, int arg, int mx, int my)
{
	constexpr int h = 24;
	p.Font(0);
	const int textW = p.TextWidth(label) + 4;
	const int w = Painter::kToggleW + 10 + textW;
	const bool hov = enabled && p.Hover(x, y, w, h, mx, my);
	p.Toggle(x, y + (h - Painter::kToggleH) / 2, on, enabled, hov);
	p.Text(x + Painter::kToggleW + 10, y, textW, h, label, on && enabled ? kText : kDim);
	if (enabled) p.AddHit(x, y, w, h, action, arg);
}

inline void PaintAlertIcon(Painter &p, int x, int y, bool severe, int size = 16)
{
	p.Fill(x, y, size, size, severe ? kBad : kGold);
	p.Font(1);
	p.Text(x, y, size, size, "!", RGB(20, 15, 10), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

inline void PaintProgress(Painter &p, int x, int y, int w, int h, float fraction)
{
	p.Fill(x, y, w, h, pal::well);
	const int filled = static_cast<int>(w * std::max(0.0f, std::min(1.0f, fraction)));
	if (filled > 0) p.FillV(x, y, filled, h, pal::goldLight, pal::bronze);
}

inline void PaintBusyBar(Painter &p, int x, int y, int w, int h, DWORD now)
{
	const int seg = std::max(20, w / 8);
	p.Fill(x, y, w, h, pal::well);
	p.Fill(x + static_cast<int>((now / 8) % (w - seg)), y, seg, h, kGold);
}

inline void PaintBadge(Painter &p, int x, int y, int w, int h, const std::string &text, COLORREF bg, COLORREF fg)
{
	p.Fill(x, y, w, h, bg);
	p.Font(3);
	p.Text(x, y, w, h, text, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

}
