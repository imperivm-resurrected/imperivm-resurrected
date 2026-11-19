// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/colorwheel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui/widgets.h"

namespace colorwheel {

namespace {
const double kPi = 3.14159265358979;
// A colour picked as black would leave nothing to see: the wheel keeps a brightness of at least this.
const double kMinValue = 0.35;

// The wheel at full brightness, with `around` in its corners: the painter copies pictures without alpha.
const std::vector<uint32_t> &WheelPicture(int side, COLORREF around)
{
	static std::vector<uint32_t> picture;
	static int madeSide = 0;
	static COLORREF madeAround = 0;
	if (madeSide == side && madeAround == around && !picture.empty()) return picture;
	madeSide = side;
	madeAround = around;
	picture.assign(static_cast<size_t>(side) * side, 0);
	const double c = (side - 1) / 2.0;
	const uint32_t bg = static_cast<uint32_t>(GetRValue(around)) << 16 | static_cast<uint32_t>(GetGValue(around)) << 8 | GetBValue(around);
	for (int py = 0; py < side; py++) {
		for (int px = 0; px < side; px++) {
			const double dx = px - c, dy = py - c, d = std::sqrt(dx * dx + dy * dy) / c;
			uint32_t pixel = bg;
			if (d <= 1.0) {
				double h = std::atan2(dy, dx) / (2 * kPi) + 0.5;
				if (h >= 1) h -= 1;
				pixel = FromHsv(h, d, 1.0);
			}
			picture[static_cast<size_t>(py) * side + px] = 0xff000000u | pixel;
		}
	}
	return picture;
}
}

void ToHsv(uint32_t rgb, double &h, double &s, double &v)
{
	const double r = ((rgb >> 16) & 0xff) / 255.0, g = ((rgb >> 8) & 0xff) / 255.0, b = (rgb & 0xff) / 255.0;
	const double mx = (std::max)({r, g, b}), mn = (std::min)({r, g, b}), d = mx - mn;
	v = mx;
	s = mx > 0 ? d / mx : 0;
	h = 0;
	if (d > 0) {
		if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
		else if (mx == g) h = (b - r) / d + 2;
		else h = (r - g) / d + 4;
		h /= 6;
	}
}

uint32_t FromHsv(double h, double s, double v)
{
	const double f = h * 6 - std::floor(h * 6);
	const double p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	double r = v, g = t, b = p;
	switch (static_cast<int>(std::floor(h * 6)) % 6) {
	case 1: r = q, g = v, b = p; break;
	case 2: r = p, g = v, b = t; break;
	case 3: r = p, g = q, b = v; break;
	case 4: r = t, g = p, b = v; break;
	case 5: r = v, g = p, b = q; break;
	default: break;
	}
	const auto byte = [](double c) { return static_cast<uint32_t>((std::max)(0.0, (std::min)(255.0, c * 255 + 0.5))); };
	return byte(r) << 16 | byte(g) << 8 | byte(b);
}

COLORREF Ref(uint32_t rgb)
{
	return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

void Paint(ui::Painter &p, int x, int y, uint32_t color, COLORREF around, int wheelAction, int stripAction, int arg)
{
	double hh = 0, ss = 0, vv = 0;
	ToHsv(color, hh, ss, vv);
	const int side = p.S(kWheel);
	p.Image(x, y, kWheel, kWheel, WheelPicture(side, around).data(), side, side);
	const double angle = (hh - 0.5) * 2 * kPi, r = ss * (kWheel / 2.0);
	const int mx = x + kWheel / 2 + static_cast<int>(std::cos(angle) * r), my = y + kWheel / 2 + static_cast<int>(std::sin(angle) * r);
	p.Frame(mx - 5, my - 5, 11, 11, ui::pal::bg);
	p.Frame(mx - 4, my - 4, 9, 9, ui::pal::text);
	p.AddHit(x, y, kWheel, kWheel, wheelAction, arg);
	// The brightness, from the colour's own hue at the top to dark at the bottom.
	const int sx = x + kWheel + kGap;
	for (int i = 0; i < kWheel; i += 4) p.Fill(sx, y + i, kStrip, 4, Ref(FromHsv(hh, ss, 1.0 - static_cast<double>(i) / kWheel)));
	p.Frame(sx, y, kStrip, kWheel, ui::pal::edge);
	const int vy = y + static_cast<int>((1.0 - vv) * (kWheel - 1));
	p.Fill(sx - 3, vy - 1, kStrip + 6, 3, ui::pal::text);
	p.AddHit(sx - 4, y, kStrip + 8, kWheel, stripAction, arg);
}

uint32_t Pick(bool strip, const RECT &hit, int mx, int my, uint32_t current)
{
	double h = 0, s = 0, v = 0;
	ToHsv(current, h, s, v);
	const int w = (std::max)(1L, hit.right - hit.left), ht = (std::max)(1L, hit.bottom - hit.top);
	if (strip) {
		v = 1.0 - (std::max)(0.0, (std::min)(1.0, static_cast<double>(my - hit.top) / ht));
		return FromHsv(h, s, (std::max)(v, kMinValue));
	}
	const double c = w / 2.0, dx = mx - hit.left - c, dy = my - hit.top - ht / 2.0;
	s = (std::min)(1.0, std::sqrt(dx * dx + dy * dy) / c);
	h = std::atan2(dy, dx) / (2 * kPi) + 0.5;
	if (h >= 1) h -= 1;
	return FromHsv(h, s, (std::max)(v, kMinValue));
}

}
