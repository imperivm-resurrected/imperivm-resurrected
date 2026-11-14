// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leagueemblem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;
const uint32_t kWhite = 0xFFFFFF;

// ---- The 20-pixel drawings: rectangles over a transparent square, in order, as the mockup's tables. ----

struct Px {
	int x, y, w, h;
	uint32_t rgb;
	float alpha;
};

// Light, body and border of a shield, filled in by each rank's table.
enum Slot : uint32_t { kB = 0x1000000, kC = 0x2000000, kL = 0x3000000 };

struct Grid {
	float r[kLeagueBadgeGrid * kLeagueBadgeGrid] = {}, g[kLeagueBadgeGrid * kLeagueBadgeGrid] = {}, b[kLeagueBadgeGrid * kLeagueBadgeGrid] = {},
		  a[kLeagueBadgeGrid * kLeagueBadgeGrid] = {};
	void Over(int i, uint32_t rgb, float alpha)
	{
		const float sr = ((rgb >> 16) & 255) / 255.0f, sg = ((rgb >> 8) & 255) / 255.0f, sb = (rgb & 255) / 255.0f;
		r[i] = sr * alpha + r[i] * (1 - alpha);
		g[i] = sg * alpha + g[i] * (1 - alpha);
		b[i] = sb * alpha + b[i] * (1 - alpha);
		a[i] = alpha + a[i] * (1 - alpha);
	}
};

uint32_t Resolve(uint32_t rgb, const LeagueColors &c)
{
	switch (rgb & 0xFF000000u) {
	case kB: return c.border;
	case kC: return c.body;
	case kL: return c.light;
	default: return rgb;
	}
}

uint32_t Premultiplied(float r, float g, float b, float a)
{
	auto ch = [](float v) { return static_cast<uint32_t>(std::lround(std::max(0.0f, std::min(1.0f, v)) * 255)); };
	return (ch(a) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

LeagueImage FromGrid(const Grid &grid, int scale)
{
	scale = std::max(1, scale);
	LeagueImage img;
	img.w = img.h = kLeagueBadgeGrid * scale;
	img.px.resize(static_cast<size_t>(img.w) * img.h);
	for (int y = 0; y < img.h; y++) {
		for (int x = 0; x < img.w; x++) {
			const int i = (y / scale) * kLeagueBadgeGrid + x / scale;
			img.px[static_cast<size_t>(y) * img.w + x] = Premultiplied(grid.r[i], grid.g[i], grid.b[i], grid.a[i]);
		}
	}
	return img;
}

LeagueImage Paint(const std::vector<Px> &table, const LeagueColors &c, int scale)
{
	Grid grid;
	for (const Px &p : table) {
		const uint32_t rgb = Resolve(p.rgb, c);
		for (int y = std::max(0, p.y); y < std::min(kLeagueBadgeGrid, p.y + p.h); y++) {
			for (int x = std::max(0, p.x); x < std::min(kLeagueBadgeGrid, p.x + p.w); x++) grid.Over(y * kLeagueBadgeGrid + x, rgb, p.alpha);
		}
	}
	return FromGrid(grid, scale);
}

// The crest of a centurion's helmet, over the shields from Centurion up.
const std::vector<Px> kCrest = {{6, 0, 8, 1, 0xE3474C, 1}, {4, 1, 12, 1, 0xC4161C, 1}, {3, 2, 14, 2, 0xC4161C, 1}, {6, 2, 1, 2, 0x8A0E13, 1},
	{9, 2, 2, 2, 0x8A0E13, 1}, {13, 2, 1, 2, 0x8A0E13, 1}};

std::vector<Px> With(std::vector<Px> a, std::initializer_list<Px> b)
{
	a.insert(a.end(), b);
	return a;
}

std::vector<Px> TableOf(LeagueTier tier)
{
	switch (tier) {
	case LeagueTier::Recruit:
		return {{5, 2, 10, 1, kB, 1}, {4, 3, 12, 15, kB, 1}, {5, 18, 10, 1, kB, 1}, {5, 3, 10, 15, kC, 1}, {5, 3, 5, 15, kWhite, 0.12f},
			{8, 8, 4, 4, kL, 1}, {9, 9, 2, 2, kC, 1}, {6, 4, 1, 1, kL, 1}, {13, 4, 1, 1, kL, 1}, {6, 16, 1, 1, kL, 1}, {13, 16, 1, 1, kL, 1}};
	case LeagueTier::Legionary:
		return {{1, 0, 1, 1, 0xEEF2F5, 1}, {2, 1, 1, 1, kL, 1}, {3, 2, 1, 1, kL, 1}, {4, 3, 1, 1, kL, 1}, {4, 17, 1, 1, kL, 1}, {3, 18, 1, 1, kL, 1},
			{2, 19, 1, 1, kL, 1}, {18, 0, 1, 1, 0xEEF2F5, 1}, {17, 1, 1, 1, kL, 1}, {16, 2, 1, 1, kL, 1}, {15, 3, 1, 1, kL, 1}, {15, 17, 1, 1, kL, 1},
			{16, 18, 1, 1, kL, 1}, {17, 19, 1, 1, kL, 1}, {6, 3, 8, 1, kB, 1}, {5, 4, 10, 13, kB, 1}, {6, 17, 8, 1, kB, 1}, {6, 4, 8, 13, kC, 1},
			{6, 4, 4, 13, kWhite, 0.12f}, {8, 8, 4, 4, kL, 1}, {9, 9, 2, 2, kC, 1}};
	// Not in the mockup, which had no Signifer: the shield under a signum, the standard he carried.
	case LeagueTier::Signifer:
		return {{9, 0, 2, 4, kL, 1}, {6, 1, 8, 1, kL, 1}, {6, 2, 1, 2, kL, 1}, {13, 2, 1, 2, kL, 1}, {9, 0, 2, 1, 0xCFEDE5, 1}, {6, 4, 8, 1, kB, 1},
			{5, 5, 10, 13, kB, 1}, {6, 18, 8, 1, kB, 1}, {6, 5, 8, 13, kC, 1}, {6, 5, 4, 13, kWhite, 0.12f}, {8, 9, 4, 4, kL, 1}, {9, 10, 2, 2, kC, 1}};
	case LeagueTier::Centurion:
		return With(kCrest, {{6, 4, 8, 1, kB, 1}, {5, 5, 10, 13, kB, 1}, {6, 18, 8, 1, kB, 1}, {6, 5, 8, 13, kC, 1}, {6, 5, 4, 13, kWhite, 0.12f},
								{8, 9, 4, 4, kL, 1}, {9, 10, 2, 2, kC, 1}});
	case LeagueTier::Tribune:
		return With(kCrest, {{2, 6, 4, 1, kL, 1}, {1, 7, 5, 1, kC, 1}, {2, 8, 4, 1, kC, 1}, {3, 9, 3, 1, kC, 1}, {4, 10, 2, 1, kC, 1},
								{14, 6, 4, 1, kL, 1}, {14, 7, 5, 1, kC, 1}, {14, 8, 4, 1, kC, 1}, {14, 9, 3, 1, kC, 1}, {14, 10, 2, 1, kC, 1},
								{7, 4, 6, 1, kB, 1}, {6, 5, 8, 13, kB, 1}, {7, 18, 6, 1, kB, 1}, {7, 5, 6, 13, kC, 1}, {7, 5, 3, 13, kWhite, 0.12f},
								{8, 9, 4, 4, kL, 1}, {9, 10, 2, 2, kC, 1}});
	case LeagueTier::Legate:
		return With(kCrest, {{2, 5, 4, 1, kL, 1}, {0, 6, 6, 1, kC, 1}, {1, 7, 5, 1, kC, 1}, {0, 8, 6, 1, kC, 1}, {2, 9, 4, 1, kC, 1},
								{1, 10, 5, 1, kC, 1}, {3, 11, 3, 1, kC, 1}, {4, 12, 2, 1, kC, 1}, {0, 6, 1, 1, kL, 1}, {0, 8, 1, 1, kL, 1},
								{1, 10, 1, 1, kL, 1}, {14, 5, 4, 1, kL, 1}, {14, 6, 6, 1, kC, 1}, {14, 7, 5, 1, kC, 1}, {14, 8, 6, 1, kC, 1},
								{14, 9, 4, 1, kC, 1}, {14, 10, 5, 1, kC, 1}, {14, 11, 3, 1, kC, 1}, {14, 12, 2, 1, kC, 1}, {19, 6, 1, 1, kL, 1},
								{19, 8, 1, 1, kL, 1}, {18, 10, 1, 1, kL, 1}, {7, 4, 6, 1, kB, 1}, {6, 5, 8, 13, kB, 1}, {7, 18, 6, 1, kB, 1},
								{7, 5, 6, 13, kC, 1}, {7, 5, 3, 13, kWhite, 0.12f}, {8, 9, 4, 4, kL, 1}, {9, 10, 2, 2, kC, 1}});
	case LeagueTier::Praetorian:
		return With(kCrest, {{2, 4, 3, 1, kL, 1}, {2, 5, 3, 14, kC, 1}, {3, 5, 1, 14, kB, 0.6f}, {2, 8, 3, 1, kL, 1}, {2, 12, 3, 1, kL, 1},
								{2, 16, 3, 1, kL, 1}, {0, 5, 2, 4, kL, 1}, {15, 4, 3, 1, kL, 1}, {15, 5, 3, 14, kC, 1}, {16, 5, 1, 14, kB, 0.6f},
								{15, 8, 3, 1, kL, 1}, {15, 12, 3, 1, kL, 1}, {15, 16, 3, 1, kL, 1}, {18, 5, 2, 4, kL, 1}, {7, 4, 6, 1, kB, 1},
								{6, 5, 8, 13, kB, 1}, {7, 18, 6, 1, kB, 1}, {7, 5, 6, 13, kC, 1}, {7, 5, 3, 13, kWhite, 0.12f}, {8, 9, 4, 4, kL, 1},
								{9, 10, 2, 2, kC, 1}});
	case LeagueTier::Imperator:
		return {{1, 1, 2, 1, kL, 1}, {2, 2, 3, 1, kL, 1}, {3, 3, 5, 1, kL, 1}, {17, 1, 2, 1, kL, 1}, {15, 2, 3, 1, kL, 1}, {12, 3, 5, 1, kL, 1},
			{8, 2, 4, 3, kL, 1}, {9, 0, 2, 2, kL, 1}, {11, 1, 1, 1, 0xE6BE5A, 1}, {5, 5, 10, 1, 0xB08A2E, 1}, {7, 6, 6, 1, kL, 1}, {6, 7, 8, 11, kL, 1},
			{7, 18, 6, 1, kL, 1}, {7, 7, 6, 11, kC, 1}, {7, 7, 3, 11, kWhite, 0.12f}, {8, 10, 4, 4, kL, 1}, {9, 11, 2, 2, kC, 1}};
	default:
		return {{6, 3, 8, 1, kB, 1}, {5, 4, 10, 13, kB, 1}, {6, 17, 8, 1, kB, 1}, {6, 4, 8, 13, kC, 1}, {9, 9, 2, 2, kB, 1}};
	}
}

// ---- The full emblem: shapes in a 160-unit square, filled with coverage counted on a 4x4 grid in each pixel. ----

struct Pt {
	double x, y;
};
typedef std::vector<Pt> Ring;

// A shape is the union of its parts; the rings of one part go by the even-odd rule, so an outline is its outer ring and its inner one.
struct Shape {
	std::vector<std::vector<Ring>> parts;
	uint32_t rgb = 0;
	double alpha = 1;
};

Ring Arc(double cx, double cy, double rx, double ry, double from, double to, int steps)
{
	Ring r;
	for (int i = 0; i <= steps; i++) {
		const double t = from + (to - from) * i / steps;
		r.push_back({cx + rx * std::cos(t), cy + ry * std::sin(t)});
	}
	return r;
}

Ring Ellipse(double cx, double cy, double rx, double ry)
{
	Ring r = Arc(cx, cy, rx, ry, 0, 2 * kPi, 48);
	r.pop_back();
	return r;
}

Ring RoundRect(double x, double y, double w, double h, double radius)
{
	radius = std::max(0.0, std::min(radius, std::min(w, h) / 2));
	Ring r;
	auto corner = [&](double cx, double cy, double from) {
		for (int i = 0; i <= 6; i++) {
			const double t = from + kPi / 2 * i / 6;
			r.push_back({cx + radius * std::cos(t), cy + radius * std::sin(t)});
		}
	};
	corner(x + w - radius, y + radius, -kPi / 2);
	corner(x + w - radius, y + h - radius, 0);
	corner(x + radius, y + h - radius, kPi / 2);
	corner(x + radius, y + radius, kPi);
	return r;
}

// A quadratic Bézier from `a` to `b`, without its first point.
void Quad(Ring &r, Pt a, Pt c, Pt b)
{
	for (int i = 1; i <= 8; i++) {
		const double t = i / 8.0, u = 1 - t;
		r.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y});
	}
}

Ring Mirrored(const Ring &r)
{
	Ring out;
	for (const Pt &p : r) out.push_back({160 - p.x, p.y});
	return out;
}

// SVG's rotate(degrees) about (cx, cy).
Ring Rotated(const Ring &r, double degrees, double cx, double cy)
{
	const double a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
	Ring out;
	for (const Pt &p : r) out.push_back({cx + (p.x - cx) * c - (p.y - cy) * s, cy + (p.x - cx) * s + (p.y - cy) * c});
	return out;
}

struct Scene {
	std::vector<Shape> shapes;

	void Fill(const Ring &r, uint32_t rgb, double alpha = 1) { shapes.push_back({{{r}}, rgb, alpha}); }
	// A line `width` wide through the points, with round joins.
	void Stroke(const Ring &pts, double width, uint32_t rgb, double alpha = 1, bool closed = false)
	{
		Shape s;
		s.rgb = rgb;
		s.alpha = alpha;
		const size_t n = pts.size();
		for (size_t i = 0; i + (closed ? 0 : 1) < n; i++) {
			const Pt a = pts[i], b = pts[(i + 1) % n];
			const double dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
			if (len <= 0) continue;
			const double nx = -dy / len * width / 2, ny = dx / len * width / 2;
			s.parts.push_back({{{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}});
		}
		if (n > 2 || closed) {
			for (size_t i = closed ? 0 : 1; i < (closed ? n : n - 1); i++) s.parts.push_back({Ellipse(pts[i].x, pts[i].y, width / 2, width / 2)});
		}
		shapes.push_back(s);
	}
	void Line(double x1, double y1, double x2, double y2, double width, uint32_t rgb, double alpha = 1) { Stroke({{x1, y1}, {x2, y2}}, width, rgb, alpha); }
	void FillStroked(const Ring &r, uint32_t fill, uint32_t edge, double width)
	{
		Fill(r, fill);
		Stroke(r, width, edge, 1, true);
	}
	void RectOutline(double x, double y, double w, double h, double radius, double width, uint32_t rgb, double alpha)
	{
		const double d = width / 2;
		shapes.push_back({{{RoundRect(x - d, y - d, w + width, h + width, radius + d), RoundRect(x + d, y + d, w - width, h - width, radius - d)}}, rgb, alpha});
	}
};

void Shield(Scene &s, const LeagueColors &c, bool bolts, bool rivets)
{
	s.Fill(RoundRect(48, 50, 64, 92, 13), c.border);
	s.Fill(RoundRect(53, 55, 54, 82, 9), c.body);
	Ring shine = {{62, 55}, {80, 55}, {80, 137}, {62, 137}};
	Quad(shine, {62, 137}, {53, 137}, {53, 128});
	shine.push_back({53, 64});
	Quad(shine, {53, 64}, {53, 55}, {62, 55});
	s.Fill(shine, kWhite, 0.09);
	s.RectOutline(58.5, 60.5, 43, 71, 5, 1.5, c.light, 0.55);
	s.Fill(RoundRect(77, 55, 6, 82, 0), c.light, 0.22);
	if (rivets) {
		for (const Pt p : {Pt{62, 66}, Pt{98, 66}, Pt{62, 126}, Pt{98, 126}}) s.Fill(Ellipse(p.x, p.y, 2.2, 2.2), c.light);
	}
	if (bolts) {
		const Ring bolt = {{0, 0}, {5, -11}, {1, -11}, {5, -24}, {-4, -8}, {0, -8}, {-4, 0}};
		for (const double angle : {-34.0, 34.0, 146.0, -146.0}) {
			Ring placed;
			for (const Pt &p : bolt) placed.push_back({80 + p.x, 96 + p.y - 14});
			s.Fill(Rotated(placed, angle, 80, 96), c.light, 0.9);
		}
	}
	s.Fill(Ellipse(80, 96, 13.5, 13.5), c.border);
	s.Fill(Ellipse(80, 96, 11.5, 11.5), c.light);
	s.Fill(Ellipse(80, 96, 6, 6), c.body);
	s.Fill(Ellipse(76.5, 92.5, 2.4, 2.4), kWhite, 0.6);
}

void Pila(Scene &s, const LeagueColors &c)
{
	for (const bool mirror : {false, true}) {
		auto at = [mirror](double x) { return mirror ? 160 - x : x; };
		s.Line(at(30), 156, at(122), 34, 7, c.border);
		s.Line(at(30), 156, at(122), 34, 3.5, c.light);
		const Ring tip = {{at(128.5), 25.5}, {at(125.6), 37}, {at(118.4), 31.6}};
		s.FillStroked(tip, c.light, c.border, 1.5);
	}
}

void Crest(Scene &s, const LeagueColors &c)
{
	s.Fill(Arc(80, 54, 54, 32, kPi, 2 * kPi, 40), 0xC4161C);
	const double lines[9][4] = {{64.59, 51.03, 30.7, 44.51}, {66.89, 48.36, 38.06, 35.94}, {70.48, 46.23, 49.53, 29.15}, {74.99, 44.87, 63.98, 24.78},
		{80, 44.4, 80, 23.28}, {85.01, 44.87, 96.02, 24.78}, {89.52, 46.23, 110.47, 29.15}, {93.11, 48.36, 121.94, 35.94}, {95.41, 51.03, 129.3, 44.51}};
	for (const auto &l : lines) s.Line(l[0], l[1], l[2], l[3], 2, 0x8A0E13);
	s.Stroke(Arc(80, 54, 54, 32, kPi, 2 * kPi, 40), 2, 0xE3474C);
	s.FillStroked(RoundRect(68, 45, 24, 10, 2), c.light, c.border, 1.5);
}

void Wings(Scene &s, const LeagueColors &c, const Ring &wing, const std::vector<std::vector<double>> &feathers, const Ring &edge)
{
	for (const bool mirror : {false, true}) {
		auto m = [mirror](const Ring &r) { return mirror ? Mirrored(r) : r; };
		s.Fill(m(wing), c.body);
		s.Stroke(m(wing), 2, c.border, 1, true);
		for (const auto &f : feathers) s.Stroke(m({{f[0], f[1]}, {f[2], f[3]}}), 1.5, c.light);
		s.Stroke(m(edge), 2, c.light);
	}
}

void Fasces(Scene &s, const LeagueColors &c)
{
	for (const bool mirror : {false, true}) {
		auto m = [mirror](const Ring &r) { return mirror ? Mirrored(r) : r; };
		s.FillStroked(m(RoundRect(23, 38, 14, 106, 4)), c.body, c.border, 2);
		for (const double x : {27.5, 30.0, 32.5}) s.Stroke(m({{x, 42}, {x, 140}}), 1, c.border, 0.7);
		for (const double y : {62.0, 86.0, 110.0, 132.0}) s.Fill(m(RoundRect(21, y, 18, 5, 0)), c.light);
		Ring axe = {{24, 46}, {10, 37}};
		Quad(axe, {10, 37}, {1, 52}, {10, 67});
		axe.push_back({24, 58});
		s.FillStroked(m(axe), c.light, c.border, 2);
		s.FillStroked(m(Ellipse(30, 38, 5, 5)), c.light, c.border, 1.5);
	}
}

void Signum(Scene &s, const LeagueColors &c)
{
	s.Line(80, 10, 80, 52, 7, c.border);
	s.Line(80, 10, 80, 52, 3.5, c.light);
	s.Line(56, 22, 104, 22, 6, c.border);
	s.Line(56, 22, 104, 22, 3, c.light);
	for (const double x : {58.0, 102.0}) {
		s.Line(x, 22, x, 42, 5, c.border);
		s.Line(x, 22, x, 42, 2.5, c.light);
	}
	s.FillStroked({{80, 2}, {73.5, 14}, {86.5, 14}}, c.light, c.border, 1.5);
	s.FillStroked(Ellipse(80, 35, 7.5, 7.5), c.light, c.border, 1.5);
	s.Fill(Ellipse(80, 35, 3.5, 3.5), c.body);
}

void Eagle(Scene &s)
{
	const uint32_t gold = 0xF2D06B, dark = 0x5A420E;
	const Ring wing = {{78, 46}, {58, 43}, {38, 28}, {22, 6}, {29, 25}, {14, 21}, {27, 35}, {12, 38}, {29, 47}, {50, 54}, {78, 57}};
	for (const bool mirror : {false, true}) {
		auto m = [mirror](const Ring &r) { return mirror ? Mirrored(r) : r; };
		s.FillStroked(m(wing), gold, dark, 2);
		s.Stroke(m({{72, 50}, {30, 40}}), 1.2, dark);
		s.Stroke(m({{72, 47}, {36, 29}}), 1.2, dark);
	}
	s.FillStroked(Ellipse(80, 44, 9, 13), gold, dark, 2);
	s.FillStroked(Ellipse(80, 26, 7, 7), gold, dark, 2);
	s.FillStroked({{86, 24}, {94, 28}, {86, 31}}, 0xE6BE5A, dark, 1.2);
	s.Fill(Ellipse(82, 24, 1.6, 1.6), dark);
	s.FillStroked(RoundRect(58, 53, 44, 6, 2), gold, dark, 1.5);
}

Scene SceneOf(LeagueTier tier)
{
	Scene s;
	const LeagueColors c = LeagueTierColors(tier);
	switch (tier) {
	case LeagueTier::Recruit: Shield(s, c, false, true); break;
	case LeagueTier::Legionary:
		Pila(s, c);
		Shield(s, c, true, false);
		break;
	case LeagueTier::Signifer:
		Signum(s, c);
		Shield(s, c, true, false);
		break;
	case LeagueTier::Centurion:
		Pila(s, c);
		Crest(s, c);
		Shield(s, c, true, false);
		break;
	case LeagueTier::Tribune:
		Crest(s, c);
		Wings(s, c, {{50, 72}, {36, 63}, {20, 58}, {27, 69}, {18, 74}, {29, 81}, {21, 87}, {32, 91}, {27, 99}, {50, 102}},
			{{48, 78, 30, 69}, {48, 86, 29, 81}, {48, 94, 31, 92}}, {{50, 72}, {36, 63}, {20, 58}});
		Shield(s, c, true, false);
		break;
	case LeagueTier::Legate:
		Crest(s, c);
		Wings(s, c, {{50, 64}, {30, 50}, {6, 42}, {17, 57}, {3, 61}, {17, 71}, {4, 79}, {21, 86}, {11, 96}, {29, 98}, {23, 109}, {50, 110}},
			{{48, 74, 20, 62}, {48, 84, 17, 77}, {48, 94, 23, 91}, {48, 103, 31, 104}}, {{50, 64}, {30, 50}, {6, 42}});
		Shield(s, c, true, false);
		break;
	case LeagueTier::Praetorian:
		Crest(s, c);
		Fasces(s, c);
		Shield(s, c, true, false);
		break;
	case LeagueTier::Imperator: {
		const Ring ray = {{76, 96}, {84, 96}, {80, 26}};
		for (int k = 0; k < 16; k++) s.Fill(Rotated(ray, k * 22.5, 80, 96), 0xF2D06B, k % 2 ? 0.16 : 0.3);
		Shield(s, c, true, false);
		Eagle(s);
		break;
	}
	default:
		s.Fill(RoundRect(49.5, 51.5, 61, 89, 12), c.body);
		s.RectOutline(49.5, 51.5, 61, 89, 12, 3, c.border, 1);
		break;
	}
	return s;
}

LeagueImage Rasterize(const Scene &scene, int size)
{
	const int kSub = 4;
	const double scale = size / 160.0;
	std::vector<float> r(static_cast<size_t>(size) * size), g(r.size()), b(r.size()), a(r.size());
	std::vector<unsigned char> covered;
	std::vector<int> count;
	std::vector<double> crossings;
	for (const Shape &shape : scene.shapes) {
		double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
		for (const auto &part : shape.parts) {
			for (const Ring &ring : part) {
				for (const Pt &p : ring) {
					minX = std::min(minX, p.x * scale);
					maxX = std::max(maxX, p.x * scale);
					minY = std::min(minY, p.y * scale);
					maxY = std::max(maxY, p.y * scale);
				}
			}
		}
		const int x0 = std::max(0, static_cast<int>(std::floor(minX))), x1 = std::min(size, static_cast<int>(std::ceil(maxX)) + 1);
		const int y0 = std::max(0, static_cast<int>(std::floor(minY))), y1 = std::min(size, static_cast<int>(std::ceil(maxY)) + 1);
		if (x0 >= x1 || y0 >= y1) continue;
		const int cols = (x1 - x0) * kSub;
		count.assign(static_cast<size_t>(x1 - x0), 0);
		const float sr = ((shape.rgb >> 16) & 255) / 255.0f, sg = ((shape.rgb >> 8) & 255) / 255.0f, sb = (shape.rgb & 255) / 255.0f;
		for (int py = y0; py < y1; py++) {
			std::fill(count.begin(), count.end(), 0);
			for (int sub = 0; sub < kSub; sub++) {
				const double y = (py + (sub + 0.5) / kSub) / scale;
				covered.assign(static_cast<size_t>(cols), 0);
				for (const auto &part : shape.parts) {
					crossings.clear();
					for (const Ring &ring : part) {
						for (size_t i = 0, n = ring.size(); i < n; i++) {
							const Pt p = ring[i], q = ring[(i + 1) % n];
							if ((p.y <= y) == (q.y <= y)) continue;
							crossings.push_back(p.x + (y - p.y) * (q.x - p.x) / (q.y - p.y));
						}
					}
					std::sort(crossings.begin(), crossings.end());
					for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
						// Samples sit at the centres of the sub-columns.
						const double from = crossings[i] * scale * kSub - x0 * kSub - 0.5, to = crossings[i + 1] * scale * kSub - x0 * kSub - 0.5;
						const int c0 = std::max(0, static_cast<int>(std::ceil(from))), c1 = std::min(cols - 1, static_cast<int>(std::ceil(to)) - 1);
						for (int c = c0; c <= c1; c++) covered[static_cast<size_t>(c)] = 1;
					}
				}
				for (int c = 0; c < cols; c++) count[static_cast<size_t>(c / kSub)] += covered[static_cast<size_t>(c)];
			}
			for (int px = x0; px < x1; px++) {
				const int hits = count[static_cast<size_t>(px - x0)];
				if (!hits) continue;
				const float alpha = static_cast<float>(shape.alpha) * hits / (kSub * kSub);
				const size_t i = static_cast<size_t>(py) * size + px;
				r[i] = sr * alpha + r[i] * (1 - alpha);
				g[i] = sg * alpha + g[i] * (1 - alpha);
				b[i] = sb * alpha + b[i] * (1 - alpha);
				a[i] = alpha + a[i] * (1 - alpha);
			}
		}
	}
	LeagueImage img;
	img.w = img.h = size;
	img.px.resize(r.size());
	for (size_t i = 0; i < r.size(); i++) img.px[i] = Premultiplied(r[i], g[i], b[i], a[i]);
	return img;
}

// ---- The phalerae of the tournaments (TorneoCampeon, TorneoCuadro): the mockups' colours of each metal. ----

struct PhaleraShades {
	uint32_t deep, rim, body, edge, light, inner, bright;
};

PhaleraShades ShadesOf(LeagueMetal metal)
{
	switch (metal) {
	case LeagueMetal::Silver: return {0x1A1C20, 0x6F757B, 0x22252A, 0x9AA0A6, 0xC9CED4, 0x2E3238, 0xEEF2F5};
	case LeagueMetal::Bronze: return {0x1F1610, 0x7A4A26, 0x2A1E16, 0xB0703C, 0xCF8A55, 0x3A2618, 0xEDB58A};
	// The champion of a season's final: gold over the brand's red.
	case LeagueMetal::Season: return {0x220607, 0x8A0E13, 0x3E0507, 0xC99A2E, 0xF2D06B, 0x5A0A0E, 0xF3D98A};
	default: return {0x1F1912, 0x8A6A22, 0x2A2116, 0xC99A2E, 0xE0B54A, 0x3A2C14, 0xF3D98A};
	}
}

Ring Circle(double cx, double cy, double r)
{
	Ring out = Arc(cx, cy, r, r, 0, 2 * kPi, 96);
	out.pop_back();
	return out;
}

// A ring `width` wide centred on the circle of radius `r`.
void Band(Scene &s, double cx, double cy, double r, double width, uint32_t rgb)
{
	s.shapes.push_back({{{Circle(cx, cy, r + width / 2), Circle(cx, cy, r - width / 2)}}, rgb, 1});
}

// The eight-pointed star of the phalerae, a point upwards.
Ring Star(double cx, double cy, double outer, double inner)
{
	Ring out;
	for (int k = 0; k < 16; k++) {
		const double a = -kPi / 2 + k * kPi / 8, r = k % 2 ? inner : outer;
		out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
	}
	return out;
}

}

LeagueColors LeagueTierColors(LeagueTier t)
{
	switch (t) {
	case LeagueTier::Recruit: return {0x3A2F27, 0x7A6553, 0xBBA28B, 0xBBA28B};
	case LeagueTier::Legionary: return {0x262C31, 0x66727B, 0xB3BEC6, 0xB3BEC6};
	case LeagueTier::Signifer: return {0x1F3B36, 0x3F7F72, 0x8FCBBB, 0x8FCBBB};
	case LeagueTier::Centurion: return {0x4A2E12, 0xA8692C, 0xE3A35E, 0xE3A35E};
	case LeagueTier::Tribune: return {0x3E444A, 0xA3ACB5, 0xEEF2F5, 0xDCE2E7};
	case LeagueTier::Legate: return {0x4F3A0C, 0xC4962A, 0xF2D06B, 0xF2D06B};
	case LeagueTier::Praetorian: return {0x3A1027, 0x8E2C63, 0xE08AB6, 0xE08AB6};
	case LeagueTier::Imperator: return {0x3E0507, 0xC4161C, 0xF2D06B, 0xE3474C};
	default: return {0x6F6B66, 0x1C1917, 0x9E9B94, 0x9E9B94};
	}
}

LeagueImage LeagueBadgePixels(LeagueTier tier, int scale)
{
	LeagueColors c = LeagueTierColors(tier);
	if (tier == LeagueTier::None) c.body = 0x1F1B19;
	return Paint(TableOf(tier), c, scale);
}

LeagueImage LeagueVexillumPixels(LeagueTier tier, int scale)
{
	const LeagueColors c = LeagueTierColors(tier == LeagueTier::None ? LeagueTier::Legate : tier);
	const std::vector<Px> table = {{9, 0, 2, 2, kL, 1}, {3, 2, 14, 1, kL, 1}, {2, 2, 1, 1, kL, 1}, {17, 2, 1, 1, kL, 1}, {4, 3, 12, 12, kB, 1},
		{5, 3, 10, 11, kC, 1}, {5, 3, 5, 11, kWhite, 0.12f}, {9, 5, 2, 7, kL, 1}, {5, 15, 1, 1, kL, 1}, {7, 15, 1, 1, kL, 1}, {12, 15, 1, 1, kL, 1},
		{14, 15, 1, 1, kL, 1}, {9, 15, 2, 5, 0x7A6553, 1}};
	return Paint(table, c, scale);
}

LeagueColors LeagueFounderColors()
{
	// Gold over the brand's red, as the champion of a season's final, so it reads apart from every rank.
	return {0x5A0A0E, 0x8A0E13, 0xF2D06B, 0xF2D06B};
}

LeagueImage LeagueFounderPixels(int scale)
{
	// The vexillum with a gold trim and a gold lozenge on the red cloth, in place of a rank's bar.
	const std::vector<Px> table = {{9, 0, 2, 2, kL, 1}, {3, 2, 14, 1, kL, 1}, {2, 2, 1, 1, kL, 1}, {17, 2, 1, 1, kL, 1}, {4, 3, 12, 12, kL, 1},
		{5, 3, 10, 11, kB, 1}, {6, 4, 8, 9, kC, 1}, {6, 4, 4, 9, kWhite, 0.12f}, {9, 5, 2, 1, kL, 1}, {8, 6, 4, 1, kL, 1}, {7, 7, 6, 2, kL, 1},
		{8, 9, 4, 1, kL, 1}, {9, 10, 2, 1, kL, 1}, {5, 15, 1, 1, kL, 1}, {7, 15, 1, 1, kL, 1}, {12, 15, 1, 1, kL, 1}, {14, 15, 1, 1, kL, 1},
		{9, 15, 2, 5, 0x7A6553, 1}};
	return Paint(table, LeagueFounderColors(), scale);
}

LeagueColors LeagueMetalColors(LeagueMetal metal)
{
	const PhaleraShades s = ShadesOf(metal);
	return {s.edge, s.body, s.light, s.light};
}

LeagueImage LeagueTrophyImage(LeagueMetal metal, int size)
{
	// The mockups' 32-unit phalera, scaled to the 160 units of the scenes: a rim, a ring and the star, nothing finer than a pixel at 20.
	const PhaleraShades c = ShadesOf(metal);
	Scene s;
	s.Fill(Circle(80, 80, 76), c.edge);
	s.Fill(Circle(80, 80, 68), c.body);
	s.Fill(Ellipse(52, 52, 42, 42), kWhite, 0.06);
	Band(s, 80, 80, 52, 10, c.light);
	s.Fill(Star(80, 80, 38, 16), c.light);
	return Rasterize(s, std::max(8, std::min(512, size)));
}

LeagueImage LeagueTrophyPixels(LeagueMetal metal, int scale)
{
	return LeagueTrophyImage(metal, kLeagueBadgeGrid * std::max(1, scale));
}

LeagueImage LeaguePhaleraImage(LeagueMetal metal, int size)
{
	// TorneoCampeon's 200-unit phalera at 0.8.
	const PhaleraShades c = ShadesOf(metal);
	Scene s;
	s.Fill(Circle(80, 80, 78.4), c.rim);
	s.Fill(Circle(80, 80, 77.6), c.deep);
	s.Fill(Circle(80, 80, 74.4), c.edge);
	s.Fill(Circle(80, 80, 72.8), c.body);
	Band(s, 80, 80, 66.4, 2.4, c.light);
	for (int k = 0; k < 16; k++) {
		const double a = k * kPi / 8;
		s.Fill(Circle(80 + 59.2 * std::cos(a), 80 + 59.2 * std::sin(a), 3.2), c.light);
	}
	s.Fill(Circle(80, 80, 51.2), c.edge);
	s.Fill(Circle(80, 80, 46.4), c.inner);
	s.Fill(Star(80, 80, 36.8, 15.2), c.light);
	s.Fill(Star(80, 80, 24, 10.4), c.bright);
	s.Fill(Circle(80, 80, 5.6), c.inner);
	return Rasterize(s, std::max(8, std::min(512, size)));
}

LeagueImage LeagueEmblemImage(LeagueTier tier, int size)
{
	return Rasterize(SceneOf(tier), std::max(8, std::min(512, size)));
}
