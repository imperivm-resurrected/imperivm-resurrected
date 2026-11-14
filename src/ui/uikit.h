// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <cmath>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "i18n/tr.h"
#include "ui/ornaments.h"

namespace ui {

// A scroll in the caller's own units, rows or pixels: `pos` is the first one shown, `max` the furthest `pos` goes and `page` how many fit
// at once.
struct ScrollSpan {
	int pos = 0, max = 0, page = 1;
};

// A thumb inside its track: its top below the track's top and its length, in pixels.
struct ThumbSpan {
	int top = 0, len = 0;
};

inline int ScrollClamp(int pos, int max) { return std::max(0, std::min(pos, max)); }

// The thumb is as long as the share of the content in view, and never shorter than `minLen`.
inline ThumbSpan ScrollThumb(const ScrollSpan &s, int trackLen, int minLen)
{
	if (trackLen <= 0) return {};
	if (s.max <= 0) return {0, trackLen};
	const long long page = std::max(1, s.page);
	const int len = std::min(trackLen, std::max(std::min(minLen, trackLen), static_cast<int>(trackLen * page / (s.max + page))));
	return {static_cast<int>(static_cast<long long>(trackLen - len) * ScrollClamp(s.pos, s.max) / s.max), len};
}

// The scroll that puts the thumb's top `thumbTop` pixels below the track's top: where a drag takes the content.
inline int ScrollAtThumb(int max, int trackLen, int thumbLen, int thumbTop)
{
	const int travel = trackLen - thumbLen;
	if (max <= 0 || travel <= 0) return 0;
	const long long top = std::max(0, std::min(thumbTop, travel));
	return static_cast<int>((top * max + travel / 2) / travel);
}

// One page up (dir < 0) or down from where the scroll is.
inline int ScrollPaged(const ScrollSpan &s, int dir) { return ScrollClamp(s.pos + (dir < 0 ? -1 : 1) * std::max(1, s.page), s.max); }

// A hit with this action is a scroll bar: `arg` is the id its screen gave it, `r` its track and the rest where its thumb was painted.
constexpr int kHitScrollbar = -0x5c0;

struct Hit {
	RECT r;
	int action;
	int arg;
	ScrollSpan span = {};
	int thumbTop = 0, thumbLen = 0;
};

inline std::wstring W(const std::string &s)
{
	if (s.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
	UINT cp = n > 0 ? CP_UTF8 : CP_ACP;
	DWORD flags = n > 0 ? MB_ERR_INVALID_CHARS : 0;
	n = MultiByteToWideChar(cp, flags, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(cp, flags, s.c_str(), -1, &w[0], n);
	return w;
}

// The one place that defines the colours of the interface.
namespace pal {
constexpr COLORREF bg = RGB(0x12, 0x11, 0x10), panel = RGB(0x18, 0x15, 0x13), raised = RGB(0x1c, 0x19, 0x17), selected = RGB(0x21, 0x1c, 0x1a),
				   field = RGB(0x1f, 0x1b, 0x19), button = RGB(0x2a, 0x26, 0x24), buttonHover = RGB(0x38, 0x33, 0x2f),
				   shadow = RGB(0x08, 0x08, 0x07);
constexpr COLORREF hairline = RGB(0x2a, 0x27, 0x25), edge = RGB(0x33, 0x30, 0x2d), lineStrong = RGB(0x3a, 0x35, 0x32), edgeHot = RGB(0x5a, 0x55, 0x50);
constexpr COLORREF text = RGB(0xf1, 0xee, 0xe8), body = RGB(0xd9, 0xd6, 0xcf), textDim = RGB(0xbd, 0xbb, 0xb4), caption = RGB(0x9e, 0x9b, 0x94),
				   disabled = RGB(0x6f, 0x6b, 0x66), onRed = RGB(0xff, 0xff, 0xff), onLight = RGB(0x12, 0x11, 0x10);
// red fills things; redText is the red that reads on dark backgrounds, the brand red fails contrast for small text.
constexpr COLORREF red = RGB(0xc4, 0x16, 0x1c), redPressed = RGB(0x9c, 0x10, 0x16), redText = RGB(0xe3, 0x47, 0x4c);
constexpr COLORREF ok = RGB(0x8c, 0xc8, 0x78), bad = redText, warn = RGB(0xe6, 0xbe, 0x5a);
// Every typeface of the interface; a different family only needs to change here. The brand's ship in
// imperivm-resurrected\fonts (LoadBrandFonts); the second of each pair is Windows' own, for when they are missing.
// Archivo's static SemiBold is a family of its own for GDI ("Archivo SemiBold", regular weight).
constexpr const wchar_t *kFontBody = L"Archivo", *kFontUi = L"Archivo", *kFontUiStrong = L"Archivo SemiBold", *kFontDisplay = L"League Gothic";
constexpr const wchar_t *kFontBodyFallback = L"Georgia", *kFontUiFallback = L"Segoe UI", *kFontDisplayFallback = L"Georgia";
constexpr const wchar_t *kBrandFontFiles[] = {L"LeagueGothic.ttf", L"Archivo-Regular.ttf", L"Archivo-Medium.ttf", L"Archivo-SemiBold.ttf", L"Archivo-Bold.ttf"};
}

// A token as a pixel of the frame buffers (0xRRGGBB), for screens that paint them directly.
constexpr uint32_t PixelOf(COLORREF c) { return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff); }

constexpr int kFontCount = 6;

inline bool InUnitEllipse(double x, double y) { return (x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5) <= 0.25; }
inline bool InUnitDiamond(double x, double y) { return std::fabs(x - 0.5) + std::fabs(y - 0.5) <= 0.5; }

// The coverage of a shape at a size in pixels, kept: the same few circles and lozenges are drawn on every frame.
inline std::vector<uint8_t> CachedShapeMask(bool (*inside)(double, double), int w, int h)
{
	static std::mutex lock;
	static std::map<std::tuple<bool (*)(double, double), int, int>, std::vector<uint8_t>> cache;
	std::lock_guard<std::mutex> guard(lock);
	const auto key = std::make_tuple(inside, w, h);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	if (cache.size() > 512) cache.clear();
	return cache[key] = ShapeMask(w, h, inside);
}

// Lays a colour over what the DC holds, in the share of each pixel the mask gives, as GDI's own shapes have no smooth edges. It goes
// through GdiAlphaBlend, which keeps to the DC's clip and to however its bitmap is laid out.
inline bool BlendCoverage(HDC dc, const RECT &r, const std::vector<uint8_t> &mask, COLORREF c)
{
	const int w = r.right - r.left, h = r.bottom - r.top;
	if (w <= 0 || h <= 0 || mask.size() != static_cast<size_t>(w) * h) return true;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp || !bits) {
		if (bmp) DeleteObject(bmp);
		return false;
	}
	uint32_t *px = static_cast<uint32_t *>(bits);
	const uint32_t cr = GetRValue(c), cg = GetGValue(c), cb = GetBValue(c);
	for (size_t i = 0; i < mask.size(); i++) {
		const uint32_t a = mask[i];
		px[i] = (a << 24) | (((cr * a + 127) / 255) << 16) | (((cg * a + 127) / 255) << 8) | ((cb * a + 127) / 255);
	}
	HDC mem = CreateCompatibleDC(dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	const bool ok = GdiAlphaBlend(dc, r.left, r.top, w, h, mem, 0, 0, w, h, blend) != FALSE;
	SelectObject(mem, old);
	DeleteDC(mem);
	DeleteObject(bmp);
	return ok;
}

struct Painter {
	HDC dc;
	float s;
	int ox, oy;
	std::vector<Hit> *hits;
	HFONT fonts[kFontCount] = {};
	int pressAction = -1, pressArg = 0;
	int focusAction = -1, focusArg = 0;
	bool noHover = false;
	// Names of players and texts the game already translated are drawn as they are: a player called "Spartacus" stays so.
	bool raw = false;

	int S(int v) const { return static_cast<int>(v * s + 0.5f); }
	RECT R(int x, int y, int w, int h) const { return {S(x), S(y), S(x + w), S(y + h)}; }

	void Font(int i) { SelectObject(dc, fonts[i]); }
	void Fill(int x, int y, int w, int h, COLORREF c)
	{
		RECT r = R(x, y, w, h);
		// Below scale 1 a thin line can round to no width at all, and frames lose a side.
		if (w > 0 && r.right <= r.left) r.right = r.left + 1;
		if (h > 0 && r.bottom <= r.top) r.bottom = r.top + 1;
		HBRUSH b = CreateSolidBrush(c);
		FillRect(dc, &r, b);
		DeleteObject(b);
	}
	// A shape over a box in pixels, with smooth edges.
	bool ShapeAt(const RECT &r, bool (*inside)(double, double), COLORREF c)
	{
		return BlendCoverage(dc, r, CachedShapeMask(inside, r.right - r.left, r.bottom - r.top), c);
	}
	void Shape(int x, int y, int w, int h, bool (*inside)(double, double), COLORREF c) { ShapeAt(R(x, y, w, h), inside, c); }
	void Diamond(int cx, int cy, int radius, COLORREF c)
	{
		const int x = S(cx), y = S(cy), r = S(radius);
		if (ShapeAt({x - r, y - r, x + r + 1, y + r + 1}, InUnitDiamond, c)) return;
		POINT pts[4] = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}};
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		Polygon(dc, pts, 4);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void ArrowUp(int x, int y, int w, int h, COLORREF c)
	{
		const RECT r = R(x, y, w, h);
		POINT pts[3] = {{r.left, r.bottom}, {r.right, r.bottom}, {(r.left + r.right) / 2, r.top}};
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		Polygon(dc, pts, 3);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void ArrowRight(int x, int y, int w, int h, COLORREF c)
	{
		const RECT r = R(x, y, w, h);
		POINT pts[3] = {{r.left, r.top}, {r.left, r.bottom}, {r.right, (r.top + r.bottom) / 2}};
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		Polygon(dc, pts, 3);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void ArrowLeft(int x, int y, int w, int h, COLORREF c)
	{
		const RECT r = R(x, y, w, h);
		POINT pts[3] = {{r.right, r.top}, {r.right, r.bottom}, {r.left, (r.top + r.bottom) / 2}};
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		Polygon(dc, pts, 3);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void Spinner(int cx, int cy, int radius, int dot, DWORD tick)
	{
		const int n = 12;
		const int head = static_cast<int>((tick / 70) % n);
		for (int i = 0; i < n; i++) {
			const double a = i * 6.283185307 / n - 1.570796327;
			const int px = cx + static_cast<int>(std::lround(std::cos(a) * radius)), py = cy + static_cast<int>(std::lround(std::sin(a) * radius));
			const int k = std::max(0, 255 - ((head - i + n) % n) * 22);
			Ellipse(px - dot / 2, py - dot / 2, dot, dot, RGB(42 + (227 - 42) * k / 255, 39 + (71 - 39) * k / 255, 37 + (76 - 37) * k / 255));
		}
	}
	void ArrowDown(int x, int y, int w, int h, COLORREF c)
	{
		const RECT r = R(x, y, w, h);
		POINT pts[3] = {{r.left, r.top}, {r.right, r.top}, {(r.left + r.right) / 2, r.bottom}};
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		Polygon(dc, pts, 3);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void Line(int x1, int y1, int x2, int y2, COLORREF c, int width = 1)
	{
		LOGBRUSH lb = {BS_SOLID, c, 0};
		HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, std::max(1, S(width)), &lb, 0, nullptr);
		HGDIOBJ old = SelectObject(dc, pen);
		MoveToEx(dc, S(x1), S(y1), nullptr);
		LineTo(dc, S(x2), S(y2));
		SelectObject(dc, old);
		DeleteObject(pen);
	}
	void Ellipse(int x, int y, int w, int h, COLORREF c)
	{
		if (ShapeAt(R(x, y, w, h), InUnitEllipse, c)) return;
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		const RECT r = R(x, y, w, h);
		::Ellipse(dc, r.left, r.top, r.right, r.bottom);
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void RoundRect(int x, int y, int w, int h, int radius, COLORREF c)
	{
		HBRUSH b = CreateSolidBrush(c);
		HPEN pen = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pen);
		const RECT r = R(x, y, w, h);
		::RoundRect(dc, r.left, r.top, r.right, r.bottom, S(radius), S(radius));
		SelectObject(dc, ob);
		SelectObject(dc, op);
		DeleteObject(b);
		DeleteObject(pen);
	}
	void Frame(int x, int y, int w, int h, COLORREF c)
	{
		Fill(x, y, w, 1, c);
		Fill(x, y + h - 1, w, 1, c);
		Fill(x, y, 1, h, c);
		Fill(x + w - 1, y, 1, h, c);
	}
	void Image(int x, int y, int w, int h, const uint32_t *bgra, int iw, int ih)
	{
		if (!bgra || iw <= 0 || ih <= 0) return;
		const RECT r = R(x, y, w, h);
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = iw;
		bi.bmiHeader.biHeight = -ih;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		const int old = SetStretchBltMode(dc, HALFTONE);
		SetBrushOrgEx(dc, 0, 0, nullptr);
		StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, iw, ih, bgra, &bi, DIB_RGB_COLORS, SRCCOPY);
		SetStretchBltMode(dc, old);
	}
	void Text(int x, int y, int w, int h, const std::string &t, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
	{
		RECT r = R(x, y, w, h);
		SetTextColor(dc, c);
		std::wstring ws = W(raw ? t : Tr(t));
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, fmt | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	// Uppercase, with the letter spacing a plain DrawText cannot align on its own.
	void TextCaps(int x, int y, int w, int h, const std::string &t, COLORREF c, UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE, int spacing = 1)
	{
		std::wstring ws = W(raw ? t : Tr(t));
		if (!ws.empty()) CharUpperBuffW(&ws[0], static_cast<DWORD>(ws.size()));
		SetTextColor(dc, c);
		const int old = SetTextCharacterExtra(dc, S(spacing));
		RECT r = R(x, y, w, h);
		SIZE sz = {};
		GetTextExtentPoint32W(dc, ws.c_str(), static_cast<int>(ws.size()), &sz);
		const int avail = r.right - r.left;
		// DrawText's own ellipsis leaves out the letter spacing, so a spaced label was cut mid-letter: trim it here instead.
		if (sz.cx > avail && (fmt & DT_SINGLELINE) && ws.size() > 1) {
			const std::wstring dots = L"\x2026";
			std::wstring cut = ws;
			while (cut.size() > 1) {
				cut.pop_back();
				while (!cut.empty() && cut.back() == L' ') cut.pop_back();
				const std::wstring tryText = cut + dots;
				GetTextExtentPoint32W(dc, tryText.c_str(), static_cast<int>(tryText.size()), &sz);
				if (sz.cx <= avail) break;
			}
			ws = cut + dots;
		}
		if (sz.cx < avail) {
			if (fmt & DT_CENTER) r.left += (avail - sz.cx) / 2;
			else if (fmt & DT_RIGHT) r.left = r.right - sz.cx;
		}
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, ((fmt & ~(DT_CENTER | DT_RIGHT)) | DT_LEFT) | DT_NOPREFIX | DT_END_ELLIPSIS);
		SetTextCharacterExtra(dc, old);
	}
	int CapsWidth(const std::string &t, int spacing = 1)
	{
		std::wstring ws = W(raw ? t : Tr(t));
		if (!ws.empty()) CharUpperBuffW(&ws[0], static_cast<DWORD>(ws.size()));
		const int old = SetTextCharacterExtra(dc, S(spacing));
		SIZE sz = {};
		GetTextExtentPoint32W(dc, ws.c_str(), static_cast<int>(ws.size()), &sz);
		SetTextCharacterExtra(dc, old);
		return static_cast<int>(sz.cx / s + 0.99f);
	}
	int TextWrapped(int x, int y, int w, int h, const std::string &t, COLORREF c)
	{
		RECT r = R(x, y, w, h);
		SetTextColor(dc, c);
		std::wstring ws = W(raw ? t : Tr(t));
		RECT calc = r;
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &calc, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
		return static_cast<int>((std::min(calc.bottom, r.bottom) - r.top) / s);
	}
	int Measure(int w, const std::string &t)
	{
		RECT r = {0, 0, S(w), 0};
		std::wstring ws = W(raw ? t : Tr(t));
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
		return static_cast<int>((r.bottom - r.top) / s + 0.99f);
	}
	int TextWidth(const std::string &t)
	{
		const std::wstring ws = W(raw ? t : Tr(t));
		SIZE sz = {};
		GetTextExtentPoint32W(dc, ws.c_str(), static_cast<int>(ws.size()), &sz);
		return static_cast<int>(sz.cx / s + 0.99f);
	}
	float TextWidthExact(const std::string &t)
	{
		const std::wstring ws = W(raw ? t : Tr(t));
		SIZE sz = {};
		GetTextExtentPoint32W(dc, ws.c_str(), static_cast<int>(ws.size()), &sz);
		return static_cast<float>(sz.cx) / s;
	}
	std::vector<std::wstring> WrapLines(const std::string &t, int firstIndent, int width)
	{
		const std::wstring ws = W(t);
		std::vector<std::wstring> lines;
		std::wstring cur;
		auto fits = [&](const std::wstring &text, int limit) {
			SIZE sz = {};
			GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
			return sz.cx <= S(limit);
		};
		auto limit = [&]() { return lines.empty() ? width - firstIndent : width; };
		size_t pos = 0;
		while (pos <= ws.size()) {
			size_t end = ws.find(L' ', pos);
			if (end == std::wstring::npos) end = ws.size();
			std::wstring word = ws.substr(pos, end - pos);
			pos = end + 1;
			if (word.empty() && end != ws.size()) {
				if (!cur.empty()) cur += L' ';
				continue;
			}
			const std::wstring joined = cur.empty() ? word : cur + L' ' + word;
			if (fits(joined, limit())) {
				cur = joined;
				continue;
			}
			if (!cur.empty()) {
				lines.push_back(cur);
				cur.clear();
			}
			while (!word.empty() && !fits(word, limit())) {
				int count = 0;
				SIZE sz = {};
				GetTextExtentExPointW(dc, word.c_str(), static_cast<int>(word.size()), S(limit()), &count, nullptr, &sz);
				if (count < 1) count = 1;
				lines.push_back(word.substr(0, count));
				word.erase(0, count);
			}
			cur = word;
		}
		if (!cur.empty() || lines.empty()) lines.push_back(cur);
		return lines;
	}
	void LineW(int x, int y, int w, int h, const std::wstring &ws, COLORREF c)
	{
		RECT r = R(x, y, w, h);
		SetTextColor(dc, c);
		DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
	}
	void Clip(int x, int y, int w, int h)
	{
		const RECT r = R(x, y, w, h);
		HRGN g = CreateRectRgn(r.left, r.top, r.right, r.bottom);
		SelectClipRgn(dc, g);
		DeleteObject(g);
	}
	void Unclip() { SelectClipRgn(dc, nullptr); }
	void AddHit(int x, int y, int w, int h, int action, int arg = 0)
	{
		RECT r = R(x, y, w, h);
		OffsetRect(&r, ox, oy);
		hits->push_back({r, action, arg});
		if (action >= 0 && action == focusAction && arg == focusArg) {
			Frame(x, y, w, h, pal::text);
			Frame(x + 1, y + 1, w - 2, h - 2, pal::text);
		}
	}
	bool Hover(int x, int y, int w, int h, int mx, int my) const
	{
		if (noHover) return false;
		RECT r = R(x, y, w, h);
		OffsetRect(&r, ox, oy);
		POINT p = {mx, my};
		return PtInRect(&r, p) != 0;
	}
	static const int kToggleW = 26, kToggleH = 13;
	void Toggle(int x, int y, bool on, bool enabled, bool hover)
	{
		Fill(x, y, kToggleW, kToggleH, !enabled ? pal::field : on ? (hover ? pal::redPressed : pal::red) : pal::field);
		Frame(x, y, kToggleW, kToggleH, !enabled ? pal::hairline : on ? (hover ? pal::redPressed : pal::red) : hover ? pal::edgeHot : pal::edge);
		const int kx = on ? x + kToggleW - kToggleH + 2 : x + 2, knob = kToggleH - 4;
		Fill(kx, y + 2, knob, knob, !enabled ? pal::disabled : on ? pal::onRed : pal::caption);
	}
	void Button(int x, int y, int w, int h, const std::string &label, int action, int arg, int mx, int my,
		bool enabled = true, bool strong = false, int font = 1)
	{
		const bool hov = enabled && Hover(x, y, w, h, mx, my);
		const bool down = enabled && action == pressAction && arg == pressArg;
		const COLORREF fill = !enabled ? pal::field : strong ? (hov || down ? pal::redPressed : pal::red) : (hov || down ? pal::buttonHover : pal::button);
		Fill(x, y, w, h, fill);
		Font(font);
		// A label too long for a narrow button takes the small type before it is shortened.
		if (font == 1 && CapsWidth(label, 1) > w - 16) Font(4);
		TextCaps(x, y, w, h, label, !enabled ? pal::disabled : strong ? pal::onRed : pal::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		if (enabled) AddHit(x, y, w, h, action, arg);
	}
};

inline void DrawDiscordIcon(Painter &p, int x, int y, int size)
{
	const COLORREF blurple = RGB(88, 101, 242);
	p.RoundRect(x, y + size / 8, size, size * 3 / 4, size / 3, blurple);
	p.Ellipse(x + size / 6, y + size * 3 / 4 - size / 10, size / 4, size / 4, blurple);
	p.Ellipse(x + size - size / 6 - size / 4, y + size * 3 / 4 - size / 10, size / 4, size / 4, blurple);
	const int eye = size / 5 > 3 ? size / 5 : 3;
	p.Ellipse(x + size / 4, y + size * 3 / 8, eye, eye + 1, RGB(255, 255, 255));
	p.Ellipse(x + size - size / 4 - eye, y + size * 3 / 8, eye, eye + 1, RGB(255, 255, 255));
}

inline std::string HumanBytes(uint64_t b)
{
	char buf[32];
	if (b >= 1024ull * 1024 * 1024) sprintf_s(buf, "%.1f GB", b / 1073741824.0);
	else if (b >= 1024 * 1024) sprintf_s(buf, "%.1f MB", b / 1048576.0);
	else sprintf_s(buf, "%.0f KB", b / 1024.0);
	return buf;
}

inline std::string TransferText(uint64_t done, uint64_t total, double perSecond)
{
	std::string out;
	if (total) out = std::to_string(std::min<uint64_t>(100, done * 100 / total)) + " %  ·  ";
	out += HumanBytes(done) + (total ? Tr(" of ") + HumanBytes(total) : "");
	if (perSecond > 1024) out += "  ·  " + HumanBytes(static_cast<uint64_t>(perSecond)) + "/s";
	return out;
}

// The bar a screen is dragging, kept between messages: the id it painted it with and how far below the thumb's top it was taken.
struct ScrollDrag {
	int id = -1;
	int grab = 0;
};

// Where a bar asks its screen to scroll: none when `id` is below 0.
struct ScrollTo {
	int id = -1;
	int pos = 0;
};

struct ScrollbarLook {
	int width = 6;
	// The track is centred under the thumb: -1 makes it as wide as the thumb, 0 leaves it out.
	int trackWidth = -1;
	int minThumb = 20;
	// How far to each side of the thumb the mouse still takes the bar.
	int grip = 4;
	COLORREF track = pal::field, thumb = pal::lineStrong;
};

// Draws the track and the thumb, lit while the mouse is over the bar or drags it, and returns where the thumb went.
inline ThumbSpan DrawScrollbar(Painter &p, int x, int y, int h, const ScrollSpan &span, bool dragged, int mx, int my, const ScrollbarLook &look = {})
{
	if (span.max <= 0 || h <= 0) return {};
	const ThumbSpan t = ScrollThumb(span, h, look.minThumb);
	const int trackW = look.trackWidth < 0 ? look.width : look.trackWidth;
	if (trackW > 0) p.Fill(x + (look.width - trackW) / 2, y, trackW, h, look.track);
	const bool hot = dragged || p.Hover(x - look.grip, y, look.width + 2 * look.grip, h, mx, my);
	p.Fill(x, y + t.top, look.width, t.len, hot ? pal::edgeHot : look.thumb);
	return t;
}

// A vertical bar whose thumb's left edge is at x. It goes into the hits with the screen's `id`, so ScrollbarMouse can drag and page it.
inline ThumbSpan PaintScrollbar(Painter &p, int id, int x, int y, int h, const ScrollSpan &span, const ScrollDrag &drag, int mx, int my,
	const ScrollbarLook &look = {})
{
	const ThumbSpan t = DrawScrollbar(p, x, y, h, span, id >= 0 && drag.id == id, mx, my, look);
	if (span.max <= 0 || h <= 0 || !p.hits) return t;
	p.AddHit(x - look.grip, y, look.width + 2 * look.grip, h, kHitScrollbar, id);
	Hit &hit = p.hits->back();
	hit.span = {ScrollClamp(span.pos, span.max), span.max, std::max(1, span.page)};
	hit.thumbTop = p.oy + p.S(y + t.top);
	hit.thumbLen = p.S(y + t.top + t.len) - p.S(y + t.top);
	return t;
}

inline const Hit *TopHitAt(const std::vector<Hit> &hits, POINT at)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (PtInRect(&it->r, at)) return &*it;
	}
	return nullptr;
}

// Gives a mouse message to the bars of the last frame's hits. A press on a thumb drags it until the button is let go, even outside the bar,
// and keeps the point where it was taken under the mouse; a press on the track pages towards the click. True when a bar took the message,
// and then `to` says which bar scrolls and to where.
inline bool ScrollbarMouse(ScrollDrag &drag, const std::vector<Hit> &hits, UINT msg, WPARAM wp, POINT at, ScrollTo &to)
{
	to = ScrollTo();
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const Hit *bar = TopHitAt(hits, at);
		if (!bar || bar->action != kHitScrollbar) return false;
		if (at.y >= bar->thumbTop && at.y < bar->thumbTop + bar->thumbLen) {
			drag.id = bar->arg;
			drag.grab = at.y - bar->thumbTop;
		}
		else {
			to.id = bar->arg;
			to.pos = ScrollPaged(bar->span, at.y < bar->thumbTop ? -1 : 1);
		}
		return true;
	}
	case WM_MOUSEMOVE: {
		if (drag.id < 0) return false;
		// The release can happen where the window doesn't hear it.
		if (!(wp & MK_LBUTTON)) {
			drag = ScrollDrag();
			return false;
		}
		const Hit *bar = nullptr;
		for (auto it = hits.rbegin(); it != hits.rend() && !bar; ++it) {
			if (it->action == kHitScrollbar && it->arg == drag.id) bar = &*it;
		}
		if (!bar) {
			drag = ScrollDrag();
			return false;
		}
		to.id = drag.id;
		to.pos = ScrollAtThumb(bar->span.max, bar->r.bottom - bar->r.top, bar->thumbLen, at.y - drag.grab - bar->r.top);
		return true;
	}
	case WM_LBUTTONUP:
		if (drag.id < 0) return false;
		drag = ScrollDrag();
		return true;
	default: return false;
	}
}

struct Canvas {
	HDC dc = nullptr;
	HBITMAP bmp = nullptr;
	HGDIOBJ old = nullptr;
	uint32_t *bits = nullptr;
	int w = 0, h = 0;
	bool Ensure(int nw, int nh)
	{
		if (dc && nw == w && nh == h) return true;
		Release();
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = nw;
		bi.bmiHeader.biHeight = -nh;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		dc = CreateCompatibleDC(nullptr);
		bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void **>(&bits), nullptr, 0);
		if (!dc || !bmp) {
			Release();
			return false;
		}
		old = SelectObject(dc, bmp);
		SetBkMode(dc, TRANSPARENT);
		w = nw;
		h = nh;
		return true;
	}
	void Release()
	{
		if (dc && old) SelectObject(dc, old);
		if (bmp) DeleteObject(bmp);
		if (dc) DeleteDC(dc);
		dc = nullptr;
		bmp = nullptr;
		bits = nullptr;
		w = h = 0;
	}
};

inline HFONT g_fonts[kFontCount] = {};
inline float g_fontScale = 0;

// The brand's typefaces, for this process only (nothing is installed in Windows): true when all of them loaded.
inline bool LoadBrandFonts()
{
	static int loaded = -1;
	if (loaded >= 0) return loaded != 0;
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir(exe);
	dir = dir.substr(0, dir.find_last_of(L'\\') + 1) + L"imperivm-resurrected\\fonts\\";
	int ok = 0;
	for (const wchar_t *file : pal::kBrandFontFiles) ok += AddFontResourceExW((dir + file).c_str(), FR_PRIVATE, nullptr) > 0 ? 1 : 0;
	loaded = ok == static_cast<int>(sizeof(pal::kBrandFontFiles) / sizeof(pal::kBrandFontFiles[0])) ? 1 : 0;
	return loaded != 0;
}

// The text and interface families for screens that make their own fonts.
inline const wchar_t *BodyFace()
{
	return LoadBrandFonts() ? pal::kFontBody : pal::kFontBodyFallback;
}
inline const wchar_t *UiFace()
{
	return LoadBrandFonts() ? pal::kFontUi : pal::kFontUiFallback;
}

enum class BrandFace { Body, BodyBold, Ui, UiStrong, Display };

// A brand face at a size the shared set doesn't have, for the screens laid out on the whole screen. Made once per size in pixels and
// never deleted: a frame's DC may still hold the last one, and only a change of resolution adds new ones.
inline HFONT BrandFont(BrandFace face, int size, float s)
{
	static std::mutex lock;
	static std::map<std::pair<int, int>, HFONT> cache;
	const int px = std::max(1, static_cast<int>(size * s + 0.5f));
	std::lock_guard<std::mutex> guard(lock);
	const auto key = std::make_pair(static_cast<int>(face), px);
	const auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	const bool brand = LoadBrandFonts();
	const wchar_t *name = pal::kFontBodyFallback;
	int weight = FW_NORMAL;
	switch (face) {
	case BrandFace::Body: name = brand ? pal::kFontBody : pal::kFontBodyFallback; break;
	case BrandFace::BodyBold:
		name = brand ? pal::kFontBody : pal::kFontBodyFallback;
		weight = FW_BOLD;
		break;
	case BrandFace::Ui: name = brand ? pal::kFontUi : pal::kFontUiFallback; break;
	case BrandFace::UiStrong:
		name = brand ? pal::kFontUiStrong : pal::kFontUiFallback;
		weight = brand ? FW_NORMAL : FW_SEMIBOLD;
		break;
	// League Gothic has one weight: asking it bold would make Windows smear a fake one.
	case BrandFace::Display: name = brand ? pal::kFontDisplay : pal::kFontDisplayFallback; break;
	}
	return cache[key] = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			   DEFAULT_PITCH, name);
}

inline void EnsureFonts(float s)
{
	if (g_fonts[0] && g_fontScale == s) return;
	for (HFONT &f : g_fonts) {
		if (f) DeleteObject(f);
	}
	auto make = [s](int size, int weight, const wchar_t *face) {
		return CreateFontW(-static_cast<int>(size * s + 0.5f), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
	};
	if (LoadBrandFonts()) {
		// League Gothic has one weight: asking it bold would make Windows smear a fake one.
		g_fonts[0] = make(15, FW_NORMAL, pal::kFontBody);
		g_fonts[1] = make(15, FW_BOLD, pal::kFontBody);
		g_fonts[2] = make(22, FW_NORMAL, pal::kFontDisplay);
		g_fonts[3] = make(13, FW_NORMAL, pal::kFontUi);
		g_fonts[4] = make(13, FW_NORMAL, pal::kFontUiStrong);
		g_fonts[5] = make(30, FW_NORMAL, pal::kFontDisplay);
	}
	else {
		g_fonts[0] = make(15, FW_NORMAL, pal::kFontBodyFallback);
		g_fonts[1] = make(15, FW_BOLD, pal::kFontBodyFallback);
		g_fonts[2] = make(22, FW_BOLD, pal::kFontDisplayFallback);
		g_fonts[3] = make(13, FW_NORMAL, pal::kFontUiFallback);
		g_fonts[4] = make(13, FW_SEMIBOLD, pal::kFontUiFallback);
		g_fonts[5] = make(30, FW_NORMAL, pal::kFontDisplayFallback);
	}
	g_fontScale = s;
}

inline void Blend(uint32_t *buf, int w, int h, const Canvas &c, int ox, int oy, int alpha)
{
	for (int y = 0; y < c.h; y++) {
		const int ty = oy + y;
		if (ty < 0 || ty >= h) continue;
		for (int x = 0; x < c.w; x++) {
			const int tx = ox + x;
			if (tx < 0 || tx >= w) continue;
			uint32_t src = c.bits[static_cast<size_t>(y) * c.w + x], &dst = buf[static_cast<size_t>(ty) * w + tx];
			uint32_t rb = ((src & 0xff00ff) * alpha + (dst & 0xff00ff) * (255 - alpha)) >> 8;
			uint32_t g = ((src & 0x00ff00) * alpha + (dst & 0x00ff00) * (255 - alpha)) >> 8;
			dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
		}
	}
}

}
