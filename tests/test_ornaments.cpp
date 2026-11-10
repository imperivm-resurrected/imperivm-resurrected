// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <algorithm>

#include "ui/ornaments.h"

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

int main()
{
	EmptySizes();
	SizeAndContent();
	StaysLegibleWhenSmall();
	IconsAreLegibleWhenSmall();
	RuleFadesOutAtBothEnds();
	return test::Summary("ornaments");
}
