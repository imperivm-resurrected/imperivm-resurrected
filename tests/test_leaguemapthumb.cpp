// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui/leaguemapthumb.h"

namespace {

const uint32_t kWater = 0x1b2a33, kRed = 0xc84e3c, kBlue = 0x4a80c8;

uint32_t At(const std::vector<uint32_t> &px, int size, double u, double v)
{
	return px[static_cast<size_t>(v * size) * size + static_cast<size_t>(u * size)] & 0xffffff;
}

// The share of the picture that is plain water.
int WaterPercent(const std::vector<uint32_t> &px)
{
	size_t water = 0;
	for (uint32_t c : px) water += (c & 0xffffff) == kWater ? 1 : 0;
	return static_cast<int>(water * 100 / px.size());
}

void Sizes()
{
	CHECK_EQ(static_cast<int>(LeagueMapThumbPixels("continent", 64).size()), 64 * 64);
	CHECK(LeagueMapThumbPixels("continent", 0).empty());
	CHECK(LeagueMapThumbPixels("continent", -5).empty());
	const std::vector<uint32_t> px = LeagueMapThumbPixels("island", 32);
	for (uint32_t c : px) CHECK_EQ(c >> 24, 0xffu);
}

// The middle of the pixels of a colour, as fractions of the picture; false when there are none.
bool Centre(const std::vector<uint32_t> &px, int size, uint32_t color, double &u, double &v)
{
	double su = 0, sv = 0;
	int n = 0;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			if ((px[static_cast<size_t>(y) * size + x] & 0xffffff) != color) continue;
			su += x + 0.5;
			sv += y + 0.5;
			n++;
		}
	}
	if (!n) return false;
	u = su / n / size;
	v = sv / n / size;
	return true;
}

void Starts()
{
	// Both starting places are drawn on every type, the first below on the left and the second above on the right, on land.
	for (const char *type : {"continent", "island", "large_islands", "small_islands", "coastal", "mediterranean", "mountain_passes", "tournament",
			 "randommap", ""}) {
		const std::vector<uint32_t> px = LeagueMapThumbPixels(type, 100);
		double ru = 0, rv = 0, bu = 0, bv = 0;
		CHECK(Centre(px, 100, kRed, ru, rv));
		CHECK(Centre(px, 100, kBlue, bu, bv));
		CHECK(ru < 0.5 && rv > 0.5);
		CHECK(bu > 0.5 && bv < 0.5);
		// Just past the rim, towards the middle of the map.
		CHECK(At(px, 100, ru + 0.06, rv - 0.06) != kWater);
		CHECK(At(px, 100, bu - 0.06, bv + 0.06) != kWater);
	}
}

void Kinds()
{
	const std::vector<uint32_t> island = LeagueMapThumbPixels("island", 100), continent = LeagueMapThumbPixels("continent", 100);
	const std::vector<uint32_t> passes = LeagueMapThumbPixels("mountain_passes", 100), sea = LeagueMapThumbPixels("mediterranean", 100);
	// An island has more sea around it than a continent; the passes have none; the inland sea is in the middle.
	CHECK(WaterPercent(island) > WaterPercent(continent));
	CHECK(WaterPercent(continent) > 10);
	CHECK_EQ(WaterPercent(passes), 0);
	CHECK_EQ(At(sea, 100, 0.5, 0.5), kWater);
	CHECK(At(continent, 100, 0.5, 0.5) != kWater);
	CHECK_EQ(At(island, 100, 0.03, 0.03), kWater);
	// A type the drawing does not know is the random map's.
	CHECK(LeagueMapThumbPixels("someday_new", 40) == LeagueMapThumbPixels("randommap", 40));
}

}

int main()
{
	Sizes();
	Starts();
	Kinds();
	return test::Summary("leaguemapthumb");
}
