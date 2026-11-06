// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/ornaments.h"

#include <cmath>
#include <cstddef>

namespace {
const double kPi = 3.14159265358979;
const int kLeafPairs = 7;
const int kSamples = 4;

struct Point {
	double x, y;
};

Point Stem(double t)
{
	const double u = 1 - t;
	return {u * u * 0.96 + 2 * u * t * 0.46 + t * t * 0.08, u * u * 0.88 + 2 * u * t * 0.92 + t * t * 0.3};
}

Point StemDirection(double t)
{
	const double u = 1 - t;
	return {2 * u * (0.46 - 0.96) + 2 * t * (0.08 - 0.46), 2 * u * (0.92 - 0.88) + 2 * t * (0.3 - 0.92)};
}

struct Leaf {
	double x, y, angle, length, width;
};

std::vector<Leaf> Leaves(double aspect)
{
	std::vector<Leaf> leaves;
	for (int i = 0; i < kLeafPairs; i++) {
		const double t = 0.06 + i * 0.145, size = 1 - i * 0.05;
		const Point at = Stem(t), along = StemDirection(t);
		const double angle = std::atan2(along.y / aspect, along.x);
		leaves.push_back({at.x, at.y, angle + 0.72, 0.17 * size, 0.042 * size});
		leaves.push_back({at.x, at.y, angle - 0.72, 0.17 * size, 0.042 * size});
	}
	const Point tip = Stem(1), along = StemDirection(1);
	leaves.push_back({tip.x, tip.y, std::atan2(along.y / aspect, along.x), 0.13, 0.034});
	return leaves;
}

bool InsideLeaf(const Leaf &l, double x, double y, double aspect)
{
	const double dx = x - l.x, dy = (y - l.y) / aspect;
	const double u = dx * std::cos(l.angle) + dy * std::sin(l.angle), v = -dx * std::sin(l.angle) + dy * std::cos(l.angle);
	if (u < 0 || u > l.length) return false;
	return std::fabs(v) <= l.width * std::sin(u / l.length * kPi);
}

bool InsideStem(double x, double y, double aspect)
{
	const int steps = 24;
	for (int i = 0; i <= steps; i++) {
		const Point at = Stem(static_cast<double>(i) / steps);
		const double dx = x - at.x, dy = (y - at.y) / aspect;
		if (dx * dx + dy * dy < 0.012 * 0.012) return true;
	}
	return false;
}
}

std::vector<uint8_t> LaurelBranchMask(int width, int height)
{
	std::vector<uint8_t> mask;
	if (width <= 0 || height <= 0) return mask;
	mask.resize(static_cast<size_t>(width) * height);
	const double aspect = static_cast<double>(width) / height;
	const std::vector<Leaf> leaves = Leaves(aspect);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int hits = 0;
			for (int j = 0; j < kSamples; j++) {
				for (int k = 0; k < kSamples; k++) {
					const double px = (x + (k + 0.5) / kSamples) / width, py = (y + (j + 0.5) / kSamples) / height;
					bool inside = InsideStem(px, py, aspect);
					for (size_t i = 0; !inside && i < leaves.size(); i++) inside = InsideLeaf(leaves[i], px, py, aspect);
					hits += inside ? 1 : 0;
				}
			}
			mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(hits * 255 / (kSamples * kSamples));
		}
	}
	return mask;
}
