// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/ornaments.h"

#include <algorithm>
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

template <class Inside>
std::vector<uint8_t> Coverage(int width, int height, Inside inside)
{
	std::vector<uint8_t> mask(static_cast<size_t>(width) * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int hits = 0;
			for (int j = 0; j < kSamples; j++) {
				for (int k = 0; k < kSamples; k++) {
					hits += inside((x + (k + 0.5) / kSamples) / width, (y + (j + 0.5) / kSamples) / height) ? 1 : 0;
				}
			}
			mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(hits * 255 / (kSamples * kSamples));
		}
	}
	return mask;
}

bool InRect(double x, double y, double x0, double y0, double x1, double y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }

bool InRoundRect(double x, double y, double x0, double y0, double x1, double y1, double radius)
{
	if (!InRect(x, y, x0, y0, x1, y1)) return false;
	const double dx = std::max({x0 + radius - x, x - (x1 - radius), 0.0}), dy = std::max({y0 + radius - y, y - (y1 - radius), 0.0});
	return dx * dx + dy * dy <= radius * radius;
}

bool InCircle(double x, double y, double cx, double cy, double radius) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius; }

template <size_t N>
bool InPolygon(double x, double y, const Point (&points)[N])
{
	bool inside = false;
	for (size_t i = 0, j = N - 1; i < N; j = i++) {
		const Point &a = points[i], &b = points[j];
		if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
	}
	return inside;
}

bool InScreen(double x, double y)
{
	static const Point stand[] = {{0.43, 0.72}, {0.57, 0.72}, {0.61, 0.86}, {0.39, 0.86}};
	if (InPolygon(x, y, stand) || InRoundRect(x, y, 0.25, 0.85, 0.75, 0.92, 0.03)) return true;
	return InRoundRect(x, y, 0.03, 0.1, 0.97, 0.74, 0.06) && !InRect(x, y, 0.11, 0.18, 0.89, 0.66);
}

bool InSoundWave(double x, double y, double inner, double outer)
{
	const double dx = x - 0.48, dy = y - 0.5, distance = std::sqrt(dx * dx + dy * dy);
	return distance >= inner && distance <= outer && dx > 0 && std::fabs(dy) <= dx * 1.25;
}

bool InSound(double x, double y)
{
	static const Point cone[] = {{0.22, 0.35}, {0.48, 0.13}, {0.48, 0.87}, {0.22, 0.65}};
	return InRoundRect(x, y, 0.04, 0.35, 0.24, 0.65, 0.03) || InPolygon(x, y, cone) || InSoundWave(x, y, 0.13, 0.21) || InSoundWave(x, y, 0.3, 0.38);
}

// along runs from the pommel to the point of a sword, across is the distance to its axis.
bool InSword(double along, double across, double pad)
{
	const double side = std::fabs(across);
	if (along >= -0.2 && along < 0.47) return side <= 0.058 + pad;
	if (along >= 0.47 && along <= 0.64 + pad) return side <= (0.64 + pad - along) * 0.34 + pad;
	if (along >= -0.27 && along < -0.2) return side <= 0.15 + pad;
	if (along >= -0.47 && along < -0.27 && side <= 0.035 + pad) return true;
	return InCircle(along, across, -0.5, 0, 0.065 + pad);
}

bool InSwords(double x, double y)
{
	const double root = std::sqrt(2.0);
	const double cx = x - 0.5, cy = y - 0.5;
	const double frontAlong = (cx - cy) / root, frontAcross = (cx + cy) / root;
	if (InSword(frontAlong, frontAcross, 0)) return true;
	const double backAlong = (-cx - cy) / root, backAcross = (-cx + cy) / root;
	return InSword(backAlong, backAcross, 0) && !InSword(frontAlong, frontAcross, 0.03);
}

bool InGear(double x, double y)
{
	const double dx = x - 0.5, dy = y - 0.5, distance = std::sqrt(dx * dx + dy * dy);
	const double root = 0.35, tip = 0.47;
	if (distance < 0.15 || distance > tip) return false;
	if (distance <= root) return true;
	const int teeth = 8;
	const double turn = (std::atan2(dy, dx) + kPi) / (2 * kPi) * teeth + 0.5;
	const double fromCenter = std::fabs(turn - std::floor(turn) - 0.5);
	return fromCenter <= 0.27 - (distance - root) / (tip - root) * 0.09;
}

bool InKeys(double x, double y)
{
	if (!InRoundRect(x, y, 0.02, 0.2, 0.98, 0.8, 0.07)) return false;
	if (InRect(x, y, 0.26, 0.625, 0.74, 0.71)) return false;
	for (int i = 0; i < 5; i++) {
		const double left = 0.105 + i * 0.165;
		if (InRect(x, y, left, 0.3, left + 0.12, 0.395) || InRect(x, y, left, 0.46, left + 0.12, 0.555)) return false;
	}
	return true;
}

std::vector<uint8_t> LaurelMask(int width, int height)
{
	const double aspect = static_cast<double>(width) / height;
	const std::vector<Leaf> leaves = Leaves(aspect);
	return Coverage(width, height, [&](double x, double y) {
		if (InsideStem(x, y, aspect)) return true;
		return std::any_of(leaves.begin(), leaves.end(), [&](const Leaf &l) { return InsideLeaf(l, x, y, aspect); });
	});
}

std::vector<uint8_t> RuleMask(int width, int height)
{
	const double half = height / 2.0, centerX = width / 2.0, centerY = height / 2 + 0.5;
	const double start = half * 3.6, reach = std::max(centerX - start, 1.0);
	std::vector<uint8_t> mask = Coverage(width, height, [&](double u, double v) {
		const double dx = std::fabs(u * width - centerX), dy = std::fabs(v * height - centerY);
		return dx / (half * 1.7) + dy / half <= 1 || InCircle(dx, dy, half * 2.55, 0, half * 0.24);
	});
	// The line is covered exactly along each column: sampling it would show steps where it barely narrows.
	for (int x = 0; x < width; x++) {
		const double dx = std::fabs(x + 0.5 - centerX);
		if (dx < start) continue;
		const double along = std::min((dx - start) / reach, 1.0);
		const double swell = std::max(half * 0.3 * std::min((dx - start) / (half * 3), 1.0) * std::pow(1 - along, 3.0), 0.5);
		const double fade = std::min((1 - along) * 2.2, 1.0);
		for (int y = 0; y < height; y++) {
			const double covered = std::min(y + 1.0, centerY + swell) - std::max<double>(y, centerY - swell);
			if (covered > 0) mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(255 * covered * fade);
		}
	}
	return mask;
}
}

std::vector<uint8_t> OrnamentMask(Ornament ornament, int width, int height)
{
	if (width <= 0 || height <= 0) return {};
	switch (ornament) {
	case kOrnamentLaurel: return LaurelMask(width, height);
	case kOrnamentRule: return RuleMask(width, height);
	case kOrnamentScreen: return Coverage(width, height, InScreen);
	case kOrnamentSound: return Coverage(width, height, InSound);
	case kOrnamentSwords: return Coverage(width, height, InSwords);
	case kOrnamentGear: return Coverage(width, height, InGear);
	case kOrnamentKeys: return Coverage(width, height, InKeys);
	default: return {};
	}
}
