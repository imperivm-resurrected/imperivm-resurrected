// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/menuart.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <utility>

namespace {

unsigned U32(const std::vector<unsigned char> &b, size_t at)
{
	return at + 4 <= b.size() ? static_cast<unsigned>(b[at] | b[at + 1] << 8 | b[at + 2] << 16 | b[at + 3] << 24) : 0u;
}

void Put32(std::vector<unsigned char> &b, size_t at, unsigned v)
{
	for (int i = 0; i < 4; i++) b[at + i] = static_cast<unsigned char>(v >> (8 * i));
}

std::string Trim(const std::string &s)
{
	size_t a = 0, z = s.size();
	while (a < z && (s[a] == ' ' || s[a] == '\t')) a++;
	while (z > a && (s[z - 1] == ' ' || s[z - 1] == '\t' || s[z - 1] == '\r')) z--;
	return s.substr(a, z - a);
}

bool KeyIs(const std::string &line, const char *key, std::string &value)
{
	const std::string t = Trim(line);
	const size_t n = strlen(key);
	if (t.size() <= n || _strnicmp(t.c_str(), key, n) != 0) return false;
	const std::string rest = Trim(t.substr(n));
	if (rest.empty() || rest[0] != '=') return false;
	value = Trim(rest.substr(1));
	return true;
}

bool IsFullFrame(std::string rect)
{
	std::string compact;
	for (char c : rect) {
		if (c != ' ' && c != '\t') compact += static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return compact == "0,0,%larghezza%,%altezza%";
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::vector<std::pair<std::string, std::string>> Pairs(const std::string &ini)
{
	std::vector<std::pair<std::string, std::string>> out;
	for (size_t at = 0; at < ini.size();) {
		size_t end = ini.find('\n', at);
		if (end == std::string::npos) end = ini.size();
		std::string line = Trim(ini.substr(at, end - at));
		at = end + 1;
		const size_t comment = line.find(';');
		if (comment != std::string::npos) line = line.substr(0, comment);
		const size_t eq = line.find('=');
		if (eq != std::string::npos && line.compare(0, 1, "[") != 0) out.push_back({Trim(line.substr(0, eq)), Trim(line.substr(eq + 1))});
	}
	return out;
}

std::string Expand(const std::string &text, const std::map<std::string, std::string> &vars, int depth = 0)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		const size_t close = text[i] == '%' ? text.find('%', i + 1) : std::string::npos;
		const auto it = close == std::string::npos ? vars.end() : vars.find(Lower(text.substr(i + 1, close - i - 1)));
		if (it == vars.end() || depth > 16) {
			out += text[i];
			continue;
		}
		out += Expand(it->second, vars, depth + 1);
		i = close;
	}
	return out;
}

struct Calc {
	const std::string &s;
	size_t i = 0;
	bool ok = true;
	void Skip()
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '	')) i++;
	}
	long Term()
	{
		Skip();
		if (i < s.size() && s[i] == '(') {
			i++;
			const long v = Sum();
			Skip();
			if (i < s.size() && s[i] == ')') i++;
			else ok = false;
			return v;
		}
		if (i < s.size() && s[i] == '-') {
			i++;
			return -Term();
		}
		if (i >= s.size() || !isdigit(static_cast<unsigned char>(s[i]))) {
			ok = false;
			return 0;
		}
		long v = 0;
		while (i < s.size() && isdigit(static_cast<unsigned char>(s[i]))) v = v * 10 + (s[i++] - '0');
		return v;
	}
	long Product()
	{
		long v = Term();
		for (Skip(); ok && i < s.size() && (s[i] == '*' || s[i] == '/'); Skip()) {
			const char op = s[i++];
			const long r = Term();
			if (op == '*') v *= r;
			else if (r) v /= r;
			else ok = false;
		}
		return v;
	}
	long Sum()
	{
		long v = Product();
		for (Skip(); ok && i < s.size() && (s[i] == '+' || s[i] == '-'); Skip()) {
			const char op = s[i++];
			const long r = Product();
			v = op == '+' ? v + r : v - r;
		}
		return v;
	}
};

bool Evaluate(std::string text, long &value)
{
	text = Trim(text);
	if (text.size() >= 2 && text.front() == '#' && text.back() == '#') text = text.substr(1, text.size() - 2);
	Calc c{text};
	value = c.Sum();
	c.Skip();
	return c.ok && c.i == text.size() && !text.empty();
}

// The game's own menus fit a 1366x768 frame: a piece that starts beyond it was placed on a 1920x1080 one.
bool StartsBeyond1366(const std::string &rect, const std::map<std::string, std::string> &vars)
{
	const std::string expanded = Expand(rect, vars);
	const size_t a = expanded.find(','), b = a == std::string::npos ? a : expanded.find(',', a + 1);
	if (b == std::string::npos) return false;
	long x = 0, y = 0;
	return Evaluate(expanded.substr(0, a), x) && Evaluate(expanded.substr(a + 1, b - a - 1), y) && (x >= 1366 || y >= 768);
}

}

bool ScaleBmpToCover(const std::vector<unsigned char> &src, int w, int h, std::vector<unsigned char> &out)
{
	if (src.size() < 54 || src[0] != 'B' || src[1] != 'M' || U32(src, 14) < 40) return false;
	const int sw = static_cast<int>(U32(src, 18)), sh = static_cast<int>(U32(src, 22));
	if (src[28] != 24 || src[29] != 0 || U32(src, 30) != 0 || sw <= 0 || sh <= 0) return false;
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || (w == sw && h == sh)) return false;
	const size_t offset = U32(src, 10), srcStride = (static_cast<size_t>(sw) * 3 + 3) & ~static_cast<size_t>(3);
	if (offset + srcStride * sh > src.size()) return false;
	int cropW = sw, cropH = sh;
	if (static_cast<long long>(sw) * h > static_cast<long long>(sh) * w) cropW = static_cast<int>(static_cast<long long>(sh) * w / h);
	else cropH = static_cast<int>(static_cast<long long>(sw) * h / w);
	const int left = (sw - cropW) / 2, bottom = sh - (sh - cropH) / 2 - cropH;
	const size_t dstStride = (static_cast<size_t>(w) * 3 + 3) & ~static_cast<size_t>(3);
	out.assign(54 + dstStride * h, 0);
	memcpy(out.data(), src.data(), 54);
	Put32(out, 2, static_cast<unsigned>(out.size()));
	Put32(out, 10, 54);
	Put32(out, 14, 40);
	Put32(out, 18, static_cast<unsigned>(w));
	Put32(out, 22, static_cast<unsigned>(h));
	Put32(out, 34, static_cast<unsigned>(dstStride * h));
	if (cropW < w) {
		// Larger than the art: each pixel blends the four nearest ones of the crop.
		for (int y = 0; y < h; y++) {
			const float fy = std::min(std::max((y + 0.5f) * cropH / h - 0.5f, 0.0f), static_cast<float>(cropH - 1));
			const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, cropH - 1);
			const float ty = fy - y0;
			for (int x = 0; x < w; x++) {
				const float fx = std::min(std::max((x + 0.5f) * cropW / w - 0.5f, 0.0f), static_cast<float>(cropW - 1));
				const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, cropW - 1);
				const float tx = fx - x0;
				const unsigned char *a = &src[offset + srcStride * (bottom + y0) + (left + x0) * 3], *b = &src[offset + srcStride * (bottom + y0) + (left + x1) * 3];
				const unsigned char *c = &src[offset + srcStride * (bottom + y1) + (left + x0) * 3], *d = &src[offset + srcStride * (bottom + y1) + (left + x1) * 3];
				unsigned char *q = &out[54 + dstStride * y + x * 3];
				for (int k = 0; k < 3; k++) {
					const float top = a[k] + (b[k] - a[k]) * tx, low = c[k] + (d[k] - c[k]) * tx;
					q[k] = static_cast<unsigned char>(top + (low - top) * ty + 0.5f);
				}
			}
		}
		return true;
	}
	for (int y = 0; y < h; y++) {
		const int y0 = bottom + y * cropH / h, y1 = bottom + (y + 1) * cropH / h > y0 ? bottom + (y + 1) * cropH / h : y0 + 1;
		for (int x = 0; x < w; x++) {
			const int x0 = left + x * cropW / w, x1 = left + (x + 1) * cropW / w > x0 ? left + (x + 1) * cropW / w : x0 + 1;
			unsigned sum[3] = {0, 0, 0};
			for (int sy = y0; sy < y1; sy++) {
				for (int sx = x0; sx < x1; sx++) {
					const unsigned char *p = &src[offset + srcStride * sy + sx * 3];
					sum[0] += p[0];
					sum[1] += p[1];
					sum[2] += p[2];
				}
			}
			const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
			unsigned char *q = &out[54 + dstStride * y + x * 3];
			for (int c = 0; c < 3; c++) q[c] = static_cast<unsigned char>(sum[c] / count);
		}
	}
	return true;
}

// The interface's bars are a pattern that repeats along a long picture with an ornament at its start: past the picture's
// end the pattern goes on, from the last whole repetition before its last few columns.
bool ExtendBarBmp(const std::vector<unsigned char> &src, int w, std::vector<unsigned char> &out)
{
	if (src.size() < 54 || src[0] != 'B' || src[1] != 'M' || U32(src, 14) < 40) return false;
	const int sw = static_cast<int>(U32(src, 18)), rows = std::abs(static_cast<int>(U32(src, 22)));
	const int bpp = src[28] | src[29] << 8;
	const unsigned compression = U32(src, 30);
	if ((bpp != 16 && bpp != 24 && bpp != 32) || (compression != 0 && compression != 3)) return false;
	if (sw < 1024 || rows <= 0 || rows > 200 || w <= sw || w > 8192) return false;
	const size_t offset = U32(src, 10), px = bpp / 8, srcStride = (static_cast<size_t>(sw) * px + 3) & ~static_cast<size_t>(3);
	if (offset < 54 || offset + srcStride * rows > src.size()) return false;
	const int base = sw - 8, from = sw / 2;
	auto at = [&](int x, int y) { return &src[offset + srcStride * y + static_cast<size_t>(x) * px]; };
	int period = 0;
	unsigned long long bestDiff = 0;
	// The difference per column between the last stretch of p columns and the one before it: the smallest marks the period.
	for (int p = 32; p <= base - from; p++) {
		const unsigned long long limit = period ? (bestDiff * p + period - 1) / period + 1 : ~0ull;
		unsigned long long diff = 0;
		for (int y = 0; y < rows && diff < limit; y += 2) {
			for (int x = base - p; x < base && diff < limit; x += 2) {
				const unsigned char *a = at(x, y), *b = at(x - p, y);
				for (size_t k = 0; k < px; k++) diff += static_cast<unsigned>(std::abs(a[k] - b[k]));
			}
		}
		if (!period || diff * period < bestDiff * p) {
			bestDiff = diff;
			period = p;
		}
	}
	if (!period) return false;
	const size_t dstStride = (static_cast<size_t>(w) * px + 3) & ~static_cast<size_t>(3);
	out.assign(offset + dstStride * rows, 0);
	memcpy(out.data(), src.data(), offset);
	Put32(out, 2, static_cast<unsigned>(out.size()));
	Put32(out, 18, static_cast<unsigned>(w));
	Put32(out, 34, static_cast<unsigned>(dstStride * rows));
	for (int y = 0; y < rows; y++) {
		unsigned char *row = &out[offset + dstStride * y];
		memcpy(row, at(0, y), static_cast<size_t>(base) * px);
		for (int x = base; x < w; x++) memcpy(row + static_cast<size_t>(x) * px, at(base - period + (x - base) % period, y), px);
	}
	return true;
}

// A bar's piece is "RectWH = 0, 0, 2048, <height>" with HAlign 0, 0, 1: the extra width of the screen goes to its right
// margin, so the piece keeps the width written here.
bool WidenBarRects(std::string &ini, int from, int to)
{
	bool changed = false;
	for (size_t at = 0; at < ini.size();) {
		size_t end = ini.find('\n', at);
		if (end == std::string::npos) end = ini.size();
		std::string value;
		int x = -1, y = -1, w = 0, h = 0;
		if (KeyIs(ini.substr(at, end - at), "RectWH", value) && sscanf_s(value.c_str(), "%d , %d , %d , %d", &x, &y, &w, &h) == 4 && x == 0 && y == 0 && w == from) {
			const bool cr = end > at && ini[end - 1] == '\r';
			const std::string line = "RectWH = 0, 0, " + std::to_string(to) + ", " + std::to_string(h) + (cr ? "\r" : "");
			ini.replace(at, end - at, line);
			end = at + line.size();
			changed = true;
		}
		at = end + 1;
	}
	return changed;
}

namespace {

bool FirstDialogFillsFrame(const std::string &ini)
{
	bool inside = false;
	for (size_t at = 0; at < ini.size();) {
		size_t end = ini.find('\n', at);
		if (end == std::string::npos) end = ini.size();
		const std::string line = Trim(ini.substr(at, end - at));
		at = end + 1;
		if (line.compare(0, 1, "[") == 0) {
			if (inside) return false;
			inside = true;
			continue;
		}
		std::string value;
		if (inside && KeyIs(line, "RectWH", value)) return IsFullFrame(value);
	}
	return false;
}

}

bool MenuLayoutIsFixed(const std::string &mainMenu, const std::string &tmpl)
{
	if (!FirstDialogFillsFrame(mainMenu)) return false;
	static const char *const kDefinitions[] = {"larghezza", "altezza", "screenleft", "screentop", "screenright", "screenbottom", "screenspacing"};
	static const char *const kEdges[] = {"%larghezza%", "%altezza%", "%screenright%", "%screenbottom%"};
	const auto menu = Pairs(mainMenu), params = Pairs(tmpl);
	for (const auto *pairs : {&menu, &params}) {
		for (const auto &kv : *pairs) {
			const std::string key = Lower(kv.first);
			if (std::find(std::begin(kDefinitions), std::end(kDefinitions), key) != std::end(kDefinitions)) continue;
			// Only where a piece starts counts: a size of %Larghezza% fills the screen without leaning on its edges.
			const std::string place = Lower(key == "rectwh" ? kv.second.substr(0, kv.second.find(',', kv.second.find(',') + 1)) : kv.second);
			for (const char *edge : kEdges) {
				if (place.find(edge) != std::string::npos) return false;
			}
		}
	}
	std::map<std::string, std::string> vars;
	for (const auto &kv : params) vars[Lower(kv.first)] = kv.second;
	for (const auto &kv : params) {
		const std::string key = Lower(kv.first);
		if (key.size() > 10 && key.compare(0, 6, "button") == 0 && key.compare(key.size() - 4, 4, "rect") == 0 && StartsBeyond1366(kv.second, vars)) return true;
	}
	for (const auto &kv : menu) {
		if (Lower(kv.first) == "rectwh" && StartsBeyond1366(kv.second, vars)) return true;
	}
	return false;
}

bool FitFullFrameImages(std::string &ini, const std::function<std::string(const std::string &image)> &fit)
{
	std::vector<std::string> lines;
	for (size_t at = 0; at <= ini.size();) {
		size_t end = ini.find('\n', at);
		if (end == std::string::npos) end = ini.size();
		lines.push_back(ini.substr(at, end - at));
		at = end + 1;
	}
	bool changed = false;
	for (size_t start = 0; start < lines.size();) {
		size_t end = start + 1;
		while (end < lines.size() && Trim(lines[end]).compare(0, 1, "[") != 0) end++;
		bool fullFrame = false;
		size_t imageLine = 0;
		std::string value, image;
		for (size_t i = start; i < end; i++) {
			if (KeyIs(lines[i], "RectWH", value)) fullFrame = IsFullFrame(value);
			else if (KeyIs(lines[i], "Image", value)) {
				imageLine = i;
				image = Trim(value.substr(0, value.find(',')));
			}
		}
		if (fullFrame && !image.empty()) {
			const std::string fitted = fit(image);
			if (!fitted.empty()) {
				const bool cr = !lines[imageLine].empty() && lines[imageLine].back() == '\r';
				lines[imageLine] = "Image = " + fitted + ", 0, 0" + (cr ? "\r" : "");
				changed = true;
			}
		}
		start = end;
	}
	if (!changed) return false;
	std::string out;
	for (size_t i = 0; i < lines.size(); i++) out += (i ? "\n" : "") + lines[i];
	ini = out;
	return true;
}
