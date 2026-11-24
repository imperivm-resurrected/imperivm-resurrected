// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "app/overlay_view.h"

#include <emmintrin.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "i18n/tr.h"

namespace overlay {

namespace {

namespace pal = ui::pal;

const COLORREF kBack = pal::bg;
const COLORREF kGraphBack = pal::shadow;
const COLORREF kIdleBand = RGB(0x17, 0x16, 0x15);
const COLORREF kHatch = RGB(0x26, 0x24, 0x22);
const COLORREF kBarDim = RGB(0x4c, 0x6e, 0x42);

const int kFullW = 360;
const int kPad = 10;
const int kLabelW = 74;
const int kSparkW = 72;

std::wstring Dec(double v, int decimals)
{
	wchar_t buf[48];
	swprintf_s(buf, L"%.*f", decimals, v);
	// "." is translated to each language's decimal mark.
	const std::wstring mark = TrW(L".");
	for (wchar_t *p = buf; *p; p++) {
		if (*p == L'.' && !mark.empty()) *p = mark[0];
	}
	return buf;
}

std::wstring Caps(const std::wstring &text)
{
	std::wstring out = text;
	if (!out.empty()) CharUpperBuffW(&out[0], static_cast<DWORD>(out.size()));
	return out;
}

COLORREF FpsColor(float fps) { return fps >= 55 ? pal::ok : fps >= 30 ? pal::warn : pal::bad; }
COLORREF LoadColor(float pct) { return pct < 70 ? pal::ok : pct < 90 ? pal::warn : pal::bad; }
COLORREF MemColor(double share) { return share < 0.7 ? pal::ok : share < 0.85 ? pal::warn : pal::bad; }
COLORREF PingColor(int ping) { return ping < 120 ? pal::ok : ping < 250 ? pal::warn : pal::bad; }
COLORREF TurnColor(int ms) { return ms < 170 ? pal::ok : ms < 320 ? pal::warn : pal::bad; }
COLORREF LagColor(int ms) { return ms < 3000 ? pal::ok : ms < 6000 ? pal::warn : pal::bad; }

COLORREF Mix(COLORREF a, COLORREF b, int amount)
{
	auto ch = [&](int x, int y) { return x + (y - x) * amount / 255; };
	return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}

struct Fonts {
	HFONT title, big, value, text, caption;
	explicit Fonts(float s)
	{
		title = ui::BrandFont(ui::BrandFace::Display, 17, s);
		big = ui::BrandFont(ui::BrandFace::Display, 30, s);
		value = ui::BrandFont(ui::BrandFace::UiStrong, 13, s);
		text = ui::BrandFont(ui::BrandFace::Ui, 12, s);
		caption = ui::BrandFont(ui::BrandFace::Ui, 10, s);
	}
};

// Painting on the canvas in its own pixels; S() turns the layout's units into them.
struct Pen {
	HDC dc;
	float s;
	ui::Canvas *cv;
	int S(double v) const { return static_cast<int>(std::lround(v * s)); }
	// Straight into the canvas's pixels, without a GDI call for each of the graph's columns.
	void Fill(int x, int y, int w, int h, COLORREF c) const
	{
		if (w <= 0 || h <= 0 || !cv || !cv->bits || dc != cv->dc) {
			if (w <= 0 || h <= 0) return;
			RECT r = {x, y, x + w, y + h};
			SetDCBrushColor(dc, c);
			FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
			return;
		}
		GdiFlush();
		const int x0 = std::max(0, x), x1 = std::min(cv->w, x + w), y0 = std::max(0, y), y1 = std::min(cv->h, y + h);
		const uint32_t px = ui::PixelOf(c);
		for (int py = y0; py < y1; py++) std::fill(cv->bits + static_cast<size_t>(py) * cv->w + x0, cv->bits + static_cast<size_t>(py) * cv->w + x1, px);
	}
	int Width(const std::wstring &t, HFONT f) const
	{
		SelectObject(dc, f);
		SIZE sz = {};
		GetTextExtentPoint32W(dc, t.c_str(), static_cast<int>(t.size()), &sz);
		return sz.cx;
	}
	// Text in a band from y, h tall, centred on it; from x, or ending at x with DT_RIGHT. Returns its width. ExtTextOut: DrawText
	// costs several times more, and the panel draws dozens of texts each refresh.
	int Text(int x, int y, int h, const std::wstring &t, HFONT f, COLORREF c, UINT align = DT_LEFT) const
	{
		const int w = Width(t, f);
		SetTextColor(dc, c);
		const int left = align == DT_RIGHT ? x - w : align == DT_CENTER ? x - w / 2 : x;
		ExtTextOutW(dc, left, y + (h - LineHeight(f)) / 2, 0, nullptr, t.c_str(), static_cast<UINT>(t.size()), nullptr);
		return w;
	}
	// The same, cut with "…" to `maxW`.
	int TextFit(int x, int y, int h, std::wstring t, HFONT f, COLORREF c, int maxW) const
	{
		if (Width(t, f) > maxW) {
			while (!t.empty() && Width(t + L"…", f) > maxW) t.pop_back();
			t += L"…";
		}
		return Text(x, y, h, t, f, c);
	}
	int LineHeight(HFONT f) const
	{
		static std::map<HFONT, int> heights;
		const auto it = heights.find(f);
		if (it != heights.end()) return it->second;
		SelectObject(dc, f);
		TEXTMETRICW tm = {};
		GetTextMetricsW(dc, &tm);
		return heights[f] = tm.tmHeight;
	}
	void Dashed(int x, int y, int w, COLORREF c, int on, int off) const
	{
		for (int i = 0; i < w; i += on + off) Fill(x + i, y, std::min(on, w - i), 1, c);
	}
	// A shape with smooth edges straight into the canvas's pixels, its coverage sampled 4x4 a pixel once per size; `inside` takes
	// coordinates from 0 to 1.
	void Shape(int x, int y, int w, int h, COLORREF c, bool (*inside)(double, double)) const
	{
		static std::map<std::tuple<bool (*)(double, double), int, int>, std::vector<uint8_t>> masks;
		std::vector<uint8_t> &mask = masks[std::make_tuple(inside, w, h)];
		if (mask.size() != static_cast<size_t>(w) * h) {
			mask.assign(static_cast<size_t>(w) * h, 0);
			for (int py = 0; py < h; py++) {
				for (int px = 0; px < w; px++) {
					int hits = 0;
					for (int sy = 0; sy < 4; sy++) {
						for (int sx = 0; sx < 4; sx++) hits += inside((px + (sx + 0.5) / 4.0) / w, (py + (sy + 0.5) / 4.0) / h) ? 1 : 0;
					}
					mask[static_cast<size_t>(py) * w + px] = static_cast<uint8_t>(hits * 255 / 16);
				}
			}
		}
		GdiFlush();
		const uint32_t cr = GetRValue(c), cg = GetGValue(c), cb = GetBValue(c);
		for (int py = std::max(0, y); py < std::min(cv->h, y + h); py++) {
			for (int px = std::max(0, x); px < std::min(cv->w, x + w); px++) {
				const uint32_t a = mask[static_cast<size_t>(py - y) * w + (px - x)];
				if (!a) continue;
				uint32_t &d = cv->bits[static_cast<size_t>(py) * cv->w + px];
				const uint32_t r = ((cr * a) + ((d >> 16) & 255) * (255 - a)) / 255, g = ((cg * a) + ((d >> 8) & 255) * (255 - a)) / 255,
							   b = ((cb * a) + (d & 255) * (255 - a)) / 255;
				d = (r << 16) | (g << 8) | b;
			}
		}
	}
	void Dot(int cx, int cy, int r, COLORREF c) const
	{
		Shape(cx - r, cy - r, r * 2, r * 2, c, [](double u, double v) { return (u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5) <= 0.25; });
	}
	void Gear(int x, int y, int size, COLORREF c) const
	{
		Shape(x, y, size, size, c, [](double u, double v) {
			const double dx = u - 0.5, dy = v - 0.5, d = std::sqrt(dx * dx + dy * dy);
			if (d < 0.17) return false;
			if (d < 0.36) return true;
			const double a = std::atan2(dy, dx) * 8 / 6.283185307 + 8;
			return d < 0.5 && a - std::floor(a) < 0.5;
		});
	}
};

COLORREF StateColor(const View &v)
{
	if (v.recordingLeft >= 0) return pal::red;
	switch (v.screen) {
	case Screen::kMatch: return v.drawing ? pal::ok : pal::warn;
	case Screen::kStalled: return pal::bad;
	case Screen::kPaused:
	case Screen::kLoading: return pal::warn;
	default: return v.drawing ? pal::ok : pal::caption;
	}
}

std::wstring StateText(const View &v)
{
	if (v.recordingLeft >= 0) return TrW(L"REC") + L" " + std::to_wstring(v.recordingLeft) + L" s";
	switch (v.screen) {
	case Screen::kMatch: return TrW(L"in a match");
	case Screen::kStalled: return TrW(L"no image");
	case Screen::kPaused: return TrW(L"paused");
	case Screen::kLoading: return TrW(L"loading");
	default: return v.drawing ? TrW(L"menu") : TrW(L"menu · idle");
	}
}

void DrawLine(const Pen &p, int x, int y, int w, int h, const overlay::Spark &spark, float top, int window)
{
	if (spark.values.empty() || top <= 0) return;
	const int n = std::max(2, window);
	const size_t first = spark.values.size() > static_cast<size_t>(n) ? spark.values.size() - n : 0;
	std::vector<POINT> pts;
	HPEN pen = CreatePen(PS_SOLID, std::max(1, p.S(1.4)), spark.color);
	HGDIOBJ old = SelectObject(p.dc, pen);
	auto flush = [&] {
		if (pts.size() >= 2) Polyline(p.dc, pts.data(), static_cast<int>(pts.size()));
		pts.clear();
	};
	for (size_t i = first; i < spark.values.size(); i++) {
		const float value = spark.values[i];
		if (value < 0) {
			flush();
			continue;
		}
		const int k = static_cast<int>(i - first) + (n - static_cast<int>(spark.values.size() - first));
		const int px = x + k * (w - 1) / (n - 1);
		const int py = y + h - 1 - static_cast<int>(std::min(1.0f, value / top) * (h - 2));
		pts.push_back({px, py});
	}
	flush();
	SelectObject(p.dc, old);
	DeleteObject(pen);
}

void DrawSpark(const Pen &p, int x, int y, int w, int h, const overlay::Spark &spark, float top, int window)
{
	p.Fill(x, y, w, h, kGraphBack);
	DrawLine(p, x, y, w, h, spark, top, window);
}

// A graph's scale or guide value, on a box of the graph's background so the bars don't hide it.
void Label(const Pen &p, int x, int y, const std::wstring &text, HFONT font, UINT align = DT_LEFT)
{
	const int w = p.Width(text, font), h = p.S(12), left = align == DT_RIGHT ? x - w : x;
	p.Fill(left - p.S(2), y, w + p.S(4), h, kGraphBack);
	p.Text(x, y, h, text, font, pal::caption, align);
}

float NiceTop(float v, float minimum)
{
	const float steps[] = {50, 100, 150, 200, 300, 500, 1000};
	for (float s : steps) {
		if (s >= minimum && s >= v) return s;
	}
	return 1000;
}

void FrameGraph(const Pen &p, const Fonts &f, const View &v, int x, int y, int w, int h)
{
	p.Fill(x, y, w, h, kGraphBack);
	// Scaled to the usual frames, not to the worst: a long hitch reaches the top and gets a red cap.
	const float top = NiceTop(v.frames.p99Ms * 1.5f, 50);
	auto Y = [&](float ms) { return y + h - 1 - static_cast<int>(std::min(1.0f, ms / top) * (h - 2)); };
	const int colW = std::max(1, p.S(2));
	const int n = static_cast<int>(v.columns.size());
	for (int c = 0; c < n; c++) {
		const perf::GraphColumn &g = v.columns[c];
		const int cx = x + w - (n - c) * colW;
		if (cx < x) continue;
		if (!(g.flags & perf::kColumnFrames)) {
			if (g.flags & perf::kColumnIdle) p.Fill(cx, y, colW, h, kIdleBand);
			continue;
		}
		const int by = Y(g.maxMs);
		const bool hitch = (g.flags & perf::kColumnHitch) != 0;
		const bool slow = v.hitchLineMs > 0 && g.maxMs > v.hitchLineMs * 0.75f;
		const COLORREF body = hitch ? pal::red : slow ? Mix(kBarDim, pal::warn, 140) : kBarDim;
		p.Fill(cx, by, colW, y + h - by, body);
		p.Fill(cx, by, colW, std::max(1, p.S(1)), hitch ? pal::redText : slow ? pal::warn : pal::ok);
		if (g.maxMs > top) p.Fill(cx, y, colW, std::max(2, p.S(2)), pal::redText);
	}
	// Where the game drew only on demand, hatched.
	GdiFlush();
	const int step = std::max(4, p.S(6));
	const uint32_t idle = ui::PixelOf(kIdleBand), hatch = ui::PixelOf(kHatch);
	for (int py = std::max(0, y); py < std::min(p.cv->h, y + h); py++) {
		uint32_t *row = p.cv->bits + static_cast<size_t>(py) * p.cv->w;
		for (int px = std::max(0, x); px < std::min(p.cv->w, x + w); px++) {
			if ((row[px] & 0xffffff) == idle && (px + py) % step == 0) row[px] = hatch;
		}
	}
	for (float guide : {1000.0f / 60, 1000.0f / 30}) {
		if (guide >= top) continue;
		const int gy = Y(guide);
		p.Dashed(x, gy, w, pal::lineStrong, p.S(3), p.S(3));
		Label(p, x + p.S(3), gy - p.S(12), Dec(guide, 1), f.caption);
	}
	if (v.hitchLineMs > 0 && v.hitchLineMs < top) p.Dashed(x, Y(v.hitchLineMs), w, pal::redPressed, p.S(1), p.S(2));
	Label(p, x + w - p.S(3), y + p.S(1), Dec(top, 0) + L" ms", f.caption, DT_RIGHT);
	for (int c = 0; c < n; c++) {
		const perf::GraphColumn &g = v.columns[c];
		if (!(g.flags & perf::kColumnEvent)) continue;
		const int cx = x + w - (n - c) * colW;
		if (cx < x) continue;
		p.Fill(cx, y, std::max(1, p.S(1)), h, pal::textDim);
		static const wchar_t *const kNames[] = {L"", L"Load mark", L"Start mark", L"End mark", L"Desync mark", L"Rejoin mark", L"Capture mark"};
		const std::wstring name = g.event < 7 ? TrW(kNames[g.event]) : L"";
		if (name.empty()) continue;
		const std::wstring letter(1, name[0]);
		const int lw = p.Width(letter, f.caption) + p.S(4);
		const int lx = std::min(x + w - lw, cx + 1);
		p.Fill(lx, y + h - p.S(13), lw, p.S(12), pal::button);
		p.Text(lx + lw / 2, y + h - p.S(13), p.S(12), letter, f.caption, pal::text, DT_CENTER);
	}
}

void Meter(const Pen &p, int x, int y, int w, int h, double share, COLORREF c)
{
	p.Fill(x, y, w, h, kGraphBack);
	p.Fill(x, y, static_cast<int>(w * std::min(1.0, std::max(0.0, share))), h, c);
	p.Fill(x + static_cast<int>(w * 0.85), y - p.S(1), std::max(1, p.S(1)), h + p.S(2), pal::red);
}

struct Part {
	std::wstring text;
	COLORREF color;
	HFONT font;
};

// Parts one after another from x, in a band from y, h tall; returns where they end.
int Parts(const Pen &p, int x, int y, int h, const std::vector<Part> &parts, int gap)
{
	for (size_t i = 0; i < parts.size(); i++) x += p.Text(x, y, h, parts[i].text, parts[i].font, parts[i].color) + (i + 1 < parts.size() ? gap : 0);
	return x;
}

int PartsWidth(const Pen &p, const std::vector<Part> &parts, int gap)
{
	int w = 0;
	for (size_t i = 0; i < parts.size(); i++) w += p.Width(parts[i].text, parts[i].font) + (i + 1 < parts.size() ? gap : 0);
	return w;
}

std::wstring Ms(int ms) { return std::to_wstring(ms) + L" ms"; }

std::vector<Part> CompactParts(const View &v, const Fonts &f)
{
	std::vector<Part> parts;
	if (v.set.perf) {
		if (v.drawing && v.frames.nowFps > 0) {
			parts.push_back({Dec(v.frames.nowFps, 0) + L" FPS", FpsColor(v.frames.nowFps), f.value});
			parts.push_back({Dec(v.frames.nowMs, 1) + L" ms", pal::body, f.text});
			if (v.set.showLows && v.frames.frames >= 100) parts.push_back({TrW(L"1% low ") + Dec(v.frames.low1Fps, 0), FpsColor(v.frames.low1Fps), f.text});
			if (v.frames.hitches) parts.push_back({std::to_wstring(v.frames.hitches) + L" " + TrW(L"hitches"), pal::bad, f.text});
		}
		else {
			parts.push_back({Caps(StateText(v)), pal::caption, f.caption});
		}
		if (v.set.showCpu && v.cpuThread >= 0) parts.push_back({L"CPU " + Dec(v.cpuThread, 0) + L" %", LoadColor(v.cpuThread), f.text});
		if (v.set.showMemory && v.addrTotalGB > 0) parts.push_back({Dec(v.addrUsedGB, 1) + L" GB", MemColor(v.addrUsedGB / v.addrTotalGB), f.text});
	}
	if (v.set.net) {
		int worst = -1;
		for (const PeerLine &peer : v.peers) {
			if (peer.connected) worst = std::max(worst, peer.ping);
		}
		if (worst >= 0) parts.push_back({Ms(worst), PingColor(worst), f.text});
		if (v.turnAvgMs >= 0) parts.push_back({TrW(L"turn ") + Ms(v.turnAvgMs), TurnColor(v.turnAvgMs), f.text});
		if (v.lagMs >= 0) parts.push_back({TrW(L"delay ") + Dec(v.lagMs / 1000.0, 1) + L" s", LagColor(v.lagMs), f.text});
	}
	if (v.recordingLeft >= 0 && v.set.perf && v.drawing) parts.push_back({StateText(v), pal::redText, f.value});
	return parts;
}

void PaintCompact(ui::Canvas &cv, const View &v, std::vector<Hit> &hits, HDC measure)
{
	const Fonts f(v.s);
	Pen m = {measure, v.s, &cv};
	const std::vector<Part> parts = CompactParts(v, f);
	const int pad = m.S(8), gap = m.S(10), h = m.S(24), dot = m.S(4), gear = m.S(14);
	const bool showGear = v.hover || v.menuOpen;
	const int w = pad + dot * 2 + gap + PartsWidth(m, parts, gap) + (showGear ? gap + gear : 0) + pad;
	if (!cv.Ensure(w, h)) return;
	Pen p = {cv.dc, v.s, &cv};
	p.Fill(0, 0, w, h, kBack);
	p.Fill(0, 0, m.S(2), h, StateColor(v) == pal::caption ? pal::edge : StateColor(v));
	GdiFlush();
	p.Dot(pad + dot, h / 2, dot, StateColor(v));
	Parts(p, pad + dot * 2 + gap, 0, h, parts, gap);
	if (showGear) {
		const int gx = w - pad - gear;
		p.Gear(gx, (h - gear) / 2, gear, v.menuOpen ? pal::text : pal::caption);
		hits.push_back({{gx - m.S(4), 0, w, h}, kHitGear, 0});
	}
}

}

int GraphColumns(const View &v)
{
	const float s = v.s;
	const int inner = static_cast<int>(std::lround((kFullW - kPad * 2) * s));
	return std::max(10, inner / std::max(1, static_cast<int>(std::lround(2 * s))));
}

void PaintPanel(ui::Canvas &cv, const View &v, std::vector<Hit> &hits)
{
	hits.clear();
	static HDC measure = CreateCompatibleDC(nullptr);
	if (v.set.compact) {
		PaintCompact(cv, v, hits, measure);
		return;
	}
	const Fonts f(v.s);
	Pen m = {measure, v.s, &cv};
	const int pad = m.S(kPad), w = m.S(kFullW), inner = w - pad * 2, rowH = m.S(20), labelW = m.S(kLabelW), gap = m.S(8);
	const bool perfOn = v.set.perf;
	const bool netOn = v.set.net && (!v.peers.empty() || v.turnAvgMs >= 0 || v.lagMs >= 0);
	const int headH = m.S(28), tilesH = m.S(48), graphH = m.S(62), sumH = m.S(20), sparkH = m.S(36), toastH = m.S(18), tableH = m.S(16);
	int h = headH;
	if (perfOn) {
		h += tilesH;
		if (v.set.graphFrame) h += graphH + m.S(4);
		h += sumH;
		h += rowH * ((v.set.showCpu ? 1 : 0) + (v.set.showMemory ? 1 : 0) + (v.set.showSim ? 1 : 0));
	}
	if (netOn) {
		if (perfOn) h += rowH + m.S(4);
		if (!v.peers.empty()) h += tableH + rowH * static_cast<int>(v.peers.size());
		if (v.lagMs >= 0) h += rowH;
		else if (v.turnAvgMs >= 0) h += rowH * 2;
		if (v.set.graphPing && !v.pingSparks.empty()) h += sparkH + m.S(6);
	}
	if (!v.toast.empty()) h += toastH;
	h += pad - m.S(4);
	if (!cv.Ensure(w, h)) return;
	Pen p = {cv.dc, v.s, &cv};
	p.Fill(0, 0, w, h, kBack);
	p.Fill(0, 0, w, m.S(2), pal::red);
	p.Fill(0, h - 1, w, 1, pal::edge);
	p.Fill(0, 0, 1, h, pal::edge);
	p.Fill(w - 1, 0, 1, h, pal::edge);
	// The header: the title, the state of the screen and the gear.
	int y = m.S(2);
	p.Text(pad, y, headH - m.S(2), Caps(perfOn ? TrW(L"Performance") : TrW(L"Network")), f.title, pal::text);
	{
		int right = w - pad;
		if (v.hover || v.menuOpen) {
			const int gear = m.S(14);
			p.Gear(right - gear, y + (headH - m.S(2) - gear) / 2, gear, v.menuOpen ? pal::text : pal::caption);
			hits.push_back({{right - gear - m.S(4), 0, w, headH}, kHitGear, 0});
			right -= gear + m.S(10);
		}
		const std::wstring state = v.recordingLeft >= 0 ? StateText(v) : Caps(StateText(v));
		const int tw = p.Width(state, f.caption);
		p.Text(right, y, headH - m.S(2), state, f.caption, v.recordingLeft >= 0 ? pal::redText : pal::textDim, DT_RIGHT);
		p.Dot(right - tw - m.S(8), y + (headH - m.S(2)) / 2, m.S(3), StateColor(v));
	}
	y = headH;
	if (perfOn) {
		if (!v.drawing && v.frames.frames == 0) {
			// Nothing to measure: the game is on a still screen.
			p.Text(pad, y + m.S(1), m.S(32), Caps(TrW(L"idle")), f.big, pal::caption);
			p.Text(pad, y + m.S(32), m.S(12), TrW(L"the game only draws when something changes"), f.caption, pal::caption);
		}
		else {
			// The big numbers: this second's FPS and frame time, the window's lows.
			struct Tile {
				std::wstring value, caption;
				COLORREF color;
			};
			std::vector<Tile> tiles;
			const bool numbers = v.drawing && v.frames.nowFps > 0;
			const bool lows = v.frames.frames >= 100;
			tiles.push_back({numbers ? Dec(v.frames.nowFps, 0) : L"–", L"FPS", numbers ? FpsColor(v.frames.nowFps) : pal::disabled});
			tiles.push_back({numbers ? Dec(v.frames.nowMs, 1) : L"–", L"ms", numbers ? pal::text : pal::disabled});
			if (v.set.showLows) {
				tiles.push_back({lows ? Dec(v.frames.low1Fps, 0) : L"–", TrW(L"1% low"), lows ? FpsColor(v.frames.low1Fps) : pal::disabled});
				tiles.push_back({lows ? Dec(v.frames.low01Fps, 0) : L"–", TrW(L"0.1% low"), lows ? FpsColor(v.frames.low01Fps) : pal::disabled});
			}
			const int tileW = inner / static_cast<int>(tiles.size());
			for (size_t i = 0; i < tiles.size(); i++) {
				const int tx = pad + static_cast<int>(i) * tileW;
				if (i) p.Fill(tx - m.S(6), y + m.S(8), 1, tilesH - m.S(14), pal::hairline);
				p.Text(tx, y + m.S(1), m.S(32), tiles[i].value, f.big, tiles[i].color);
				p.Text(tx, y + m.S(32), m.S(12), Caps(tiles[i].caption), f.caption, pal::caption);
			}
		}
		y += tilesH;
		if (v.set.graphFrame) {
			FrameGraph(p, f, v, pad, y, inner, graphH);
			y += graphH + m.S(4);
		}
		{
			std::vector<Part> parts;
			if (v.frames.frames > 0) {
				parts.push_back({v.frames.hitches ? std::to_wstring(v.frames.hitches) + L" " + TrW(L"hitches") : TrW(L"no hitches"),
					v.frames.hitches ? pal::redText : pal::ok, f.value});
				if (v.hitchLineMs > 0) parts.push_back({L"> " + Dec(v.hitchLineMs, 0) + L" ms", pal::caption, f.caption});
				parts.push_back({TrW(L"worst ") + Dec(v.frames.maxMs, 0) + L" ms", pal::textDim, f.text});
				parts.push_back({TrW(L"avg ") + Dec(v.frames.avgFps, 0) + L" FPS", pal::textDim, f.text});
			}
			Parts(p, pad, y, sumH, parts, gap);
			p.Text(w - pad, y, sumH, std::to_wstring(v.set.window) + L" s", f.caption, pal::caption, DT_RIGHT);
			y += sumH;
		}
		const int sparkW = m.S(kSparkW), sparkX = w - pad - sparkW, sparkY = m.S(4), sparkHh = rowH - m.S(8);
		auto label = [&](const std::wstring &text) { p.Text(pad, y, rowH, Caps(text), f.caption, pal::caption); };
		if (v.set.showCpu) {
			label(L"CPU");
			std::vector<Part> parts;
			if (v.cpuThread >= 0) {
				parts.push_back({TrW(L"game ") + Dec(v.cpuThread, 0) + L" %", LoadColor(v.cpuThread), f.value});
				parts.push_back({TrW(L"process ") + Dec(v.cpuProcess, 0) + L" %", pal::textDim, f.text});
			}
			else {
				parts.push_back({L"–", pal::disabled, f.value});
			}
			Parts(p, pad + labelW, y, rowH, parts, gap);
			if (v.set.graphCpu) DrawSpark(p, sparkX, y + sparkY, sparkW, sparkHh, v.cpuSpark, 100, v.set.window);
			y += rowH;
		}
		if (v.set.showMemory) {
			label(TrW(L"Memory"));
			const double share = v.addrTotalGB > 0 ? v.addrUsedGB / v.addrTotalGB : 0;
			std::vector<Part> parts = {{Dec(v.addrUsedGB, 1) + L" / " + Dec(v.addrTotalGB, 1) + L" GB", MemColor(share), f.value},
				{TrW(L"private ") + Dec(v.privateGB, 1) + L" GB", pal::textDim, f.text}};
			Parts(p, pad + labelW, y, rowH, parts, gap);
			if (v.set.graphMemory) DrawSpark(p, sparkX, y + sparkY, sparkW, sparkHh, v.memSpark, static_cast<float>(v.addrTotalGB), v.set.window);
			else Meter(p, sparkX, y + rowH / 2 - m.S(2), sparkW, m.S(4), share, MemColor(share));
			y += rowH;
		}
		if (v.set.showSim) {
			label(TrW(L"Simulation"));
			std::vector<Part> parts;
			if (v.simSpeed >= 0) {
				parts.push_back({L"×" + Dec(v.simSpeed, 1), pal::text, f.value});
				if (v.turnsPerSec >= 0) parts.push_back({Dec(v.turnsPerSec, 1) + L" " + TrW(L"turns/s"), pal::textDim, f.text});
				if (v.updateMs >= 0) parts.push_back({TrW(L"update ") + Dec(v.updateMs, 1) + L" ms", pal::textDim, f.text});
			}
			else {
				parts.push_back({TrW(L"out of a match"), pal::caption, f.text});
			}
			Parts(p, pad + labelW, y, rowH, parts, gap);
			y += rowH;
		}
	}
	if (netOn) {
		if (perfOn) {
			y += m.S(4);
			p.Fill(pad, y, inner, 1, pal::hairline);
			p.Text(pad, y, rowH, Caps(TrW(L"Network")), f.caption, pal::redText);
			y += rowH;
		}
		// The players as a table: colour and name, ping, route, loss and traffic.
		if (!v.peers.empty()) {
			const int swatch = m.S(9), nameX = pad + swatch + m.S(6);
			const int cols[] = {nameX, pad + m.S(126), pad + m.S(178), pad + m.S(228), pad + m.S(290)};
			const std::wstring heads[] = {TrW(L"Player"), L"Ping", TrW(L"Route"), TrW(L"Loss"), L"KB/s"};
			for (int k = 0; k < 5; k++) p.Text(cols[k], y, tableH, Caps(heads[k]), f.caption, pal::caption);
			y += tableH;
			for (const PeerLine &peer : v.peers) {
				p.Fill(pad, y + (rowH - swatch) / 2, swatch, swatch, peer.color);
				p.TextFit(cols[0], y, rowH, peer.name, f.text, pal::text, cols[1] - cols[0] - m.S(6));
				if (!peer.connected) {
					p.Text(cols[1], y, rowH, TrW(L"connecting…"), f.text, pal::warn);
				}
				else {
					p.Text(cols[1], y, rowH, Ms(peer.ping), f.value, PingColor(peer.ping));
					p.Text(cols[2], y, rowH, peer.relay ? L"relay" : TrW(L"direct"), f.text, pal::textDim);
					if (peer.lossPct >= 0) {
						p.Text(cols[3], y, rowH, Dec(peer.lossPct, peer.lossPct < 10 ? 1 : 0) + L" %", f.text,
							peer.lossPct < 2 ? pal::textDim : peer.lossPct < 8 ? pal::warn : pal::bad);
					}
					p.Text(cols[4], y, rowH, L"↓" + Dec(peer.inKBs, 1) + L" ↑" + Dec(peer.outKBs, 1), f.caption, pal::caption);
				}
				y += rowH;
			}
		}
		if (v.lagMs >= 0) {
			p.Text(pad, y, rowH, Caps(TrW(L"Delay")), f.caption, pal::caption);
			std::vector<Part> parts = {{Dec(v.lagMs / 1000.0, 1) + L" s", LagColor(v.lagMs), f.value}, {TrW(L"behind live"), pal::textDim, f.text}};
			if (v.delaySec > 0) parts.push_back({TrW(L"plus ") + std::to_wstring(v.delaySec) + L" s " + TrW(L"set by the host"), pal::textDim, f.text});
			Parts(p, pad + labelW, y, rowH, parts, gap);
			y += rowH;
		}
		else if (v.turnAvgMs >= 0) {
			p.Text(pad, y, rowH, Caps(TrW(L"Turn")), f.caption, pal::caption);
			std::vector<Part> parts = {{Ms(v.turnAvgMs), TurnColor(v.turnAvgMs), f.value}, {TrW(L"max ") + Ms(v.turnMaxMs), pal::textDim, f.text}};
			if (v.turnSizeMs >= 0) parts.push_back({TrW(L"size ") + Ms(v.turnSizeMs), pal::textDim, f.text});
			Parts(p, pad + labelW, y, rowH, parts, gap);
			y += rowH;
			p.Text(pad, y, rowH, Caps(TrW(L"Wait")), f.caption, pal::caption);
			parts = {{Ms(v.turnWaitMs), v.turnWaitMs > 150 ? pal::warn : pal::text, f.value}};
			if (v.turnLateMs >= 0) parts.push_back({TrW(L"data late ") + Ms(v.turnLateMs), v.turnLateMs > 250 ? pal::warn : pal::textDim, f.text});
			parts.push_back({std::to_wstring(v.resends) + L" " + TrW(L"resends"), v.resends ? pal::warn : pal::textDim, f.text});
			Parts(p, pad + labelW, y, rowH, parts, gap);
			y += rowH;
		}
		if (v.set.graphPing && !v.pingSparks.empty()) {
			y += m.S(2);
			float top = 150;
			for (const overlay::Spark &spark : v.pingSparks) {
				for (float value : spark.values) top = std::max(top, value * 1.15f);
			}
			top = NiceTop(top, 150);
			p.Fill(pad, y, inner, sparkH, kGraphBack);
			for (float guide : {100.0f, 200.0f}) {
				if (guide < top) p.Dashed(pad, y + sparkH - 1 - static_cast<int>(guide / top * (sparkH - 2)), inner, pal::lineStrong, p.S(3), p.S(3));
			}
			for (const overlay::Spark &spark : v.pingSparks) DrawLine(p, pad, y, inner, sparkH, spark, top, v.set.window);
			Label(p, w - pad - m.S(3), y + m.S(1), Dec(top, 0) + L" ms", f.caption, DT_RIGHT);
			y += sparkH + m.S(4);
		}
	}
	if (!v.toast.empty()) {
		p.TextFit(pad, y, toastH, v.toast, f.caption, pal::textDim, inner);
		y += toastH;
	}
}

void PaintMenu(ui::Canvas &cv, const View &v, std::vector<Hit> &hits)
{
	hits.clear();
	const Fonts f(v.s);
	static HDC measure = CreateCompatibleDC(nullptr);
	Pen m = {measure, v.s, &cv};
	const int pad = m.S(10), w = m.S(360), rowH = m.S(26), labelW = m.S(74), chipH = m.S(19), chipGap = m.S(4), chipPad = m.S(6);
	struct Chip {
		std::wstring text;
		int kind, arg;
		bool on;
		int corner = -1;
	};
	struct MenuRow {
		std::wstring label;
		std::vector<Chip> chips;
	};
	const Settings &s = v.set;
	std::vector<MenuRow> rows;
	rows.push_back({TrW(L"Panel view"), {{TrW(L"Compact view"), kHitMode, 1, s.compact}, {TrW(L"Full view"), kHitMode, 0, !s.compact}}});
	rows.push_back({TrW(L"Show"), {{TrW(L"Performance"), kHitSection, 0, s.perf}, {TrW(L"Network"), kHitSection, 1, s.net}}});
	rows.push_back({TrW(L"Rows"), {{TrW(L"Lows"), kHitData, 0, s.showLows}, {L"CPU", kHitData, 1, s.showCpu}, {TrW(L"Memory"), kHitData, 2, s.showMemory},
		{TrW(L"Simulation"), kHitData, 3, s.showSim}}});
	rows.push_back({TrW(L"Graphs"), {{TrW(L"Frame time"), kHitGraph, 0, s.graphFrame}, {L"CPU", kHitGraph, 1, s.graphCpu}, {TrW(L"Memory"), kHitGraph, 2, s.graphMemory},
		{L"Ping", kHitGraph, 3, s.graphPing}}});
	{
		MenuRow r = {TrW(L"Time window"), {}};
		for (int i = 0; i < 3; i++) r.chips.push_back({std::to_wstring(kWindows[i]) + L" s", kHitWindow, i, s.window == kWindows[i]});
		rows.push_back(r);
	}
	{
		MenuRow r = {TrW(L"Corner"), {}};
		for (int i = 0; i < 4; i++) r.chips.push_back({L"", kHitCorner, i, s.corner == i, i});
		rows.push_back(r);
	}
	{
		MenuRow r = {TrW(L"Size"), {}};
		for (int i = 0; i < 4; i++) r.chips.push_back({std::to_wstring(kScales[i]) + L" %", kHitScale, i, s.scale == kScales[i]});
		rows.push_back(r);
	}
	{
		MenuRow r = {TrW(L"Opacity"), {}};
		for (int i = 0; i < 4; i++) r.chips.push_back({std::to_wstring(kOpacities[i]) + L" %", kHitOpacity, i, s.opacity == kOpacities[i]});
		rows.push_back(r);
	}
	{
		MenuRow r = {TrW(L"Refresh rate"), {}};
		for (int i = 0; i < 3; i++) r.chips.push_back({Dec(kRefreshes[i] / 1000.0, kRefreshes[i] % 1000 ? (kRefreshes[i] % 100 ? 2 : 1) : 0) + L" s", kHitRefresh, i, s.refresh == kRefreshes[i]});
		rows.push_back(r);
	}
	const int headH = m.S(30), buttonH = m.S(40);
	const int h = headH + rowH * static_cast<int>(rows.size()) + buttonH + m.S(4);
	if (!cv.Ensure(w, h)) return;
	Pen p = {cv.dc, v.s, &cv};
	p.Fill(0, 0, w, h, pal::panel);
	p.Fill(0, 0, w, m.S(2), pal::red);
	p.Fill(0, h - 1, w, 1, pal::edge);
	p.Fill(0, 0, 1, h, pal::edge);
	p.Fill(w - 1, 0, 1, h, pal::edge);
	GdiFlush();
	hits.push_back({{0, 0, w, h}, kHitMenu, 0});
	p.Text(pad, m.S(2), headH - m.S(2), Caps(TrW(L"Performance panel")), f.title, pal::text);
	auto hot = [&](const RECT &r) { return v.menuMouseX >= r.left && v.menuMouseX < r.right && v.menuMouseY >= r.top && v.menuMouseY < r.bottom; };
	int y = headH;
	for (const MenuRow &r : rows) {
		p.Text(pad, y, rowH, Caps(r.label), f.caption, pal::caption);
		int x = pad + labelW;
		for (const Chip &c : r.chips) {
			const int cw = c.corner >= 0 ? m.S(30) : p.Width(c.text, f.text) + chipPad * 2;
			const RECT rc = {x, y + (rowH - chipH) / 2, x + cw, y + (rowH - chipH) / 2 + chipH};
			const bool over = hot(rc);
			p.Fill(rc.left, rc.top, cw, chipH, c.on ? (over ? pal::redPressed : pal::red) : over ? pal::buttonHover : pal::button);
			if (c.corner >= 0) {
				const int sw = m.S(16), sh = m.S(11), sx = x + (cw - sw) / 2, sy = rc.top + (chipH - sh) / 2;
				const COLORREF ink = c.on ? pal::onRed : pal::textDim;
				p.Fill(sx, sy, sw, 1, ink);
				p.Fill(sx, sy + sh - 1, sw, 1, ink);
				p.Fill(sx, sy, 1, sh, ink);
				p.Fill(sx + sw - 1, sy, 1, sh, ink);
				const int bw = m.S(6), bh = m.S(4);
				p.Fill(c.corner % 2 ? sx + sw - 2 - bw : sx + 2, c.corner >= 2 ? sy + sh - 2 - bh : sy + 2, bw, bh, ink);
			}
			else {
				p.Text(x + cw / 2, rc.top, chipH, c.text, f.text, c.on ? pal::onRed : pal::body, DT_CENTER);
			}
			hits.push_back({rc, c.kind, c.arg});
			x += cw + chipGap;
		}
		y += rowH;
	}
	p.Fill(pad, y + m.S(4), w - pad * 2, 1, pal::hairline);
	{
		const bool recording = v.recordingLeft >= 0;
		const std::wstring label = recording ? TrW(L"Stop recording") : TrW(L"Record frame times") + L" · " + std::to_wstring(s.captureSeconds) + L" s";
		const int bw = p.Width(label, f.value) + m.S(30);
		const RECT rc = {pad, y + m.S(12), pad + bw, y + m.S(12) + m.S(22)};
		const bool over = hot(rc);
		p.Fill(rc.left, rc.top, bw, rc.bottom - rc.top, over ? pal::buttonHover : pal::button);
		p.Dot(rc.left + m.S(11), (rc.top + rc.bottom) / 2, m.S(4), pal::red);
		p.Text(rc.left + m.S(20), rc.top, rc.bottom - rc.top, label, f.value, pal::text);
		hits.push_back({rc, kHitCapture, 0});
		if (!v.captureKey.empty() && rc.right + m.S(8) + p.Width(v.captureKey, f.caption) <= w - pad) {
			p.Text(w - pad, rc.top, rc.bottom - rc.top, v.captureKey, f.caption, pal::caption, DT_RIGHT);
		}
	}
}

void Baked::Bake(const ui::Canvas &cv, int opacity)
{
	GdiFlush();
	w = cv.w;
	h = cv.h;
	const size_t n = static_cast<size_t>(w) * h;
	pre.resize(n);
	inv.resize(n);
	const int aBack = std::max(30, std::min(100, opacity)) * 255 / 100;
	const int aFront = std::min(255, aBack + 110);
	const uint32_t back = ui::PixelOf(kBack), graph = ui::PixelOf(kGraphBack), idle = ui::PixelOf(kIdleBand), hatch = ui::PixelOf(kHatch);
	for (size_t i = 0; i < n; i++) {
		const uint32_t src = cv.bits[i] & 0xffffff;
		const uint32_t a = src == back || src == graph || src == idle || src == hatch ? aBack : aFront;
		const uint32_t r = ((src >> 16) & 255) * a / 255, g = ((src >> 8) & 255) * a / 255, b = (src & 255) * a / 255;
		pre[i] = (r << 16) | (g << 8) | b;
		const uint32_t k = 255 - a;
		inv[i] = k * 0x01010101u;
	}
}

void Baked::Blend(uint32_t *buf, int bw, int bh, int ox, int oy) const
{
	if (!buf || w <= 0 || h <= 0) return;
	const int x0 = std::max(0, -ox), x1 = std::min(w, bw - ox);
	if (x1 <= x0) return;
	const __m128i zero = _mm_setzero_si128(), opaque = _mm_set1_epi32(static_cast<int>(0xff000000u));
	for (int y = std::max(0, -oy); y < std::min(h, bh - oy); y++) {
		uint32_t *dst = buf + static_cast<size_t>(oy + y) * bw + ox;
		const uint32_t *p = pre.data() + static_cast<size_t>(y) * w, *k = inv.data() + static_cast<size_t>(y) * w;
		int x = x0;
		for (; x + 4 <= x1; x += 4) {
			const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + x));
			const __m128i i = _mm_loadu_si128(reinterpret_cast<const __m128i *>(k + x));
			const __m128i lo = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(d, zero), _mm_unpacklo_epi8(i, zero)), 8);
			const __m128i hi = _mm_srli_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(d, zero), _mm_unpackhi_epi8(i, zero)), 8);
			const __m128i out = _mm_adds_epu8(_mm_packus_epi16(lo, hi), _mm_loadu_si128(reinterpret_cast<const __m128i *>(p + x)));
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), _mm_or_si128(out, opaque));
		}
		for (; x < x1; x++) {
			const uint32_t d = dst[x], kk = k[x] & 255;
			const uint32_t rb = (((d & 0xff00ff) * kk) >> 8) & 0xff00ff, g = (((d & 0x00ff00) * kk) >> 8) & 0x00ff00;
			dst[x] = 0xff000000u | ((rb | g) + p[x]);
		}
	}
}

}
