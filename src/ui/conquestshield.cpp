// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/conquestshield.h"

#include <algorithm>
#include <cmath>

namespace {

// The shield in a unit box: a flat top with rounded corners, straight sides down to kWaist, then curving in to the point.
const double kWaist = 0.42, kCorner = 0.09;

bool InShield(double u, double v)
{
	if (v < 0 || v > 1 || u < 0 || u > 1) return false;
	const double dx = std::fabs(u - 0.5);
	if (v < kCorner && dx > 0.5 - kCorner) {
		const double cx = 0.5 - kCorner - dx, cy = kCorner - v;
		return cx * cx + cy * cy <= kCorner * kCorner;
	}
	if (v <= kWaist) return dx <= 0.5;
	const double t = (v - kWaist) / (1 - kWaist);
	return dx <= 0.5 * (1 - std::pow(t, 2.1));
}

struct Rgb {
	double r, g, b;
};

Rgb Lerp(Rgb a, Rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

}

std::vector<uint32_t> ConquestShieldArt(int w, int h, int style)
{
	std::vector<uint32_t> out(static_cast<size_t>(std::max(0, w)) * std::max(0, h), 0);
	if (w < 4 || h < 4) return out;
	const bool next = style == kShieldNext;
	// The rim, in pixels, and the same inset as a share of the box; the point needs more height to look as thick.
	const double rim = std::max(2.0, w * (next ? 0.075 : 0.06));
	const double iu = rim / w, iv = rim / h;
	const Rgb rimTop = next ? Rgb{232, 92, 88} : Rgb{132, 126, 117}, rimBottom = next ? Rgb{126, 18, 24} : Rgb{58, 55, 51};
	const Rgb fieldTop = next ? Rgb{60, 40, 38} : Rgb{46, 42, 39}, fieldBottom = next ? Rgb{30, 20, 20} : Rgb{24, 22, 21};
	const Rgb edge = {14, 12, 11};
	const int n = 4;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			int outer = 0, inner = 0, lip = 0;
			for (int sy = 0; sy < n; sy++) {
				for (int sx = 0; sx < n; sx++) {
					const double u = (x + (sx + 0.5) / n) / w, v = (y + (sy + 0.5) / n) / h;
					if (!InShield(u, v)) continue;
					outer++;
					// The line just inside the outer edge is darker, so the rim reads against the light backdrop too.
					if (InShield((u - 0.9 / w) / (1 - 1.8 / w), (v - 0.9 / h) / (1 - 2.4 / h))) lip++;
					if (InShield((u - iu) / (1 - 2 * iu), (v - iv) / (1 - 2.8 * iv))) inner++;
				}
			}
			if (!outer) continue;
			const double v = (y + 0.5) / h, u = (x + 0.5) / w;
			const double fi = inner / static_cast<double>(outer), fl = lip / static_cast<double>(outer);
			Rgb rimC = Lerp(rimTop, rimBottom, std::min(1.0, v * 1.1));
			// A soft light from the upper left on the metal.
			const double shine = std::max(0.0, 1 - std::hypot(u - 0.25, v - 0.1) / 0.55) * 0.35;
			rimC = Lerp(rimC, {255, 250, 240}, shine);
			Rgb field = Lerp(fieldTop, fieldBottom, v);
			field = Lerp(field, {255, 245, 230}, std::max(0.0, 0.5 - std::hypot(u - 0.32, v - 0.18)) * 0.12);
			Rgb c = Lerp(rimC, field, fi);
			c = Lerp(edge, c, fl);
			const uint32_t a = static_cast<uint32_t>(std::lround(255.0 * outer / (n * n)));
			const auto ch = [](double value) { return static_cast<uint32_t>(std::max(0.0, std::min(255.0, value + 0.5))); };
			out[static_cast<size_t>(y) * w + x] = a << 24 | ch(c.r) << 16 | ch(c.g) << 8 | ch(c.b);
		}
	}
	return out;
}
