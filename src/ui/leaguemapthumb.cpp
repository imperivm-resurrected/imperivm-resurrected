// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguemapthumb.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <mutex>
#include <utility>

#include "ui/uikit.h"

namespace {

// The colours of the mockups' map (RankedUiVictoria): ground, water, woods, hills and ridges, and the two starting places.
const uint32_t kLand = 0x3c3a2c, kWater = 0x1b2a33, kForest = 0x2c3a25, kHill = 0x4a4733, kPeakLit = 0x625c45, kPeakShade = 0x2f2d22, kRim = 0x121110,
			   kRed = 0xc84e3c, kBlue = 0x4a80c8;

struct Blob {
	double u, v, r;
};

// The starting places: the first player below on the left, the second above on the right, nearer the middle on one island or continent.
const double kStartR = 0.052, kRimR = 0.072;

double StartInset(const std::string &type)
{
	return type == "island" || type == "continent" ? 0.29 : 0.2;
}

// Inside a blob whose edge wobbles a little, so coasts look drawn and not stamped.
bool In(const Blob &b, double u, double v, double wobble)
{
	const double du = u - b.u, dv = v - b.v, a = std::atan2(dv, du);
	const double r = b.r * (1 + wobble * std::sin(3 * a + b.u * 7) + wobble * 0.6 * std::sin(5 * a + b.v * 5));
	return du * du + dv * dv < r * r;
}

bool InAny(const Blob *blobs, size_t n, double u, double v, double wobble)
{
	for (size_t i = 0; i < n; i++) {
		if (In(blobs[i], u, v, wobble)) return true;
	}
	return false;
}

// Along the line from the top left to the bottom right, and across it (from the first start to the second).
double Along(double u, double v)
{
	return ((u - 0.5) + (v - 0.5)) / std::sqrt(2.0);
}

double Across(double u, double v)
{
	return ((u - 0.5) - (v - 0.5)) / std::sqrt(2.0);
}

bool IsWater(const std::string &type, double u, double v)
{
	if (type == "continent") return !In({0.5, 0.5, 0.43}, u, v, 0.08);
	if (type == "island") return !In({0.5, 0.5, 0.37}, u, v, 0.1);
	if (type == "large_islands") {
		const Blob islands[] = {{0.29, 0.71, 0.28}, {0.71, 0.29, 0.28}};
		return !InAny(islands, 2, u, v, 0.1);
	}
	if (type == "small_islands") {
		const Blob islands[] = {{0.22, 0.78, 0.15}, {0.78, 0.22, 0.15}, {0.73, 0.73, 0.1}, {0.27, 0.28, 0.1}, {0.5, 0.5, 0.08}};
		return !InAny(islands, 5, u, v, 0.12);
	}
	if (type == "coastal") return u + v < 0.62 + 0.05 * std::sin(11 * u) + 0.03 * std::sin(17 * v);
	if (type == "mediterranean") {
		const double x = Along(u, v) / 0.38, y = Across(u, v) / 0.17;
		return x * x + y * y < 1 + 0.12 * std::sin(9 * Along(u, v) + 1);
	}
	if (type == "mountain_passes") return false;
	if (type == "tournament") return In({0.5, 0.5, 0.1}, u, v, 0.06);
	// Any other type, the plain random map among them: land crossed by two rivers.
	return std::fabs(v - (0.5 + 0.05 * std::sin(7 * u + 0.4))) < 0.027 || std::fabs(u - (0.53 + 0.05 * std::sin(6 * v + 1.1))) < 0.023;
}

uint32_t GroundAt(const std::string &type, double u, double v)
{
	if (IsWater(type, u, v)) return kWater;
	if (type == "mountain_passes") {
		// A ridge between the two starts, with two passes through it.
		// Peaks as seen from above, each lit on one side, along a ridge between the two starts with two passes through it.
		const double along = Along(u, v), across = Across(u, v) + 0.02 * std::sin(14 * along);
		const bool pass = std::fabs(along - 0.2) < 0.07 || std::fabs(along + 0.2) < 0.07;
		if (!pass && std::fabs(along) < 0.64) {
			const double t = along * 13 - std::floor(along * 13) - 0.5;
			if (std::fabs(across) < 0.085 * (1 - 2 * std::fabs(t))) return t < 0 ? kPeakLit : kPeakShade;
			if (std::fabs(across) < 0.11) return kHill;
		}
	}
	const Blob woods[] = {{0.2, 0.22, 0.09}, {0.8, 0.78, 0.09}, {0.37, 0.63, 0.06}, {0.63, 0.37, 0.06}, {0.1, 0.5, 0.05}, {0.9, 0.5, 0.05}};
	if (InAny(woods, 6, u, v, 0.15)) return kForest;
	const Blob hills[] = {{0.55, 0.86, 0.055}, {0.45, 0.14, 0.055}, {0.86, 0.6, 0.04}, {0.14, 0.4, 0.04}};
	if (InAny(hills, 4, u, v, 0.15)) return kHill;
	return kLand;
}

uint32_t ColorAt(const std::string &type, double u, double v)
{
	const double inset = StartInset(type);
	const double starts[2][2] = {{inset, 1 - inset}, {1 - inset, inset}};
	for (int i = 0; i < 2; i++) {
		const double du = u - starts[i][0], dv = v - starts[i][1], d2 = du * du + dv * dv;
		if (d2 < kStartR * kStartR) return i == 0 ? kRed : kBlue;
		if (d2 < kRimR * kRimR) return kRim;
	}
	return GroundAt(type, u, v);
}

std::mutex g_mx;
std::map<std::pair<std::string, int>, std::vector<uint32_t>> g_cache;

}

std::vector<uint32_t> LeagueMapThumbPixels(const std::string &type, int px)
{
	std::vector<uint32_t> out;
	if (px <= 0 || px > 1024) return out;
	out.resize(static_cast<size_t>(px) * px);
	// Three by three samples a pixel: the edges come out smooth at any size.
	const int n = 3;
	for (int y = 0; y < px; y++) {
		for (int x = 0; x < px; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int sy = 0; sy < n; sy++) {
				for (int sx = 0; sx < n; sx++) {
					const uint32_t c = ColorAt(type, (x + (sx + 0.5) / n) / px, (y + (sy + 0.5) / n) / px);
					r += (c >> 16) & 0xff;
					g += (c >> 8) & 0xff;
					b += c & 0xff;
				}
			}
			const unsigned k = n * n;
			out[static_cast<size_t>(y) * px + x] = 0xff000000u | ((r / k) << 16) | ((g / k) << 8) | (b / k);
		}
	}
	return out;
}

void PaintLeagueMapThumb(ui::Painter &p, int x, int y, int size, const std::string &type)
{
	const int px = std::max(8, std::min(512, p.S(size)));
	{
		std::lock_guard<std::mutex> lock(g_mx);
		auto it = g_cache.find({type, px});
		if (it == g_cache.end()) {
			if (g_cache.size() > 24) g_cache.clear();
			it = g_cache.emplace(std::make_pair(type, px), LeagueMapThumbPixels(type, px)).first;
		}
		p.Image(x, y, size, size, it->second.data(), px, px);
	}
	p.Frame(x, y, size, size, ui::pal::lineStrong);
}
