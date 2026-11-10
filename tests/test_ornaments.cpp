// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include "check.h"

#include <algorithm>
#include <set>

#include "ui/ornaments.h"
#include "ui/uikit.h"

static void EmptySizes()
{
	for (int i = 0; i < kOrnamentCount; i++) {
		CHECK(OrnamentMask(static_cast<Ornament>(i), 0, 10).empty());
		CHECK(OrnamentMask(static_cast<Ornament>(i), 10, 0).empty());
	}
}

static void SizeAndContent()
{
	const int w = 74, h = 46;
	for (int i = 0; i < kOrnamentCount; i++) {
		const std::vector<uint8_t> mask = OrnamentMask(static_cast<Ornament>(i), w, h);
		CHECK_EQ(mask.size(), static_cast<size_t>(w * h));
		int solid = 0, partial = 0;
		for (const uint8_t v : mask) {
			if (v == 255) solid++;
			else if (v > 0) partial++;
		}
		CHECK(solid > 0);
		CHECK(partial > 0);
		CHECK(solid + partial < w * h);
	}
}

static void StaysLegibleWhenSmall()
{
	const int w = 40, h = 24;
	const std::vector<uint8_t> mask = OrnamentMask(kOrnamentLaurel, w, h);
	int covered = 0;
	for (const uint8_t v : mask) covered += v > 0 ? 1 : 0;
	CHECK(covered > w * h / 10);
}

static void IconsAreLegibleWhenSmall()
{
	const int size = 21;
	for (int i = kOrnamentScreen; i < kOrnamentCount; i++) {
		const std::vector<uint8_t> mask = OrnamentMask(static_cast<Ornament>(i), size, size);
		int covered = 0;
		for (const uint8_t v : mask) covered += v > 64 ? 1 : 0;
		CHECK(covered > size * size / 6);
	}
}

static void RuleFadesOutAtBothEnds()
{
	const int w = 400, h = 14;
	const std::vector<uint8_t> mask = OrnamentMask(kOrnamentRule, w, h);
	const auto strongest = [&](int x) {
		uint8_t best = 0;
		for (int y = 0; y < h; y++) best = (std::max)(best, mask[static_cast<size_t>(y) * w + x]);
		return best;
	};
	CHECK(strongest(0) < 8);
	CHECK(strongest(w - 1) < 8);
	CHECK(strongest(w / 2) == 255);
	CHECK(strongest(w / 4) > strongest(w / 16));
	for (int x = 0; x < w; x++) CHECK_EQ(static_cast<int>(strongest(x)), static_cast<int>(strongest(w - 1 - x)));
}

static bool BelowShallowLine(double x, double y) { return y > 0.3 + 0.12 * x; }

// A shallow edge crosses many pixels a little at a time: each of them has to get its own share, not one of a few steps.
static void ShallowEdgesFadeSmoothly()
{
	const int w = 64, h = 16;
	const std::vector<uint8_t> mask = ShapeMask(w, h, BelowShallowLine);
	CHECK_EQ(mask.size(), static_cast<size_t>(w * h));
	std::set<int> shades;
	int partial = 0;
	for (int x = 0; x < w; x++) {
		int previous = 0;
		for (int y = 0; y < h; y++) {
			const int v = mask[static_cast<size_t>(y) * w + x];
			CHECK(v >= previous);
			previous = v;
			if (v > 0 && v < 255) {
				partial++;
				shades.insert(v);
			}
		}
		CHECK_EQ(static_cast<int>(mask[static_cast<size_t>(h - 1) * w + x]), 255);
		CHECK_EQ(static_cast<int>(mask[x]), 0);
	}
	CHECK(partial >= w);
	CHECK(static_cast<int>(shades.size()) > 30);
	CHECK(ShapeMask(0, 4, BelowShallowLine).empty());
	CHECK(ShapeMask(4, 4, nullptr).empty());
}

static void OrnamentEdgesAreSoft()
{
	for (int i = kOrnamentScreen; i < kOrnamentCount; i++) {
		const std::vector<uint8_t> mask = OrnamentMask(static_cast<Ornament>(i), 40, 40);
		std::set<int> shades;
		for (const uint8_t v : mask) {
			if (v > 0 && v < 255) shades.insert(v);
		}
		CHECK(static_cast<int>(shades.size()) > 20);
	}
}

// Circles and lozenges are laid over the canvas with soft edges, and nothing goes outside the clip.
static void ShapesBlendOntoTheCanvas()
{
	ui::Canvas canvas;
	CHECK(canvas.Ensure(32, 32));
	for (int i = 0; i < 32 * 32; i++) canvas.bits[i] = 0;
	ui::Painter p{canvas.dc, 1.0f, 0, 0, nullptr};
	p.Ellipse(0, 0, 32, 32, RGB(255, 255, 255));
	const auto red = [&](int x, int y) { return static_cast<int>((canvas.bits[y * 32 + x] >> 16) & 255); };
	CHECK_EQ(red(16, 16), 255);
	CHECK_EQ(red(0, 0), 0);
	int soft = 0;
	for (int x = 0; x < 32; x++) soft += red(x, 3) > 0 && red(x, 3) < 255 ? 1 : 0;
	CHECK(soft >= 4);
	for (int i = 0; i < 32 * 32; i++) canvas.bits[i] = 0;
	p.Clip(0, 0, 16, 32);
	p.Diamond(16, 16, 10, RGB(255, 255, 255));
	p.Unclip();
	CHECK_EQ(red(10, 16), 255);
	CHECK_EQ(red(20, 16), 0);
	canvas.Release();
}

int main()
{
	ShallowEdgesFadeSmoothly();
	OrnamentEdgesAreSoft();
	ShapesBlendOntoTheCanvas();
	EmptySizes();
	SizeAndContent();
	StaysLegibleWhenSmall();
	IconsAreLegibleWhenSmall();
	RuleFadesOutAtBothEnds();
	return test::Summary("ornaments");
}
