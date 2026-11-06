// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "ui/ornaments.h"

static void EmptySizes()
{
	CHECK(LaurelBranchMask(0, 10).empty());
	CHECK(LaurelBranchMask(10, 0).empty());
}

static void SizeAndContent()
{
	const int w = 74, h = 46;
	const std::vector<uint8_t> mask = LaurelBranchMask(w, h);
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

static void StaysLegibleWhenSmall()
{
	const int w = 40, h = 24;
	const std::vector<uint8_t> mask = LaurelBranchMask(w, h);
	int covered = 0;
	for (const uint8_t v : mask) covered += v > 0 ? 1 : 0;
	CHECK(covered > w * h / 10);
}

int main()
{
	EmptySizes();
	SizeAndContent();
	StaysLegibleWhenSmall();
	return test::Summary("ornaments");
}
