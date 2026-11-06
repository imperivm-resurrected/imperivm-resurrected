// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
namespace {
std::atomic<DWORD> g_updatesAt{0};
std::atomic<int> g_updatesCount{0};
}

void ResetUpdatesCache()
{
	g_updatesAt = 0;
}

int UpdatesAvailable()
{
	const DWORD now = GetTickCount();
	if (g_updatesAt && now - g_updatesAt < 5000) return g_updatesCount;
	g_updatesAt = now;
	const std::vector<ModInfo> mods = ModsSummaries();
	int n = 0;
	for (const ModInfo &m : mods) {
		CatalogEntry e;
		if (DlUpdateFor(m, mods, e)) n++;
	}
	g_updatesCount = n;
	return n;
}

DiscordIconState g_icon;
CardState g_card;

Painter CardPainter(HDC dc, float s)
{
	Painter p{dc, s, 0, 0, nullptr};
	for (int i = 0; i < kFontCount; i++) p.fonts[i] = g_fonts[i];
	return p;
}

RECT DrawKeysCard(Canvas &canvas, uint32_t *buf, int w, int h, const Card &c, float s, int alpha)
{
	EnsureFonts(s);
	const std::wstring ws = W(Tr(c.text));
	const int padX = static_cast<int>(18 * s), padY = static_cast<int>(10 * s), bar = std::max(3, static_cast<int>(4 * s));
	const int capH = 26, gap = static_cast<int>(10 * s);
	HDC measure = CreateCompatibleDC(nullptr);
	SelectObject(measure, g_fonts[3]);
	SIZE text = {};
	GetTextExtentPoint32W(measure, ws.c_str(), static_cast<int>(ws.size()), &text);
	Painter mp = CardPainter(measure, s);
	const int keysW = static_cast<int>(KeysWidth(mp, c.keys) * s + 0.5f);
	DeleteDC(measure);
	const int textGap = ws.empty() ? 0 : gap;
	const int tw = std::min(w - 20, bar + padX + static_cast<int>(text.cx) + textGap + keysW + padX);
	const int th = std::max(static_cast<int>(text.cy), static_cast<int>(capH * s + 0.5f)) + padY * 2;
	if (!canvas.Ensure(tw, th)) return {0, 0, 0, 0};
	HDC dc = canvas.dc;
	RECT all = {0, 0, tw, th};
	HBRUSH b = CreateSolidBrush(RGB(26, 21, 15));
	FillRect(dc, &all, b);
	DeleteObject(b);
	HBRUSH frame = CreateSolidBrush(pal::edge);
	FrameRect(dc, &all, frame);
	DeleteObject(frame);
	RECT accent = {0, 0, bar, th};
	HBRUSH ab = CreateSolidBrush(c.accent);
	FillRect(dc, &accent, ab);
	DeleteObject(ab);
	SelectObject(dc, g_fonts[3]);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(236, 224, 196));
	RECT r = {bar + padX, 0, bar + padX + static_cast<int>(text.cx), th};
	DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	Painter p = CardPainter(dc, s);
	DrawKeys(p, static_cast<int>((r.right + textGap) / s + 0.5f), static_cast<int>((th / s - capH) / 2), capH, c.keys);
	GdiFlush();
	const int x = c.atX >= 0 ? c.atX : (w - tw) / 2;
	const int y = c.atX >= 0 ? c.atCenterY - th / 2 : h - th - static_cast<int>(70 * s);
	Blend(buf, w, h, canvas, x, y, alpha);
	return {x, y, x + tw, y + th};
}

RECT DrawCard(Canvas &canvas, uint32_t *buf, int w, int h, const Card &c, float s, int alpha)
{
	if (!c.keys.empty() && c.progress < 0 && !c.close) return DrawKeysCard(canvas, buf, w, h, c, s, alpha);
	EnsureFonts(s);
	const std::wstring ws = W(Tr(c.text)), sub = W(Tr(c.sub));
	const int padX = static_cast<int>(22 * s), padY = static_cast<int>(12 * s), bar = std::max(3, static_cast<int>(4 * s));
	const int closeW = c.close ? static_cast<int>(22 * s) : 0;
	HDC measure = CreateCompatibleDC(nullptr);
	SelectObject(measure, g_fonts[3]);
	const int maxText = c.fixedW ? static_cast<int>(c.fixedW * s) - padX * 2 - bar - closeW : static_cast<int>(640 * s);
	RECT calc = {0, 0, maxText, 0};
	DrawTextW(measure, ws.c_str(), static_cast<int>(ws.size()), &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	TEXTMETRICW tm = {};
	GetTextMetricsW(measure, &tm);
	DeleteDC(measure);
	const int barH = c.progress >= 0 ? std::max(4, static_cast<int>(8 * s)) : 0;
	const int gap = c.progress >= 0 ? static_cast<int>(8 * s) : 0;
	const int subH = c.progress >= 0 && !sub.empty() ? tm.tmHeight + static_cast<int>(4 * s) : 0;
	const int tw = c.fixedW ? static_cast<int>(c.fixedW * s) : std::min(w - 20, static_cast<int>(calc.right) + padX * 2 + bar);
	const int th = static_cast<int>(calc.bottom) + padY * 2 + gap + barH + subH;
	if (!canvas.Ensure(tw, th)) return {0, 0, 0, 0};
	HDC dc = canvas.dc;
	RECT all = {0, 0, tw, th};
	HBRUSH b = CreateSolidBrush(RGB(26, 21, 15));
	FillRect(dc, &all, b);
	DeleteObject(b);
	HBRUSH frame = CreateSolidBrush(pal::edge);
	FrameRect(dc, &all, frame);
	DeleteObject(frame);
	RECT accent = {0, 0, bar, th};
	HBRUSH ab = CreateSolidBrush(c.accent);
	FillRect(dc, &accent, ab);
	DeleteObject(ab);
	SelectObject(dc, g_fonts[3]);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(236, 224, 196));
	RECT r = {bar + padX, padY, tw - padX - closeW, padY + static_cast<int>(calc.bottom)};
	DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, (c.corner ? DT_LEFT : DT_CENTER) | DT_WORDBREAK | DT_NOPREFIX);
	if (c.progress >= 0) {
		const int by = padY + static_cast<int>(calc.bottom) + gap;
		RECT track = {bar + padX, by, tw - padX - closeW, by + barH};
		HBRUSH tb = CreateSolidBrush(RGB(14, 11, 8));
		FillRect(dc, &track, tb);
		DeleteObject(tb);
		RECT fill = track;
		fill.right = track.left + static_cast<LONG>((track.right - track.left) * std::max(0.0f, std::min(1.0f, c.progress)));
		HBRUSH fb = CreateSolidBrush(RGB(214, 170, 84));
		FillRect(dc, &fill, fb);
		DeleteObject(fb);
		if (subH) {
			SetTextColor(dc, RGB(160, 146, 116));
			RECT sr = {bar + padX, by + barH + static_cast<int>(4 * s), tw - padX - closeW, th};
			DrawTextW(dc, sub.c_str(), static_cast<int>(sub.size()), &sr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
	}
	if (c.close) {
		const int sz = static_cast<int>(8 * s), cx = tw - static_cast<int>(16 * s), cy = static_cast<int>(16 * s);
		HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(1.5f * s)), RGB(190, 170, 130));
		HGDIOBJ old = SelectObject(dc, pen);
		MoveToEx(dc, cx - sz / 2, cy - sz / 2, nullptr);
		LineTo(dc, cx + sz / 2 + 1, cy + sz / 2 + 1);
		MoveToEx(dc, cx + sz / 2, cy - sz / 2, nullptr);
		LineTo(dc, cx - sz / 2 - 1, cy + sz / 2 + 1);
		SelectObject(dc, old);
		DeleteObject(pen);
	}
	GdiFlush();
	const int x = c.atX >= 0 ? c.atX : c.corner ? w - tw - static_cast<int>(16 * s) : (w - tw) / 2;
	const int y = c.atX >= 0 ? c.atCenterY - th / 2 : c.corner ? static_cast<int>(16 * s) : h - th - static_cast<int>(70 * s);
	Blend(buf, w, h, canvas, x, y, alpha);
	return {x, y, x + tw, y + th};
}
}
