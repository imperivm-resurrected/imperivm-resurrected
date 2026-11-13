// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ui/keycaps.h"

namespace ui {

void ArrowGlyph(Painter &p, int cx, int cy, unsigned short vk, COLORREF c)
{
	const int r = 4;
	POINT pts[3];
	const int x = p.S(cx), y = p.S(cy), d = p.S(r), e = p.S(r + 1);
	switch (vk) {
	case VK_LEFT: pts[0] = {x - e, y}; pts[1] = {x + d, y - e}; pts[2] = {x + d, y + e}; break;
	case VK_RIGHT: pts[0] = {x + e, y}; pts[1] = {x - d, y - e}; pts[2] = {x - d, y + e}; break;
	case VK_UP: pts[0] = {x, y - e}; pts[1] = {x - e, y + d}; pts[2] = {x + e, y + d}; break;
	default: pts[0] = {x, y + e}; pts[1] = {x - e, y - d}; pts[2] = {x + e, y - d}; break;
	}
	HBRUSH b = CreateSolidBrush(c);
	HPEN pen = CreatePen(PS_SOLID, 1, c);
	HGDIOBJ ob = SelectObject(p.dc, b), op = SelectObject(p.dc, pen);
	Polygon(p.dc, pts, 3);
	SelectObject(p.dc, ob);
	SelectObject(p.dc, op);
	DeleteObject(b);
	DeleteObject(pen);
}

bool IsArrow(unsigned short vk)
{
	return vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN;
}

std::vector<std::string> CapTexts(ControlKey k)
{
	std::vector<std::string> caps;
	if (k.mods & kKeyCtrl) caps.push_back("Ctrl");
	if (k.mods & kKeyShift) caps.push_back("Shift");
	if (k.mods & kKeyAlt) caps.push_back("Alt");
	caps.push_back(IsArrow(k.vk) ? "" : ControlsKeyLabel({k.vk, 0}));
	return caps;
}

int CapWidth(Painter &p, const std::string &text)
{
	return text.empty() ? 28 : std::max(28, p.TextWidth(text) + 16);
}

void DrawCap(Painter &p, int x, int y, int w, int h, const std::string &text, unsigned short vk, COLORREF edge, COLORREF fg,
	COLORREF face)
{
	p.RoundRect(x, y + 2, w, h - 2, 7, pal::bg);
	p.RoundRect(x, y, w, h - 2, 7, edge);
	p.RoundRect(x + 1, y + 1, w - 2, h - 4, 6, face);
	p.Fill(x + 5, y + 2, w - 10, 1, pal::lineStrong);
	if (text.empty() && IsArrow(vk)) ArrowGlyph(p, x + w / 2, y + (h - 2) / 2, vk, fg);
	else p.Text(x, y, w, h - 2, text, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawKey(Painter &p, int x, int y, int w, int h, ControlKey k, COLORREF edge, COLORREF fg, COLORREF face)
{
	p.Font(4);
	const std::vector<std::string> caps = CapTexts(k);
	const int plusW = 12;
	int total = 0;
	for (size_t i = 0; i < caps.size(); i++) total += CapWidth(p, caps[i]) + (i ? plusW : 0);
	const int capH = h - 6, cy = y + 3;
	if (total > w - 8) {
		DrawCap(p, x + 4, cy, w - 8, capH, ControlsKeyLabel(k), k.vk, edge, fg, face);
		return;
	}
	int cx = x + (w - total) / 2;
	for (size_t i = 0; i < caps.size(); i++) {
		if (i) {
			p.Text(cx, cy, plusW, capH, "+", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			cx += plusW;
		}
		const int cw = CapWidth(p, caps[i]);
		DrawCap(p, cx, cy, cw, capH, caps[i], k.vk, edge, fg, face);
		cx += cw;
	}
}

std::vector<ControlKey> KnownKeys(const std::vector<ControlKey> &keys)
{
	std::vector<ControlKey> out;
	for (const ControlKey &k : keys) {
		if (k.vk) out.push_back(k);
	}
	return out;
}

int KeysWidth(Painter &p, const std::vector<ControlKey> &keys)
{
	p.Font(4);
	const int plusW = 12, orW = 16;
	int total = 0;
	for (size_t i = 0; i < keys.size(); i++) {
		const std::vector<std::string> caps = CapTexts(keys[i]);
		if (i) total += orW;
		for (size_t c = 0; c < caps.size(); c++) total += CapWidth(p, caps[c]) + (c ? plusW : 0);
	}
	return total;
}

void DrawKeys(Painter &p, int x, int y, int h, const std::vector<ControlKey> &keys)
{
	p.Font(4);
	const int plusW = 12, orW = 16;
	const COLORREF edge = pal::edgeHot, fg = pal::body;
	int cx = x;
	for (size_t i = 0; i < keys.size(); i++) {
		if (i) {
			p.Text(cx, y, orW, h - 2, "/", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			cx += orW;
		}
		const std::vector<std::string> caps = CapTexts(keys[i]);
		for (size_t c = 0; c < caps.size(); c++) {
			if (c) {
				p.Text(cx, y, plusW, h - 2, "+", pal::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				cx += plusW;
			}
			const int cw = CapWidth(p, caps[c]);
			DrawCap(p, cx, y, cw, h, caps[c], keys[i].vk, edge, fg);
			cx += cw;
		}
	}
}

}
