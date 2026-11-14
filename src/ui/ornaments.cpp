// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/ornaments.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace {
const double kPi = 3.14159265358979;
const int kLeafPairs = 7;
// A first pass of kSamples x kSamples per pixel finds the edges: the pixels it covers in part, and the ones next to a pixel it saw
// otherwise. Those are sampled again at kEdgeSamples points, each in its own column and two to a row, mirrored left to right so that
// symmetric shapes stay so: an edge close to level (the top of the flag) or to upright gets a hundred shades or more, rather than the
// few steps of a square grid.
const int kSamples = 4, kEdgeSamples = 256, kEdgeStride = 37;

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
	std::vector<uint8_t> coarse(mask.size());
	const int full = kSamples * kSamples;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int hits = 0;
			for (int j = 0; j < kSamples; j++) {
				for (int k = 0; k < kSamples; k++) hits += inside((x + (k + 0.5) / kSamples) / width, (y + (j + 0.5) / kSamples) / height) ? 1 : 0;
			}
			coarse[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(hits);
		}
	}
	const auto at = [&](int x, int y) { return coarse[static_cast<size_t>(y) * width + x]; };
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const int c = at(x, y);
			bool edge = c > 0 && c < full;
			if (!edge && x > 0) edge = at(x - 1, y) != c;
			if (!edge && x + 1 < width) edge = at(x + 1, y) != c;
			if (!edge && y > 0) edge = at(x, y - 1) != c;
			if (!edge && y + 1 < height) edge = at(x, y + 1) != c;
			if (!edge) {
				mask[static_cast<size_t>(y) * width + x] = c ? 255 : 0;
				continue;
			}
			int hits = 0;
			const int half = kEdgeSamples / 2;
			for (int i = 0; i < half; i++) {
				const double sx = ((i * kEdgeStride) % half + 0.5) / kEdgeSamples, sy = (i + 0.5) / half;
				hits += inside((x + sx) / width, (y + sy) / height) ? 1 : 0;
				hits += inside((x + 1 - sx) / width, (y + sy) / height) ? 1 : 0;
			}
			mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>((hits * 255 + kEdgeSamples / 2) / kEdgeSamples);
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

bool InPin(double x, double y)
{
	// The sides run from the tip at the bottom along the tangents of the head, so the outline has no corner where they meet.
	static const Point taper[] = {{0.239, 0.507}, {0.761, 0.507}, {0.5, 0.97}};
	const double cx = 0.5, cy = 0.36;
	if (InCircle(x, y, cx, cy, 0.115)) return false;
	return InCircle(x, y, cx, cy, 0.3) || InPolygon(x, y, taper);
}

bool InChevron(double x, double y)
{
	static const Point head[] = {{0.18, 0.08}, {0.88, 0.5}, {0.18, 0.92}, {0.36, 0.5}};
	return InPolygon(x, y, head);
}

bool InFlag(double x, double y)
{
	static const Point cloth[] = {{0.3, 0.08}, {0.6, 0.15}, {0.9, 0.09}, {0.9, 0.5}, {0.6, 0.57}, {0.3, 0.5}};
	return InRoundRect(x, y, 0.16, 0.04, 0.27, 0.96, 0.03) || InCircle(x, y, 0.215, 0.05, 0.07) || InPolygon(x, y, cloth);
}

double SegmentDistance(double x, double y, const Point &a, const Point &b)
{
	const double dx = b.x - a.x, dy = b.y - a.y;
	const double t = std::max(0.0, std::min(1.0, ((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy)));
	return std::hypot(x - a.x - t * dx, y - a.y - t * dy);
}

bool InBack(double x, double y)
{
	static const Point tip = {0.14, 0.5}, tail = {0.88, 0.5}, upper = {0.47, 0.17}, lower = {0.47, 0.83};
	const double pen = 0.06;
	return SegmentDistance(x, y, tip, tail) <= pen || SegmentDistance(x, y, tip, upper) <= pen || SegmentDistance(x, y, tip, lower) <= pen;
}

// The Steam logo as Simple Icons draws it (CC0 1.0; the logo is Valve's trademark), in a square of 24 units.
const char kSteamPath[] =
	"M11.979 0C5.678 0 .511 4.86.022 11.037l6.432 2.658c.545-.371 1.203-.59 1.912-.59.063 0 .125.004.188.006l2.861-4.142V8.91c0-2.495 2.028-4.524 "
	"4.524-4.524 2.494 0 4.524 2.031 4.524 4.527s-2.03 4.525-4.524 4.525h-.105l-4.076 2.911c0 .052.004.105.004.159 0 1.875-1.515 3.396-3.39 "
	"3.396-1.635 0-3.016-1.173-3.331-2.727L.436 15.27C1.862 20.307 6.486 24 11.979 24c6.627 0 11.999-5.373 11.999-12S18.605 0 11.979 0zM7.54 "
	"18.21l-1.473-.61c.262.543.714.999 1.314 1.25 1.297.539 2.793-.076 3.332-1.375.263-.63.264-1.319.005-1.949s-.75-1.121-1.377-1.383c-.624-.26-"
	"1.29-.249-1.878-.03l1.523.63c.956.4 1.409 1.5 1.009 2.455-.397.957-1.497 1.41-2.454 1.012H7.54zm11.415-9.303c0-1.662-1.353-3.015-3.015-"
	"3.015-1.665 0-3.015 1.353-3.015 3.015 0 1.665 1.35 3.015 3.015 3.015 1.663 0 3.015-1.35 3.015-3.015zm-5.273-.005c0-1.252 1.013-2.266 "
	"2.265-2.266 1.249 0 2.266 1.014 2.266 2.266 0 1.251-1.017 2.265-2.266 2.265-1.253 0-2.265-1.014-2.265-2.265z";
const double kSteamUnits = 24;
// Each curve of a path becomes this many straight pieces, and each row of pixels is crossed by this many lines.
const int kCurvePieces = 16, kFillLines = 32;

bool IsSeparator(char c) { return c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n'; }

// A number of an SVG path, which can start right where the last one ended ("4.86.022" is 4.86 then .022). Read by hand: strtod follows
// the locale's decimal point.
bool ReadNumber(const char *&s, double &out)
{
	while (IsSeparator(*s)) s++;
	const char *p = s;
	const bool negative = *p == '-';
	if (*p == '-' || *p == '+') p++;
	double value = 0;
	bool digits = false;
	for (; *p >= '0' && *p <= '9'; p++, digits = true) value = value * 10 + (*p - '0');
	if (*p == '.') {
		p++;
		for (double unit = 0.1; *p >= '0' && *p <= '9'; p++, unit /= 10, digits = true) value += (*p - '0') * unit;
	}
	if (!digits) return false;
	out = negative ? -value : value;
	s = p;
	return true;
}

// The outlines of an SVG path, its curves cut into straight pieces. It knows the commands M, L, H, V, C, S and Z, in either case: what the
// logos drawn here use. An unknown command or a missing number ends it.
std::vector<std::vector<Point>> PathOutlines(const char *s)
{
	std::vector<std::vector<Point>> out;
	Point at = {0, 0}, start = {0, 0}, control = {0, 0};
	char command = 0;
	bool curved = false;
	const auto to = [&](Point p) {
		if (out.empty()) out.push_back({at});
		out.back().push_back(p);
		at = p;
	};
	const auto cubic = [&](Point a, Point b, Point end) {
		const Point from = at;
		for (int i = 1; i <= kCurvePieces; i++) {
			const double t = static_cast<double>(i) / kCurvePieces, u = 1 - t;
			to({u * u * u * from.x + 3 * u * u * t * a.x + 3 * u * t * t * b.x + t * t * t * end.x,
				u * u * u * from.y + 3 * u * u * t * a.y + 3 * u * t * t * b.y + t * t * t * end.y});
		}
		control = b;
	};
	for (;;) {
		while (IsSeparator(*s)) s++;
		if (!*s) break;
		if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) command = *s++;
		const bool relative = command >= 'a';
		const double bx = relative ? at.x : 0, by = relative ? at.y : 0;
		double n[6];
		const auto read = [&](int count) {
			for (int i = 0; i < count; i++) {
				if (!ReadNumber(s, n[i])) return false;
			}
			return true;
		};
		bool curve = false;
		switch (relative ? command - 'a' + 'A' : command) {
		case 'M':
			if (!read(2)) return out;
			at = start = {bx + n[0], by + n[1]};
			out.push_back({at});
			command = relative ? 'l' : 'L';
			break;
		case 'L':
			if (!read(2)) return out;
			to({bx + n[0], by + n[1]});
			break;
		case 'H':
			if (!read(1)) return out;
			to({bx + n[0], at.y});
			break;
		case 'V':
			if (!read(1)) return out;
			to({at.x, by + n[0]});
			break;
		case 'C':
			if (!read(6)) return out;
			cubic({bx + n[0], by + n[1]}, {bx + n[2], by + n[3]}, {bx + n[4], by + n[5]});
			curve = true;
			break;
		case 'S':
			if (!read(4)) return out;
			// The first control point mirrors the last one of the curve before, or is the current point when there was none.
			cubic(curved ? Point{2 * at.x - control.x, 2 * at.y - control.y} : at, {bx + n[0], by + n[1]}, {bx + n[2], by + n[3]});
			curve = true;
			break;
		case 'Z':
			at = start;
			command = 0;
			break;
		default: return out;
		}
		curved = curve;
	}
	return out;
}

void AddSpan(std::vector<double> &row, int width, double from, double to)
{
	from = std::max(from, 0.0);
	to = std::min(to, static_cast<double>(width));
	if (to <= from) return;
	const int first = static_cast<int>(from), last = static_cast<int>(to);
	if (first == last) {
		row[first] += to - from;
		return;
	}
	row[first] += first + 1 - from;
	for (int x = first + 1; x < last; x++) row[x] += 1;
	if (last < width) row[last] += to - last;
}

// How much of each pixel the outlines cover, filled by the nonzero rule as SVG does. Each of kFillLines lines across a row of pixels adds
// the exact length of what it crosses inside, so an edge gets its share in every pixel it passes through.
std::vector<uint8_t> FillOutlines(int width, int height, const std::vector<std::vector<Point>> &outlines, double units)
{
	struct Edge {
		double x0, y0, x1, y1;
		int winding;
	};
	const double sx = width / units, sy = height / units;
	std::vector<Edge> edges;
	for (const std::vector<Point> &o : outlines) {
		for (size_t i = 0; i < o.size(); i++) {
			const Point a = {o[i].x * sx, o[i].y * sy}, b = {o[(i + 1) % o.size()].x * sx, o[(i + 1) % o.size()].y * sy};
			if (a.y < b.y) edges.push_back({a.x, a.y, b.x, b.y, 1});
			else if (b.y < a.y) edges.push_back({b.x, b.y, a.x, a.y, -1});
		}
	}
	std::vector<uint8_t> mask(static_cast<size_t>(width) * height);
	std::vector<double> row(static_cast<size_t>(width) + 1);
	std::vector<std::pair<double, int>> crossings;
	for (int y = 0; y < height; y++) {
		std::fill(row.begin(), row.end(), 0.0);
		for (int k = 0; k < kFillLines; k++) {
			const double ly = y + (k + 0.5) / kFillLines;
			crossings.clear();
			for (const Edge &e : edges) {
				if (ly >= e.y0 && ly < e.y1) crossings.push_back({e.x0 + (ly - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.winding});
			}
			std::sort(crossings.begin(), crossings.end());
			int winding = 0;
			double from = 0;
			for (const std::pair<double, int> &c : crossings) {
				const int before = winding;
				winding += c.second;
				if (before == 0 && winding != 0) from = c.first;
				else if (before != 0 && winding == 0) AddSpan(row, width, from, c.first);
			}
		}
		for (int x = 0; x < width; x++) {
			mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(std::min(255.0, row[x] * 255 / kFillLines + 0.5));
		}
	}
	return mask;
}

std::vector<uint8_t> SteamMask(int width, int height)
{
	static const std::vector<std::vector<Point>> outlines = PathOutlines(kSteamPath);
	return FillOutlines(width, height, outlines, kSteamUnits);
}

bool InMouse(double x, double y)
{
	const double x0 = 0.2, x1 = 0.8, y0 = 0.04, y1 = 0.96, mid = 0.5, split = 0.42, wall = 0.075;
	if (!InRoundRect(x, y, x0, y0, x1, y1, 0.28)) return false;
	// A gap between the buttons and under them.
	if (y < split && std::fabs(x - mid) < 0.03) return false;
	if (y >= split && y < split + 0.05) return x < x0 + wall || x > x1 - wall;
	// The right button is only its rim: the left one is the one pressed.
	if (y < split && x > mid) return !InRoundRect(x, y, mid + 0.03 + wall * 0.6, y0 + wall, x1 - wall, split - 0.01, 0.2);
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

std::vector<uint8_t> ShapeMask(int width, int height, bool (*inside)(double x, double y))
{
	if (width <= 0 || height <= 0 || !inside) return {};
	return Coverage(width, height, inside);
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
	case kOrnamentPin: return Coverage(width, height, InPin);
	case kOrnamentChevron: return Coverage(width, height, InChevron);
	case kOrnamentFlag: return Coverage(width, height, InFlag);
	case kOrnamentMouse: return Coverage(width, height, InMouse);
	case kOrnamentSteam: return SteamMask(width, height);
	case kOrnamentBack: return Coverage(width, height, InBack);
	default: return {};
	}
}
