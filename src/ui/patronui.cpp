// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/patronui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

struct Leaf {
	double cx, cy, angle, a, b;
};

// The same laurel as the website, built from pointed leaves along two arcs so it stays sharp at any size.
std::vector<Leaf> LaurelLeaves()
{
	std::vector<Leaf> leaves;
	const double cx = 0.5, cy = 0.5, r = 0.36;
	for (int i = 0; i < 6; i++) {
		const double t = (112 + i * 24) * kPi / 180, s = 1 - i * 0.06;
		const double px = cx + r * std::cos(t), py = cy + r * std::sin(t);
		const double nx = std::cos(t), ny = std::sin(t), dx = -std::sin(t), dy = std::cos(t);
		const double along = std::atan2(dy, dx);
		leaves.push_back({px + nx * 0.075 + dx * 0.03, py + ny * 0.075 + dy * 0.03, along + 0.62, 0.1 * s, 0.052 * s});
		leaves.push_back({px - nx * 0.065 + dx * 0.03, py - ny * 0.065 + dy * 0.03, along - 0.62, 0.08 * s, 0.044 * s});
	}
	const double t = 250 * kPi / 180;
	leaves.push_back({cx + r * std::cos(t), cy + r * std::sin(t), std::atan2(std::cos(t), -std::sin(t)), 0.075, 0.034});
	return leaves;
}

bool InsideLaurel(const std::vector<Leaf> &leaves, double x, double y)
{
	if (x > 0.5) x = 1 - x;
	const double dx = x - 0.5, dy = y - 0.5;
	double deg = std::atan2(dy, dx) * 180 / kPi;
	if (deg < 0) deg += 360;
	if (deg >= 100 && deg <= 238 && std::fabs(std::sqrt(dx * dx + dy * dy) - 0.36) < 0.028) return true;
	for (const Leaf &l : leaves) {
		const double ux = x - l.cx, uy = y - l.cy;
		const double u = ux * std::cos(l.angle) + uy * std::sin(l.angle), v = -ux * std::sin(l.angle) + uy * std::cos(l.angle);
		if (std::fabs(u) <= l.a && std::fabs(v) <= l.b * (1 - (u / l.a) * (u / l.a))) return true;
	}
	return false;
}

const std::vector<uint8_t> &LaurelMask(int size)
{
	static std::map<int, std::vector<uint8_t>> cache;
	auto it = cache.find(size);
	if (it != cache.end()) return it->second;
	static const std::vector<Leaf> leaves = LaurelLeaves();
	std::vector<uint8_t> mask(static_cast<size_t>(size) * size);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			int hits = 0;
			for (int j = 0; j < 4; j++) {
				for (int k = 0; k < 4; k++) hits += InsideLaurel(leaves, (x + (k + 0.5) / 4) / size, (y + (j + 0.5) / 4) / size);
			}
			mask[static_cast<size_t>(y) * size + x] = static_cast<uint8_t>(hits * 255 / 16);
		}
	}
	return cache.emplace(size, std::move(mask)).first->second;
}

// Premultiplied 32-bit bitmaps for AlphaBlend, one per size and colour; there are only a handful.
HBITMAP LaurelBitmap(int size, COLORREF color)
{
	static std::map<std::pair<int, COLORREF>, HBITMAP> cache;
	const auto key = std::make_pair(size, color);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = size;
	bi.bmiHeader.biHeight = -size;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (bmp && bits) {
		const std::vector<uint8_t> &mask = LaurelMask(size);
		uint32_t *px = static_cast<uint32_t *>(bits);
		for (size_t i = 0; i < mask.size(); i++) {
			const uint32_t a = mask[i];
			px[i] = (a << 24) | ((GetRValue(color) * a / 255) << 16) | ((GetGValue(color) * a / 255) << 8) | (GetBValue(color) * a / 255);
		}
	}
	cache[key] = bmp;
	return bmp;
}

COLORREF Lighter(COLORREF c, int percent)
{
	return RGB(GetRValue(c) + (255 - GetRValue(c)) * percent / 100, GetGValue(c) + (255 - GetGValue(c)) * percent / 100,
		GetBValue(c) + (255 - GetBValue(c)) * percent / 100);
}

}

COLORREF PatronColor(PatronTier tier)
{
	switch (tier) {
	case PatronTier::Gold: return RGB(236, 188, 76);
	case PatronTier::Silver: return RGB(204, 210, 218);
	case PatronTier::Bronze: return RGB(218, 142, 86);
	case PatronTier::Top10: return RGB(232, 96, 88);
	case PatronTier::Patron: return RGB(196, 178, 140);
	default: return RGB(255, 255, 255);
	}
}

void DrawPatronLaurel(ui::Painter &p, int x, int y, int size, PatronTier tier)
{
	if (tier == PatronTier::None) return;
	const RECT r = p.R(x, y, size, size);
	const int px = std::max(8, static_cast<int>(r.right - r.left));
	HBITMAP bmp = LaurelBitmap(px, PatronColor(tier));
	if (!bmp) return;
	HDC mem = CreateCompatibleDC(p.dc);
	HGDIOBJ old = SelectObject(mem, bmp);
	BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
	GdiAlphaBlend(p.dc, r.left, r.top, px, px, mem, 0, 0, px, px, blend);
	SelectObject(mem, old);
	DeleteDC(mem);
}

void DrawPatronName(ui::Painter &p, int x, int y, int w, int h, const std::string &name, PatronTier tier, UINT fmt)
{
	const COLORREF base = PatronColor(tier);
	p.Text(x, y, w, h, name, base, fmt);
	if (tier != PatronTier::Gold && tier != PatronTier::Silver && tier != PatronTier::Bronze) return;
	const int textW = std::min(w, p.TextWidth(name));
	const int band = 16, sweepMs = 1400, periodMs = 3600;
	const int t = static_cast<int>(GetTickCount() % periodMs);
	if (t >= sweepMs) return;
	const int at = x - band + (textW + band) * t / sweepMs;
	const RECT clip = p.R(std::max(x, at), y, std::min(at + band, x + textW) - std::max(x, at), h);
	if (clip.right <= clip.left) return;
	// IntersectClipRect inside SaveDC keeps whatever clip the caller already set, like the chat box.
	const int saved = SaveDC(p.dc);
	IntersectClipRect(p.dc, clip.left, clip.top, clip.right, clip.bottom);
	p.Text(x, y, w, h, name, Lighter(base, 70), fmt);
	RestoreDC(p.dc, saved);
}
