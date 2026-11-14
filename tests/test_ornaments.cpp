// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include "check.h"

#include <algorithm>
#include <cstdlib>
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

static void PinAndChevronShapes()
{
	const int w = 40, h = 40;
	const std::vector<uint8_t> pin = OrnamentMask(kOrnamentPin, w, h);
	const auto at = [&](const std::vector<uint8_t> &m, double x, double y) { return static_cast<int>(m[static_cast<size_t>(y * h) * w + static_cast<size_t>(x * w)]); };
	CHECK_EQ(at(pin, 0.5, 0.36), 0);
	CHECK_EQ(at(pin, 0.5, 0.15), 255);
	CHECK_EQ(at(pin, 0.5, 0.85), 255);
	CHECK_EQ(at(pin, 0.1, 0.85), 0);
	CHECK_EQ(at(pin, 0.9, 0.1), 0);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w / 2; x++) CHECK(std::abs(pin[static_cast<size_t>(y) * w + x] - pin[static_cast<size_t>(y) * w + w - 1 - x]) <= 1);
	}
	const std::vector<uint8_t> chevron = OrnamentMask(kOrnamentChevron, w, h);
	CHECK_EQ(at(chevron, 0.7, 0.5), 255);
	CHECK_EQ(at(chevron, 0.25, 0.5), 0);
	CHECK_EQ(at(chevron, 0.9, 0.1), 0);
}

// The Steam logo is filled from its outline: a disc with the piston cut out, and a ring left inside the big wheel.
static void SteamLogoShape()
{
	const int size = 96;  // four pixels to each unit of the logo's 24
	const std::vector<uint8_t> m = OrnamentMask(kOrnamentSteam, size, size);
	const auto at = [&](double ux, double uy) { return static_cast<int>(m[static_cast<size_t>(uy * 4) * size + static_cast<size_t>(ux * 4)]); };
	CHECK_EQ(at(0.5, 0.5), 0);  // outside the disc
	CHECK_EQ(at(23.5, 23.5), 0);
	CHECK_EQ(at(6, 6), 255);  // the disc
	CHECK_EQ(at(21.5, 8.9), 255);  // the disc right of the big wheel
	CHECK_EQ(at(15.94, 8.91), 0);  // the middle of the big wheel
	CHECK(at(18.6, 8.9) >= 250);  // its ring
	CHECK_EQ(at(19.7, 8.9), 0);  // the gap around the ring
	CHECK_EQ(at(13, 12.5), 0);  // the rod
}

static void BackArrowShape()
{
	const int size = 40;
	const std::vector<uint8_t> m = OrnamentMask(kOrnamentBack, size, size);
	const auto at = [&](int x, int y) { return static_cast<int>(m[static_cast<size_t>(y) * size + x]); };
	CHECK_EQ(at(12, 20), 255);  // the shaft
	CHECK_EQ(at(33, 20), 255);
	CHECK_EQ(at(6, 20), 255);  // the tip
	CHECK_EQ(at(13, 12), 255);  // the upper stroke of the head
	CHECK_EQ(at(13, 27), 255);  // the lower one
	CHECK_EQ(at(24, 12), 0);  // between the head and the shaft
	CHECK_EQ(at(36, 4), 0);
	CHECK_EQ(at(1, 1), 0);
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
	PinAndChevronShapes();
	SteamLogoShape();
	BackArrowShape();
	return test::Summary("ornaments");
}
