// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "net/spectator.h"
#include "net/nettune.h"
#include "game/mapfog.h"
#include "game/quickicons.h"
#include "game/noteicons.h"
#include "game/zoomview.h"
#include "core/crashlog.h"
#include "app/overlay.h"
#include "i18n/tr.h"

#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "game/controls.h"
#include "core/log.h"
#include "net/steamnet.h"
#include "ui/uikit.h"
#include "addr/state.h"
#include "addr/world.h"

namespace {

using ui::Canvas;
using ui::kBad;
using ui::kDim;
using ui::kGold;
using ui::kOk;
using ui::kText;

const COLORREF kWarn = RGB(230, 190, 90);
const COLORREF kPanel = RGB(22, 17, 11);
const COLORREF kGraphBg = RGB(12, 9, 6);
const COLORREF kGrid = RGB(52, 41, 27);
const COLORREF kBorder = RGB(150, 116, 60);
const int kHist = 120;
const int kStutterMs = 150;
const int kStutterWindow = 30;

struct PeerSample {
	uint64_t id;
	int ping;
	float loss;
};
struct Sample {
	int fps = 0;
	bool playing = false;
	int load = -1;
	int worstMs = 0;
	int stutters = 0;
	int turnAvg = -1, turnMax = 0;
	int lagMs = -1;
	int delaySec = 0;
	double memUsed = 0, memTotal = 0;
	std::vector<PeerSample> peers;
};
std::deque<Sample> g_hist;

std::mutex g_frameMx;
LARGE_INTEGER g_qpf = {}, g_lastFrame = {};
int g_secWorstMs = 0, g_secStutters = 0;
bool g_secPlaying = false;

typedef int(__fastcall *GameUpdate_t)(void *game);
GameUpdate_t g_origGameUpdate = nullptr;
std::atomic<long long> g_updateTicks{0};
LARGE_INTEGER g_loadSince = {};
int g_updateDepth = 0;
const int kUpdateMaxDepth = 8;
struct UpdateCall {
	LARGE_INTEGER start;
	bool nested;
} g_updateStack[kUpdateMaxDepth];
std::atomic<int> g_updateLeafCalls{0};
int g_updateMaxDepthSeen = 0;
int g_loadLogged = 0;
int g_framesInUpdate = 0, g_framesTotal = 0;

std::mutex g_uiMx;
RECT g_panel = {};
DWORD g_panelTick = 0;
int g_frameW = 0, g_frameH = 0;
float g_scale = 1;
bool g_dragging = false, g_dragMoved = false;
int g_dragDX = 0, g_dragDY = 0, g_dragX = 0, g_dragY = 0;
bool g_hover = false;
bool g_dirty = true;
bool g_moved = false;
DWORD g_lastDown = 0;
POINT g_lastDownPt = {};
int g_mouseX = -1, g_mouseY = -1;
bool g_restorePerf = true, g_restoreNet = true;

Canvas g_canvas;
HFONT g_font = nullptr, g_bold = nullptr, g_small = nullptr;
float g_fontScale = 0;
DWORD g_built = 0;
float g_builtScale = 0;
int g_builtFor[5] = {};
bool g_empty = true;

void EnsureFonts(float s)
{
	if (g_font && g_fontScale == s) return;
	for (HFONT f : {g_font, g_bold, g_small}) {
		if (f) DeleteObject(f);
	}
	auto make = [s](int size, int weight) {
		return CreateFontW(-static_cast<int>(size * s + 0.5f), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	};
	g_font = make(13, FW_NORMAL);
	g_bold = make(13, FW_SEMIBOLD);
	g_small = make(11, FW_NORMAL);
	g_fontScale = s;
}

std::wstring Wide(const std::string &s)
{
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring w(n, L'\0');
	if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
	return w;
}

std::wstring Fmt(const wchar_t *fmt, double v)
{
	wchar_t buf[64];
	swprintf_s(buf, fmt, v);
	for (wchar_t *p = buf; *p; p++) {
		if (*p == L'.') *p = L',';
	}
	return buf;
}

bool PlayerColorOf(int number, COLORREF &out)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
	if (!game || !gs || number <= 0) return false;
	for (int i = 0; i < 8; i++) {
		const uint32_t ip = *reinterpret_cast<uint32_t *>(gs + 0xdc8 + i * 4);
		const bool match = ip == (0x7f4d0000u | static_cast<uint32_t>(number)) || ip == ((static_cast<uint32_t>(number) << 24) | 0x00004d7fu);
		if (!match) continue;
		const uint32_t c = *reinterpret_cast<uint32_t *>(game + 0x12cc + i * 800 + 0x314);
		out = RGB(((c >> 10) & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, (c & 31) * 255 / 31);
		return true;
	}
	return false;
}

COLORREF PeerColor(const NetPeerStat &n, size_t index)
{
	COLORREF c;
	if (PlayerColorOf(n.number, c)) return c;
	static const COLORREF kPalette[] = {RGB(110, 170, 255), RGB(255, 140, 90), RGB(150, 220, 120), RGB(230, 120, 220),
		RGB(240, 220, 100), RGB(110, 220, 220), RGB(255, 110, 120)};
	return kPalette[index % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

COLORREF FpsColor(int fps) { return fps >= 50 ? kOk : fps >= 25 ? kWarn : kBad; }
COLORREF MemColor(double pct) { return pct < 0.7 ? kOk : pct < 0.85 ? kWarn : kBad; }
COLORREF PingColor(int ping) { return ping < 120 ? kOk : ping < 250 ? kWarn : kBad; }
COLORREF TurnColor(int ms) { return ms < 170 ? kOk : ms < 320 ? kWarn : kBad; }
COLORREF LagColor(int ms) { return ms < 3000 ? kOk : ms < 6000 ? kWarn : kBad; }
COLORREF LoadColor(int load) { return load < 70 ? kOk : load < 90 ? kWarn : kBad; }

struct Cell {
	std::wstring text;
	COLORREF color = kText;
	bool bold = false;
};
struct Row {
	enum Kind { kLine, kTitle, kPeer, kGraphFps, kGraphCpu, kGraphMem, kGraphPing, kGraphTurn } kind = kLine;
	std::vector<Cell> cells;
	COLORREF swatch = 0;
	bool hasSwatch = false;
};

int RecentStutters()
{
	int n = 0, k = 0;
	for (auto it = g_hist.rbegin(); it != g_hist.rend() && k < kStutterWindow; ++it, ++k) n += it->stutters;
	return n;
}

std::vector<Row> BuildRows(bool perf, bool net, bool compact, const std::vector<NetPeerStat> &stats, bool hover)
{
	std::vector<Row> rows;
	const Sample last = g_hist.empty() ? Sample() : g_hist.back();
	const double memPct = last.memTotal > 0 ? last.memUsed / last.memTotal : 0;
	const int stutters = RecentStutters();
	if (compact) {
		Row r;
		r.cells.push_back({L"⋮", kDim});
		if (perf) {
			r.cells.push_back({last.playing ? std::to_wstring(last.fps) + L" FPS" : L"— FPS", last.playing ? FpsColor(last.fps) : kDim, true});
			if (last.load >= 0) r.cells.push_back({L"CPU " + std::to_wstring(last.load) + L" %", LoadColor(last.load), true});
			r.cells.push_back({Fmt(L"%.1f GB", last.memUsed), MemColor(memPct), true});
			if (stutters) r.cells.push_back({TrW(L"stutters ") + std::to_wstring(stutters), kBad, true});
		}
		if (net) {
			int worst = -1;
			for (const NetPeerStat &n : stats) {
				if (n.connected) worst = (std::max)(worst, n.ping);
			}
			if (worst >= 0) r.cells.push_back({std::to_wstring(worst) + L" ms", PingColor(worst), true});
			if (last.turnAvg >= 0) r.cells.push_back({TrW(L"turn ") + std::to_wstring(last.turnAvg) + L" ms", TurnColor(last.turnAvg), true});
			if (last.lagMs >= 0) r.cells.push_back({TrW(L"delay ") + Fmt(L"%.1f s", last.lagMs / 1000.0), LagColor(last.lagMs), true});
		}
		rows.push_back(r);
		return rows;
	}
	{
		Row t;
		t.kind = Row::kTitle;
		t.cells.push_back({perf ? TrW(L"Performance") : TrW(L"Network"), kGold, true});
		rows.push_back(t);
	}
	if (perf) {
		int minFps = 0, maxFps = 0, played = 0, sum = 0;
		for (const Sample &s : g_hist) {
			if (!s.playing) continue;
			minFps = played ? (std::min)(minFps, s.fps) : s.fps;
			maxFps = (std::max)(maxFps, s.fps);
			sum += s.fps;
			played++;
		}
		auto num = [&](int v) { return played ? std::to_wstring(v) : std::wstring(L"—"); };
		Row r;
		r.cells.push_back({L"FPS", kDim});
		if (last.playing) r.cells.push_back({std::to_wstring(last.fps), FpsColor(last.fps), true});
		else r.cells.push_back({L"—", kDim, true});
		r.cells.push_back({TrW(L"min ") + num(minFps), kDim});
		r.cells.push_back({TrW(L"avg ") + num(played ? (sum + played / 2) / played : 0), kDim});
		r.cells.push_back({TrW(L"max ") + num(maxFps), kDim});
		if (stutters) r.cells.push_back({TrW(L"stutters ") + std::to_wstring(stutters), kBad, true});
		rows.push_back(r);
		Row g;
		g.kind = Row::kGraphFps;
		rows.push_back(g);
		int minLoad = 0, maxLoad = 0, sumLoad = 0, loads = 0;
		for (const Sample &s : g_hist) {
			if (s.load < 0) continue;
			minLoad = loads ? (std::min)(minLoad, s.load) : s.load;
			maxLoad = (std::max)(maxLoad, s.load);
			sumLoad += s.load;
			loads++;
		}
		auto pct = [&](int v) { return loads ? std::to_wstring(v) + L" %" : std::wstring(L"—"); };
		Row l;
		l.cells.push_back({L"CPU", kDim});
		l.cells.push_back({last.load >= 0 ? std::to_wstring(last.load) + L" %" : L"—", last.load >= 0 ? LoadColor(last.load) : kDim, true});
		l.cells.push_back({TrW(L"min ") + pct(minLoad), kDim});
		l.cells.push_back({TrW(L"avg ") + pct(loads ? (sumLoad + loads / 2) / loads : 0), kDim});
		l.cells.push_back({TrW(L"max ") + pct(maxLoad), kDim});
		rows.push_back(l);
		Row gc;
		gc.kind = Row::kGraphCpu;
		rows.push_back(gc);
		Row m;
		m.cells.push_back({TrW(L"Memory"), kDim});
		m.cells.push_back({Fmt(L"%.1f", last.memUsed) + L" / " + Fmt(L"%.1f GB", last.memTotal), MemColor(memPct), true});
		if (memPct >= 0.85) m.cells.push_back({TrW(L"near the limit!"), kBad, true});
		rows.push_back(m);
		Row gm;
		gm.kind = Row::kGraphMem;
		rows.push_back(gm);
	}
	if (net && !stats.empty()) {
		if (perf) {
			Row h;
			h.kind = Row::kTitle;
			h.cells.push_back({TrW(L"Network"), kGold, true});
			rows.push_back(h);
		}
		for (size_t i = 0; i < stats.size(); i++) {
			const NetPeerStat &n = stats[i];
			Row r;
			r.kind = Row::kPeer;
			r.swatch = PeerColor(n, i);
			r.hasSwatch = true;
			std::wstring name = Wide(n.name);
			if (name.size() > 16) name = name.substr(0, 15) + L"…";
			r.cells.push_back({name, kText});
			if (!n.connected) {
				r.cells.push_back({TrW(L"connecting…"), kWarn});
			}
			else {
				r.cells.push_back({std::to_wstring(n.ping) + L" ms", PingColor(n.ping), true});
				r.cells.push_back({n.relay ? L"relay" : TrW(L"direct"), kDim});
				if (n.quality >= 0) {
					const double loss = (1.0 - n.quality) * 100.0;
					r.cells.push_back({TrW(L"loss ") + Fmt(loss < 10 ? L"%.1f%%" : L"%.0f%%", loss), loss < 2 ? kOk : loss < 8 ? kWarn : kBad});
				}
				else {
					r.cells.push_back({L"", kDim});
				}
				r.cells.push_back({L"↓" + Fmt(L"%.1f", n.inKBs) + L"  ↑" + Fmt(L"%.1f KB/s", n.outKBs), kDim});
			}
			rows.push_back(r);
		}
		Row g;
		g.kind = Row::kGraphPing;
		rows.push_back(g);
	}
	if (net) {
		int shown = -1, shownMax = 0;
		for (auto it = g_hist.rbegin(); it != g_hist.rend(); ++it) {
			if (it->turnAvg < 0) continue;
			shown = it->turnAvg;
			shownMax = it->turnMax;
			break;
		}
		if (last.lagMs >= 0) {
			Row t;
			t.cells.push_back({TrW(L"Delay"), kDim});
			t.cells.push_back({Fmt(L"%.1f s", last.lagMs / 1000.0), LagColor(last.lagMs), true});
			t.cells.push_back({TrW(L"behind live"), kDim});
			if (last.delaySec > 0) t.cells.push_back({TrW(L"plus " + std::to_wstring(last.delaySec) + L" s set by the host"), kDim});
			rows.push_back(t);
		}
		else if (shown >= 0) {
			Row t;
			t.cells.push_back({TrW(L"Turn"), kDim});
			t.cells.push_back({std::to_wstring(shown) + L" ms", TurnColor(shown), true});
			t.cells.push_back({TrW(L"max ") + std::to_wstring(shownMax), kDim});
			t.cells.push_back({TrW(L"order ≈ ") + std::to_wstring(shown * 3 / 2) + L" ms", kDim});
			rows.push_back(t);
			Row gt;
			gt.kind = Row::kGraphTurn;
			rows.push_back(gt);
		}
	}
	return rows;
}

void FillRectColor(HDC dc, int x, int y, int w, int h, COLORREF c)
{
	RECT r = {x, y, x + w, y + h};
	HBRUSH b = CreateSolidBrush(c);
	FillRect(dc, &r, b);
	DeleteObject(b);
}

struct Graph {
	HDC dc;
	int x, y, w, h;
	double top;
	int X(size_t i, size_t n) const { return x + w - 1 - static_cast<int>((n - 1 - i) * (w - 1) / (kHist - 1)); }
	int Y(double v) const { return y + h - 1 - static_cast<int>((std::min)(1.0, (std::max)(0.0, v / top)) * (h - 2)); }
	void Back(const std::vector<double> &guides, const std::wstring &label, float s) const
	{
		FillRectColor(dc, x, y, w, h, kGraphBg);
		for (double v : guides) FillRectColor(dc, x, Y(v), w, 1, kGrid);
		SelectObject(dc, g_small);
		SetTextColor(dc, kDim);
		RECT tr = {x, y, x + w - static_cast<int>(3 * s), y + h};
		DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &tr, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
	}
	void Line(const std::vector<POINT> &pts, COLORREF c, float s) const
	{
		if (pts.size() < 2) return;
		HPEN pen = CreatePen(PS_SOLID, (std::max)(1, static_cast<int>(1.5f * s + 0.5f)), c);
		HGDIOBJ old = SelectObject(dc, pen);
		Polyline(dc, pts.data(), static_cast<int>(pts.size()));
		SelectObject(dc, old);
		DeleteObject(pen);
	}
};

double NiceTop(double v, double step, double min)
{
	return (std::max)(min, std::ceil(v * 1.15 / step) * step);
}

void Render(const std::vector<Row> &rows, float s, bool compact, const std::map<uint64_t, COLORREF> &colors)
{
	EnsureFonts(s);
	const int pad = static_cast<int>(8 * s), gap = static_cast<int>(12 * s), lineH = static_cast<int>(19 * s);
	const int swatchW = static_cast<int>(10 * s), swatchGap = static_cast<int>(6 * s);
	const int graphW = static_cast<int>(300 * s);
	auto rowH = [&](const Row &r) {
		switch (r.kind) {
		case Row::kGraphFps:
		case Row::kGraphCpu:
		case Row::kGraphMem:
		case Row::kGraphPing:
		case Row::kGraphTurn: return static_cast<int>(44 * s);
		case Row::kTitle: return static_cast<int>(20 * s);
		default: return lineH;
		}
	};
	HDC dc = CreateCompatibleDC(nullptr);
	auto measure = [&](const Cell &c, HFONT f) {
		SelectObject(dc, f);
		SIZE sz = {};
		GetTextExtentPoint32W(dc, c.text.c_str(), static_cast<int>(c.text.size()), &sz);
		return static_cast<int>(sz.cx);
	};
	std::vector<int> cols;
	int lineW = 0;
	for (const Row &r : rows) {
		if (r.kind == Row::kPeer) {
			for (size_t k = 0; k < r.cells.size(); k++) {
				if (cols.size() <= k) cols.push_back(0);
				int w = measure(r.cells[k], r.cells[k].bold ? g_bold : g_font);
				if (k == 0) w += swatchW + swatchGap;
				cols[k] = (std::max)(cols[k], w);
			}
		}
		else if (r.kind == Row::kLine || r.kind == Row::kTitle) {
			int w = 0;
			for (size_t k = 0; k < r.cells.size(); k++) {
				w += measure(r.cells[k], r.kind == Row::kTitle && k > 0 ? g_small : r.cells[k].bold ? g_bold : g_font) + (k + 1 < r.cells.size() ? gap : 0);
			}
			if (r.kind != Row::kTitle) lineW = (std::max)(lineW, w);
		}
	}
	int netW = 0;
	for (size_t k = 0; k < cols.size(); k++) netW += cols[k] + (k + 1 < cols.size() ? gap : 0);
	const int width = (compact ? lineW : (std::max)((std::max)(lineW, netW), graphW)) + pad * 2;
	int height = pad * 2 - static_cast<int>(2 * s);
	for (const Row &r : rows) height += rowH(r) + (r.kind >= Row::kGraphFps ? static_cast<int>(4 * s) : 0);
	DeleteDC(dc);
	if (!g_canvas.Ensure(width, height)) return;
	HDC c = g_canvas.dc;
	FillRectColor(c, 0, 0, width, height, kPanel);
	{
		RECT all = {0, 0, width, height};
		HBRUSH border = CreateSolidBrush(kBorder);
		FrameRect(c, &all, border);
		DeleteObject(border);
	}
	int y = pad - static_cast<int>(1 * s);
	for (size_t i = 0; i < rows.size(); i++) {
		const Row &r = rows[i];
		const int h = rowH(r);
		const int inner = width - pad * 2;
		if (r.kind >= Row::kGraphFps) {
			Graph g = {c, pad, y + static_cast<int>(1 * s), inner, h - static_cast<int>(2 * s), 1};
			const size_t n = g_hist.size();
			if (r.kind == Row::kGraphFps) {
				int maxFps = 60;
				for (const Sample &sm : g_hist) maxFps = (std::max)(maxFps, sm.fps);
				g.top = NiceTop(maxFps, 30, 60);
				g.Back({30, 60}, std::to_wstring(static_cast<int>(g.top)), s);
				for (size_t k = 0; k < n; k++) {
					if (g_hist[k].stutters) FillRectColor(c, g.X(k, n), g.y, (std::max)(1, static_cast<int>(2 * s)), g.h, RGB(120, 34, 28));
				}
				std::vector<POINT> pts;
				for (size_t k = 0; k < n; k++) {
					if (g_hist[k].playing) {
						pts.push_back({g.X(k, n), g.Y(g_hist[k].fps)});
					}
					else {
						g.Line(pts, kOk, s);
						pts.clear();
					}
				}
				g.Line(pts, kOk, s);
			}
			else if (r.kind == Row::kGraphCpu) {
				g.top = 100;
				g.Back({50, 90}, L"100 %", s);
				std::vector<POINT> pts;
				for (size_t k = 0; k < n; k++) {
					if (g_hist[k].load >= 0) {
						pts.push_back({g.X(k, n), g.Y(g_hist[k].load)});
					}
					else {
						g.Line(pts, kOk, s);
						pts.clear();
					}
				}
				g.Line(pts, n && g_hist[n - 1].load >= 0 ? LoadColor(g_hist[n - 1].load) : kOk, s);
			}
			else if (r.kind == Row::kGraphMem) {
				double total = 0, maxUsed = 0;
				for (const Sample &sm : g_hist) {
					total = (std::max)(total, sm.memTotal);
					maxUsed = (std::max)(maxUsed, sm.memUsed);
				}
				const double limit = total * 0.85;
				g.top = NiceTop(maxUsed, 0.5, 1.0);
				if (total > 0) g.top = (std::min)(g.top, total);
				g.Back({g.top / 2}, Fmt(L"%.1f GB", g.top), s);
				if (total > 0 && limit <= g.top) FillRectColor(c, g.x, g.Y(limit), g.w, 1, RGB(140, 50, 40));
				std::vector<POINT> pts;
				for (size_t k = 0; k < n; k++) pts.push_back({g.X(k, n), g.Y(g_hist[k].memUsed)});
				const double pct = total > 0 && n ? g_hist[n - 1].memUsed / total : 0;
				g.Line(pts, MemColor(pct), s);
			}
			else if (r.kind == Row::kGraphTurn) {
				int maxTurn = 0;
				for (const Sample &sm : g_hist) maxTurn = (std::max)(maxTurn, sm.turnMax);
				g.top = NiceTop(maxTurn, 100, 300);
				g.Back({200}, std::to_wstring(static_cast<int>(g.top)) + L" ms", s);
				std::vector<POINT> hi, avg;
				for (size_t k = 0; k < n; k++) {
					if (g_hist[k].turnAvg >= 0) {
						hi.push_back({g.X(k, n), g.Y(g_hist[k].turnMax)});
						avg.push_back({g.X(k, n), g.Y(g_hist[k].turnAvg)});
					}
					else {
						g.Line(hi, RGB(120, 92, 54), s);
						g.Line(avg, kOk, s);
						hi.clear();
						avg.clear();
					}
				}
				g.Line(hi, RGB(120, 92, 54), s);
				int lastAvg = 0;
				for (size_t k = n; k > 0; k--) {
					if (g_hist[k - 1].turnAvg >= 0) {
						lastAvg = g_hist[k - 1].turnAvg;
						break;
					}
				}
				g.Line(avg, TurnColor(lastAvg), s);
			}
			else {
				int maxPing = 0;
				for (const Sample &sm : g_hist) {
					for (const PeerSample &p : sm.peers) maxPing = (std::max)(maxPing, p.ping);
				}
				g.top = NiceTop(maxPing, 50, 150);
				g.Back({100, 200}, std::to_wstring(static_cast<int>(g.top)) + L" ms", s);
				for (const auto &pc : colors) {
					std::vector<POINT> pts;
					for (size_t k = 0; k < n; k++) {
						for (const PeerSample &p : g_hist[k].peers) {
							if (p.id != pc.first) continue;
							if (p.ping >= 0) {
								pts.push_back({g.X(k, n), g.Y(p.ping)});
							}
							else {
								g.Line(pts, pc.second, s);
								pts.clear();
							}
							if (p.loss > 2) FillRectColor(c, g.X(k, n) - static_cast<int>(1 * s), g.y + g.h - static_cast<int>(4 * s),
												(std::max)(2, static_cast<int>(3 * s)), (std::max)(2, static_cast<int>(3 * s)), kBad);
						}
					}
					g.Line(pts, pc.second, s);
				}
			}
			y += h + static_cast<int>(4 * s);
			continue;
		}
		if (r.kind == Row::kTitle && i > 0) {
			FillRectColor(c, pad, y - static_cast<int>(2 * s), inner, 1, RGB(70, 54, 34));
		}
		int x = pad;
		for (size_t k = 0; k < r.cells.size(); k++) {
			const Cell &cell = r.cells[k];
			int cx = x;
			if (r.kind == Row::kPeer && k == 0) {
				if (r.hasSwatch) FillRectColor(c, x, y + (h - swatchW) / 2, swatchW, swatchW, r.swatch);
				cx += swatchW + swatchGap;
			}
			const bool hint = r.kind == Row::kTitle && k > 0;
			HFONT f = hint ? g_small : cell.bold ? g_bold : g_font;
			SelectObject(c, f);
			SetTextColor(c, cell.color);
			RECT tr = {cx, y, width - pad, y + h};
			DrawTextW(c, cell.text.c_str(), static_cast<int>(cell.text.size()), &tr,
				(hint ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			if (r.kind == Row::kPeer) {
				x += cols[k] + gap;
			}
			else {
				SIZE sz = {};
				GetTextExtentPoint32W(c, cell.text.c_str(), static_cast<int>(cell.text.size()), &sz);
				x += sz.cx + gap;
			}
		}
		y += h;
	}
}

void BlendPanel(uint32_t *buf, int w, int h, int ox, int oy, int opacity)
{
	const Canvas &cv = g_canvas;
	const uint32_t bg = 0xff000000u | (GetRValue(kPanel) << 16) | (GetGValue(kPanel) << 8) | GetBValue(kPanel);
	const uint32_t gbg = 0xff000000u | (GetRValue(kGraphBg) << 16) | (GetGValue(kGraphBg) << 8) | GetBValue(kGraphBg);
	const int aBack = (std::max)(30, (std::min)(100, opacity)) * 255 / 100;
	const int aFront = (std::min)(255, aBack + 110);
	for (int y = 0; y < cv.h; y++) {
		const int ty = oy + y;
		if (ty < 0 || ty >= h) continue;
		for (int x = 0; x < cv.w; x++) {
			const int tx = ox + x;
			if (tx < 0 || tx >= w) continue;
			const uint32_t src = cv.bits[static_cast<size_t>(y) * cv.w + x] | 0xff000000u;
			const int alpha = src == bg || src == gbg ? aBack : aFront;
			uint32_t &dst = buf[static_cast<size_t>(ty) * w + tx];
			const uint32_t rb = ((src & 0xff00ff) * alpha + (dst & 0xff00ff) * (255 - alpha)) >> 8;
			const uint32_t g = ((src & 0x00ff00) * alpha + (dst & 0x00ff00) * (255 - alpha)) >> 8;
			dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
		}
	}
}

int Margin(float s) { return static_cast<int>(8 * s); }

std::atomic<DWORD> g_saveAt{0};

void RequestSave()
{
	const DWORD at = GetTickCount() + 800;
	g_saveAt = at ? at : 1;
}

void PanelOrigin(int w, int h, float s, int &ox, int &oy)
{
	const int m = Margin(s);
	const int freeW = (std::max)(0, w - g_canvas.w - 2 * m), freeH = (std::max)(0, h - g_canvas.h - 2 * m);
	std::lock_guard<std::mutex> lock(g_uiMx);
	if (g_dragging) {
		ox = (std::max)(0, (std::min)(w - g_canvas.w, g_dragX));
		oy = (std::max)(0, (std::min)(h - g_canvas.h, g_dragY));
		return;
	}
	ox = m + freeW * (std::max)(0, (std::min)(1000, GetConfig().panelX)) / 1000;
	oy = m + freeH * (std::max)(0, (std::min)(1000, GetConfig().panelY)) / 1000;
}

void TakeSample(int fps, bool net)
{
	Sample sm;
	sm.fps = fps;
	{
		std::lock_guard<std::mutex> lock(g_frameMx);
		sm.worstMs = g_secWorstMs;
		sm.stutters = g_secStutters;
		sm.playing = g_secPlaying;
		g_secWorstMs = 0;
		g_secStutters = 0;
		g_secPlaying = false;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const long long busy = g_updateTicks.exchange(0);
	const int calls = g_updateLeafCalls.exchange(0);
	if (g_loadSince.QuadPart && now.QuadPart > g_loadSince.QuadPart && sm.playing) {
		sm.load = static_cast<int>((std::min)(100LL, (busy * 100 + (now.QuadPart - g_loadSince.QuadPart) / 2) / (now.QuadPart - g_loadSince.QuadPart)));
		if (g_loadLogged < 3 && ++g_loadLogged == 3) {
			LogD("perf", "panel: game CPU %d %% (%d Update calls per second that do work, nested up to %d)", sm.load, calls,
				g_updateMaxDepthSeen);
		}
	}
	g_loadSince = now;
	MEMORYSTATUSEX ms = {sizeof(ms)};
	GlobalMemoryStatusEx(&ms);
	sm.memTotal = static_cast<double>(ms.ullTotalVirtual) / 1073741824.0;
	sm.memUsed = static_cast<double>(ms.ullTotalVirtual - ms.ullAvailVirtual) / 1073741824.0;
	if (net) {
		const NetTurnSecond turn = NetTuneTakeSecond();
		sm.turnAvg = turn.avgMs;
		sm.turnMax = turn.maxMs;
		sm.lagMs = SpectatorLagMs();
		sm.delaySec = SpectatorDelaySec();
		for (const NetPeerStat &n : SteamNetStats()) {
			sm.peers.push_back({n.id, n.connected ? n.ping : -1, n.quality >= 0 ? static_cast<float>((1.0 - n.quality) * 100.0) : -1.0f});
		}
	}
	g_hist.push_back(std::move(sm));
	while (g_hist.size() > static_cast<size_t>(kHist)) g_hist.pop_front();
}

void SaveSnapped(int ox, int oy)
{
	const float s = g_scale;
	const int m = Margin(s), snap = static_cast<int>(28 * s);
	const int freeW = (std::max)(1, g_frameW - static_cast<int>(g_panel.right - g_panel.left) - 2 * m);
	const int freeH = (std::max)(1, g_frameH - static_cast<int>(g_panel.bottom - g_panel.top) - 2 * m);
	int px = (ox - m) * 1000 / freeW, py = (oy - m) * 1000 / freeH;
	if (ox - m < snap) px = 0;
	if (freeW - (ox - m) < snap) px = 1000;
	if (std::abs((ox - m) - freeW / 2) < snap) px = 500;
	if (oy - m < snap) py = 0;
	if (freeH - (oy - m) < snap) py = 1000;
	Config &c = MutableConfig();
	c.panelX = (std::max)(0, (std::min)(1000, px));
	c.panelY = (std::max)(0, (std::min)(1000, py));
	RequestSave();
}

int __fastcall HookGameUpdate(void *game)
{
	CrashLogLoadingEnd();
	g_updateTicks += ZoomFogGameThreadTick();
	QuickIconsGameThreadTick();
	NoteIconsGameTick();
	ZoomViewGameTick();
	ControlsGameTick();
	if (g_updateDepth > 0 && g_updateDepth <= kUpdateMaxDepth) g_updateStack[g_updateDepth - 1].nested = true;
	if (g_updateDepth >= kUpdateMaxDepth) return g_origGameUpdate(game);
	const int d = g_updateDepth++;
	g_updateMaxDepthSeen = (std::max)(g_updateMaxDepthSeen, g_updateDepth);
	g_updateStack[d].nested = false;
	QueryPerformanceCounter(&g_updateStack[d].start);
	const int r = g_origGameUpdate(game);
	LARGE_INTEGER b;
	QueryPerformanceCounter(&b);
	g_updateDepth = d;
	if (!g_updateStack[d].nested) {
		g_updateTicks += b.QuadPart - g_updateStack[d].start.QuadPart;
		g_updateLeafCalls++;
	}
	return r;
}

}

void OverlayInstall(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kGameUpdate);
	if (HookCreate(target, reinterpret_cast<void *>(&HookGameUpdate), reinterpret_cast<void **>(&g_origGameUpdate)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogD("perf", "panel: game load measured in CVXGame::Update");
	}
	else {
		LogE("perf", "panel: could not hook CVXGame::Update (no game load)");
	}
}

void OverlayNoteFrame()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
	bool running = false;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	static char *lastGame = nullptr;
	static DWORD gameSince = 0;
	if (game != lastGame) {
		lastGame = game;
		gameSince = GetTickCount();
	}
	if (game && GetTickCount() - gameSince > 8000) {
		typedef int(__fastcall * IsPaused_t)(void *);
		running = reinterpret_cast<IsPaused_t>((*reinterpret_cast<void ***>(game))[22])(game) == 0;
	}
	std::lock_guard<std::mutex> lock(g_frameMx);
	if (g_lastFrame.QuadPart && running) {
		const int ms = static_cast<int>((now.QuadPart - g_lastFrame.QuadPart) * 1000 / g_qpf.QuadPart);
		g_secPlaying = true;
		if (g_framesTotal < 600) {
			g_framesTotal++;
			if (g_updateDepth) g_framesInUpdate++;
			if (g_framesTotal == 600) LogD("perf", "panel: %d of 600 game images are delivered inside CVXGame::Update", g_framesInUpdate);
		}
		if (ms < 3000) {
			g_secWorstMs = (std::max)(g_secWorstMs, ms);
			if (ms >= kStutterMs) g_secStutters++;
		}
	}
	g_lastFrame = now;
}

bool OverlayNeedsRedraw()
{
	std::lock_guard<std::mutex> lock(g_uiMx);
	return g_moved || g_dirty;
}

bool OverlayHandleMouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM &lp)
{
	if (msg < WM_MOUSEFIRST || msg > WM_MOUSELAST) return false;
	if (!GetConfig().showPerf && !GetConfig().showNet) return false;
	std::lock_guard<std::mutex> lock(g_uiMx);
	const bool wheel = msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL;
	const int x = wheel ? g_mouseX : static_cast<short>(LOWORD(lp)), y = wheel ? g_mouseY : static_cast<short>(HIWORD(lp));
	if (!wheel) {
		g_mouseX = x;
		g_mouseY = y;
	}
	const bool visible = GetTickCount() - g_panelTick < 1500;
	const POINT pt = {x, y};
	const bool inside = visible && PtInRect(&g_panel, pt);
	if (msg == WM_MOUSEMOVE) {
		g_hover = inside;
		if (!g_dragging) return false;
		g_dragX = x - g_dragDX;
		g_dragY = y - g_dragDY;
		g_dragMoved = true;
		g_moved = true;
		const int edge = static_cast<int>(10 * g_scale);
		const int cx = (std::max)(edge, (std::min)(g_frameW - 1 - edge, x)), cy = (std::max)(edge, (std::min)(g_frameH - 1 - edge, y));
		lp = MAKELPARAM(cx, cy);
		return false;
	}
	if (msg == WM_LBUTTONUP && g_dragging) {
		g_dragging = false;
		ReleaseCapture();
		if (g_dragMoved) SaveSnapped(g_dragX, g_dragY);
		g_moved = true;
		return true;
	}
	if (!inside) return false;
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		const DWORD now = GetTickCount();
		const bool dbl = msg == WM_LBUTTONDBLCLK || (now - g_lastDown <= GetDoubleClickTime() &&
			std::abs(x - g_lastDownPt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) && std::abs(y - g_lastDownPt.y) <= GetSystemMetrics(SM_CYDOUBLECLK));
		if (dbl) {
			g_lastDown = 0;
			Config &c = MutableConfig();
			c.panelCompact = !c.panelCompact;
			RequestSave();
			g_dirty = true;
			LogD("perf", "panel %s", c.panelCompact ? "compact" : "full");
			return true;
		}
		g_lastDown = now;
		g_lastDownPt = pt;
		g_dragging = true;
		g_dragMoved = false;
		g_dragDX = x - g_panel.left;
		g_dragDY = y - g_panel.top;
		g_dragX = g_panel.left;
		g_dragY = g_panel.top;
		SetCapture(hwnd);
		return true;
	}
	case WM_MOUSEWHEEL: {
		Config &c = MutableConfig();
		c.panelOpacity = (std::max)(30, (std::min)(100, c.panelOpacity + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 10 : -10)));
		RequestSave();
		g_moved = true;
		return true;
	}
	default:
		return msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK ||
			msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP;
	}
}

bool OverlayHandleKey(UINT msg, WPARAM wp, LPARAM lp)
{
	static bool eatChar = false;
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) eatChar = false;
	if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && ControlsModKeyPressed(kModKeyPanel, static_cast<unsigned>(wp))) {
		eatChar = true;
		if (!(lp & (1 << 30))) {
			Config &c = MutableConfig();
			if (c.showPerf || c.showNet) {
				g_restorePerf = c.showPerf;
				g_restoreNet = c.showNet;
				c.showPerf = c.showNet = false;
			}
			else {
				c.showPerf = g_restorePerf;
				c.showNet = g_restoreNet;
			}
			SaveConfig();
			LogD("perf", "panel %s (keyboard shortcut)", c.showPerf || c.showNet ? "in view" : "hidden");
		}
		return true;
	}
	if ((msg == WM_CHAR || msg == WM_SYSCHAR) && eatChar) {
		eatChar = false;
		return true;
	}
	return false;
}

float PanelScale(int w, int h, int outW, int outH)
{
	if (outW <= 0 || outH <= 0) return (std::max)(0.8f, (std::min)(2.0f, h / 1080.0f * 1.15f));
	const float onScreen = (std::max)(0.8f, (std::min)(2.0f, outH / 1080.0f * 1.15f));
	const float shownH = (std::min)(static_cast<float>(outH), static_cast<float>(outW) * h / w);
	return (std::max)(0.3f, onScreen * h / shownH);
}

void DrawPerfOverlay(uint32_t *buf, int w, int h, int fps, int outW, int outH)
{
	const float s = PanelScale(w, h, outW, outH);
	const bool perf = GetConfig().showPerf, net = GetConfig().showNet, compact = GetConfig().panelCompact;
	const DWORD now = GetTickCount();
	const bool tick = now - g_built >= 1000;
	if (const DWORD due = g_saveAt; due && static_cast<int>(now - due) >= 0) {
		g_saveAt = 0;
		SaveConfig();
	}
	bool dirty, hover;
	{
		std::lock_guard<std::mutex> lock(g_uiMx);
		dirty = g_dirty;
		hover = g_hover;
		g_dirty = false;
		g_moved = false;
	}
	if (tick) TakeSample(fps, net);
	if (tick || dirty || g_builtFor[0] != w || g_builtFor[1] != h || g_builtScale != s || g_builtFor[2] != static_cast<int>(perf) ||
		g_builtFor[3] != static_cast<int>(net) || g_builtFor[4] != static_cast<int>(compact)) {
		if (tick) g_built = now;
		g_builtScale = s;
		g_builtFor[0] = w;
		g_builtFor[1] = h;
		g_builtFor[2] = perf;
		g_builtFor[3] = net;
		g_builtFor[4] = compact;
		const std::vector<NetPeerStat> stats = net ? SteamNetStats() : std::vector<NetPeerStat>();
		const std::vector<Row> rows = BuildRows(perf, net && !stats.empty(), compact, stats, hover);
		g_empty = rows.empty() || (!perf && stats.empty());
		std::map<uint64_t, COLORREF> colors;
		for (size_t k = 0; k < stats.size(); k++) colors[stats[k].id] = PeerColor(stats[k], k);
		if (!g_empty) Render(rows, s, compact, colors);
	}
	if (g_empty || !g_canvas.bits) return;
	int ox = 0, oy = 0;
	PanelOrigin(w, h, s, ox, oy);
	BlendPanel(buf, w, h, ox, oy, GetConfig().panelOpacity);
	std::lock_guard<std::mutex> lock(g_uiMx);
	g_panel = {ox, oy, ox + g_canvas.w, oy + g_canvas.h};
	g_panelTick = now;
	g_frameW = w;
	g_frameH = h;
	g_scale = s;
}
