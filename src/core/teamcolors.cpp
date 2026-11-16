// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/teamcolors.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Hsv {
	float hue = 0, sat = 0, val = 0;
};

Hsv HsvOf(uint32_t rgb)
{
	const float r = ((rgb >> 16) & 0xff) / 255.0f, g = ((rgb >> 8) & 0xff) / 255.0f, b = (rgb & 0xff) / 255.0f;
	const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), span = hi - lo;
	Hsv out;
	out.val = hi;
	out.sat = hi > 0 ? span / hi : 0;
	if (span <= 0) return out;
	float h = hi == r ? (g - b) / span : hi == g ? 2 + (b - r) / span : 4 + (r - g) / span;
	h *= 60;
	if (h < 0) h += 360;
	out.hue = h;
	return out;
}

// Degrees between two hues, 0 to 180; a grey has no hue and is as far as can be.
float HueGap(const Hsv &c, float hue)
{
	if (c.sat < 0.15f) return 180;
	const float d = std::fabs(c.hue - hue);
	return std::min(d, 360 - d);
}

}

std::array<int, kTeamColorSlots> TeamColorsFor(const uint32_t (&palette)[kTeamColorPalette], const int (&team)[kTeamColorSlots],
	const bool (&used)[kTeamColorSlots])
{
	static const float kAnchor[4] = {0, 225, 120, 55};
	std::array<int, kTeamColorSlots> out;
	out.fill(-1);
	Hsv hsv[kTeamColorPalette];
	for (int c = 0; c < kTeamColorPalette; c++) hsv[c] = HsvOf(palette[c]);
	bool taken[kTeamColorPalette] = {};
	std::vector<int> members[4];
	for (int i = 0; i < kTeamColorSlots; i++) {
		if (used[i] && team[i] >= 1 && team[i] <= 4) members[team[i] - 1].push_back(i);
	}
	// The colour of the round: the nearest free hue, the more vivid first on a tie.
	auto nearest = [&](float anchor) {
		int best = -1;
		float bestGap = 0, bestVivid = 0;
		for (int c = 0; c < kTeamColorPalette; c++) {
			if (taken[c]) continue;
			const float gap = HueGap(hsv[c], anchor), vivid = hsv[c].sat * hsv[c].val;
			if (best < 0 || gap < bestGap - 0.5f || (std::fabs(gap - bestGap) <= 0.5f && vivid > bestVivid + 0.01f)) {
				best = c;
				bestGap = gap;
				bestVivid = vivid;
			}
		}
		return best;
	};
	for (size_t round = 0; round < kTeamColorSlots; round++) {
		for (int t = 0; t < 4; t++) {
			if (round >= members[t].size()) continue;
			const int c = nearest(kAnchor[t]);
			if (c < 0) continue;
			taken[c] = true;
			out[members[t][round]] = c;
		}
	}
	// Without a team: the free colour whose hue is furthest from every colour given.
	for (int i = 0; i < kTeamColorSlots; i++) {
		if (!used[i] || out[i] >= 0) continue;
		int best = -1;
		float bestGap = -1;
		for (int c = 0; c < kTeamColorPalette; c++) {
			if (taken[c]) continue;
			float gap = 180;
			for (int o = 0; o < kTeamColorPalette; o++) {
				if (taken[o]) gap = std::min(gap, hsv[c].sat < 0.15f || hsv[o].sat < 0.15f ? 90.0f : HueGap(hsv[o], hsv[c].hue));
			}
			if (gap > bestGap + 0.5f) {
				best = c;
				bestGap = gap;
			}
		}
		if (best < 0) continue;
		taken[best] = true;
		out[i] = best;
	}
	return out;
}
