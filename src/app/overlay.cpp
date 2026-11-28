// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "net/spectator.h"
#include "net/nettune.h"
#include "net/rejoin.h"
#include "game/gamelayout.h"
#include "game/healthbars.h"
#include "game/loadcurtain.h"
#include "game/mapfog.h"
#include "game/quickicons.h"
#include "game/unitpreload.h"
#include "game/noteicons.h"
#include "game/respawnsites.h"
#include "game/zoomview.h"
#include "core/crashlog.h"
#include "core/guard.h"
#include "core/logfiles.h"
#include "core/perfstats.h"
#include "core/version.h"
#include "app/overlay.h"
#include "app/overlay_view.h"
#include "i18n/tr.h"

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hooks/hookcount.h"
#include "hooks/orderdelay.h"
#include "hooks/tweaks.h"
#include "core/config.h"
#include "game/controls.h"
#include "hooks/displaymode.h"
#include "core/log.h"
#include "net/steamnet.h"
#include "ui/uikit.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/winpath.h"

namespace {

using overlay::Hit;
using overlay::View;

// The game thread's side: every image of the game, and what the game is doing.
std::mutex g_frameMx;
perf::FrameTracker g_tracker(65536);
struct GameState {
	bool match = false, paused = false, loading = false, continuous = false;
	uint32_t clock = 0;
};
GameState g_game;
std::atomic<HANDLE> g_gameThread{nullptr};
std::atomic<unsigned> g_blits{0}, g_frames{0};

typedef int(__fastcall *GameUpdate_t)(void *game);
GameUpdate_t g_origGameUpdate = nullptr;
std::atomic<long long> g_updateTicks{0}, g_updateMaxTicks{0};
std::atomic<int> g_updateLeafCalls{0};
int g_updateDepth = 0;
const int kUpdateMaxDepth = 8;
struct UpdateCall {
	LARGE_INTEGER start;
	bool nested;
} g_updateStack[kUpdateMaxDepth];

// The window thread's side: where the panel and its menu are, and the mouse over them.
std::mutex g_uiMx;
RECT g_panel = {}, g_menu = {};
std::vector<Hit> g_panelHits, g_menuHits;
DWORD g_panelTick = 0;
int g_frameW = 0, g_frameH = 0;
float g_scale = 1;
bool g_dragging = false, g_dragMoved = false, g_gearDown = false;
int g_dragDX = 0, g_dragDY = 0, g_dragX = 0, g_dragY = 0, g_downX = 0, g_downY = 0;
bool g_hover = false, g_menuOpen = false;
bool g_dirty = true, g_menuDirty = true, g_moved = false;
int g_mouseX = -1, g_mouseY = -1;
bool g_restorePerf = true, g_restoreNet = true;
bool g_captureWanted = false;

// What the render thread lays over the game: the panel and its menu as the painter left them, and the size it wants them for.
std::mutex g_frontMx;
overlay::Baked g_front, g_frontMenu;
unsigned g_frontSerial = 0, g_blendSerial = 0;
struct Want {
	int w = 0, h = 0;
	float s = 0;
} g_want;
std::atomic<bool> g_painterStarted{false};

// The painter's side: its own thread paints a few times a second, so the render thread only blends.
ui::Canvas g_canvas, g_menuCanvas;
overlay::Baked g_baked, g_menuBaked;
View g_view;
std::vector<perf::FrameRecord> g_window;
std::vector<perf::PerfEvent> g_events;
std::vector<float> g_scratch;
double g_lastRefresh = -1e9, g_lastSecond = -1e9;
int g_builtW = 0, g_builtH = 0;
float g_builtScale = 0;
bool g_empty = true;

struct History {
	static const int kSize = 60;
	float v[kSize] = {};
	int count = 0, next = 0;
	void Push(float x)
	{
		v[next] = x;
		next = (next + 1) % kSize;
		if (count < kSize) count++;
	}
	void CopyTo(std::vector<float> &out) const
	{
		out.clear();
		for (int i = 0; i < count; i++) out.push_back(v[(next + kSize - count + i) % kSize]);
	}
};
History g_cpuHist, g_memHist;
std::map<uint64_t, History> g_pingHist;

// The figures taken once a second.
struct Second {
	perf::CpuMeter cpu{9000000};
	int cores = 1;
	double addrUsed = 0, addrTotal = 0, privateBytes = 0;
	NetTurnSecond turn;
	int lagMs = -1, delaySec = 0;
	float simSpeed = -1, turnsPerSec = -1, updateMs = -1, updateMaxMs = -1;
	uint32_t lastClock = 0;
	double lastClockAt = 0;
	unsigned lastTurns = 0;
	bool haveClock = false;
	int matchSeconds = 0;
	bool logged = false;
} g_second;

struct Capture {
	bool on = false;
	double start = 0, end = 0;
	std::wstring toast;
	DWORD toastUntil = 0;
} g_capture;

std::atomic<DWORD> g_saveAt{0};

void RequestSave()
{
	const DWORD at = GetTickCount() + 800;
	g_saveAt = at ? at : 1;
}

double QpcFreqMs()
{
	static const double f = [] {
		LARGE_INTEGER q;
		QueryPerformanceFrequency(&q);
		return static_cast<double>(q.QuadPart) / 1000.0;
	}();
	return f;
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
		const uint32_t c = Field<uint32_t>(layout::PlayerAt(game, i), layout::kPlayerColor);
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

uint64_t FileTime100ns(const FILETIME &f)
{
	return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

int Corner(const Config &c)
{
	const bool edgeX = c.panelX == 0 || c.panelX == 1000, edgeY = c.panelY == 0 || c.panelY == 1000;
	if (!edgeX || !edgeY) return -1;
	return (c.panelX == 1000 ? 1 : 0) + (c.panelY == 1000 ? 2 : 0);
}

overlay::Settings SettingsOf(const Config &c)
{
	overlay::Settings s;
	s.perf = c.showPerf;
	s.net = c.showNet;
	s.compact = c.panelCompact;
	s.showLows = c.perfShowLows;
	s.showCpu = c.perfShowCpu;
	s.showMemory = c.perfShowMemory;
	s.showSim = c.perfShowSim;
	s.graphFrame = c.perfGraphFrame;
	s.graphCpu = c.perfGraphCpu;
	s.graphMemory = c.perfGraphMemory;
	s.graphPing = c.perfGraphPing;
	s.window = c.perfWindow;
	s.corner = Corner(c);
	s.scale = c.perfScale;
	s.opacity = c.panelOpacity;
	s.refresh = c.perfRefresh;
	s.captureSeconds = c.perfCapture;
	return s;
}

std::string CaptureFileName()
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	char name[64];
	sprintf_s(name, "perf-%04u%02u%02u-%02u%02u%02u.csv", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	return name;
}

void StartCapture(double now)
{
	g_capture.on = true;
	g_capture.start = now;
	g_capture.end = now + GetConfig().perfCapture * 1000.0;
	perf::NoteEvent(perf::kEventCapture);
	LogI("perf", "capture: recording %d s of frame times", GetConfig().perfCapture);
}

void FinishCapture(double now)
{
	g_capture.on = false;
	std::vector<perf::FrameRecord> frames;
	bool cut = false;
	{
		std::lock_guard<std::mutex> lock(g_frameMx);
		g_tracker.CopySince(g_capture.start, frames);
		cut = g_tracker.Size() && g_tracker.At(0).t > g_capture.start;
	}
	std::vector<perf::PerfEvent> events;
	perf::EventsSince(g_capture.start, events);
	std::vector<float> scratch;
	const perf::FrameSummary s = perf::Summarize(frames, g_capture.start, now, scratch);
	std::string header = std::string("Imperivm Resurrected ") + ModVersionText() + ", frame times of the game's images (SetDIBitsToDevice)";
	header += "\nimage " + std::to_string(g_builtW) + "x" + std::to_string(g_builtH) + ", " + std::to_string(g_second.cores) + " cores";
	if (cut) header += "\nthe start was lost: the game sent more images than the panel keeps";
	const std::string name = CaptureFileName();
	const std::string path = MakeLogPaths(LogGameDir()).modDir + "\\" + name;
	const double start = g_capture.start;
	// Tens of thousands of lines: written away from the panel's thread.
	std::thread([path, header, s, start, now, frames = std::move(frames), events = std::move(events)] {
		const std::string text = perf::CaptureCsv(frames, events, start, now, s, header);
		const std::string summary = perf::SummaryLine(s);
		FILE *f = nullptr;
		if (fopen_s(&f, path.c_str(), "wb") == 0 && f) {
			fwrite(text.data(), 1, text.size(), f);
			fclose(f);
			LogI("perf", "capture: %s in %s", summary.c_str(), path.c_str());
		}
		else {
			LogE("perf", "capture: could not write %s", path.c_str());
		}
	}).detach();
	wchar_t line[160];
	swprintf_s(line, L"%S · %.0f FPS · 1%% %.0f · %d", name.c_str(), s.avgFps, s.low1Fps, s.hitches);
	g_capture.toast = TrW(L"Saved: ") + line + L" " + TrW(L"hitches");
	g_capture.toastUntil = GetTickCount() + 10000;
}

// Once a second: CPU, memory, the network's turns, the simulation, and the small graphs' points.
void TakeSecond(double now, bool net)
{
	Second &sec = g_second;
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		sec.cores = std::max(1, static_cast<int>(si.dwNumberOfProcessors));
		FILETIME c, e, k, u, tk = {}, tu = {}, tc, te;
		GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
		if (const HANDLE game = g_gameThread.load()) GetThreadTimes(game, &tc, &te, &tk, &tu);
		sec.cpu.Update(static_cast<uint64_t>(now * 10000.0), FileTime100ns(k) + FileTime100ns(u), FileTime100ns(tk) + FileTime100ns(tu), sec.cores);
	}
	{
		MEMORYSTATUSEX ms = {sizeof(ms)};
		GlobalMemoryStatusEx(&ms);
		sec.addrTotal = static_cast<double>(ms.ullTotalVirtual) / 1073741824.0;
		sec.addrUsed = static_cast<double>(ms.ullTotalVirtual - ms.ullAvailVirtual) / 1073741824.0;
		PROCESS_MEMORY_COUNTERS_EX pm = {};
		pm.cb = sizeof(pm);
		if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pm), sizeof(pm))) {
			sec.privateBytes = static_cast<double>(pm.PrivateUsage) / 1073741824.0;
		}
	}
	sec.turn = NetTuneTakeSecond();
	sec.lagMs = net && !SpectatorCovering() ? SpectatorLagMs() : -1;
	sec.delaySec = net && !SpectatorCovering() ? SpectatorDelaySec() : 0;
	GameState game;
	{
		std::lock_guard<std::mutex> lock(g_frameMx);
		game = g_game;
	}
	const unsigned turns = OrderDelayTurns();
	const long long busy = g_updateTicks.exchange(0), worst = g_updateMaxTicks.exchange(0);
	const int calls = g_updateLeafCalls.exchange(0);
	if (game.match && !game.loading) {
		if (sec.haveClock && now > sec.lastClockAt) {
			sec.simSpeed = game.clock >= sec.lastClock ? static_cast<float>((game.clock - sec.lastClock) / (now - sec.lastClockAt)) : -1;
			sec.turnsPerSec = static_cast<float>((turns - sec.lastTurns) * 1000.0 / (now - sec.lastClockAt));
		}
		sec.updateMs = calls ? static_cast<float>(busy / QpcFreqMs() / calls) : -1;
		sec.updateMaxMs = calls ? static_cast<float>(worst / QpcFreqMs()) : -1;
		sec.haveClock = true;
		if (++sec.matchSeconds == 10 && !sec.logged) {
			sec.logged = true;
			LogD("perf", "panel: %u copies of the game's image a second make %u frames; CVXGame::Update ran %d times; the clock moved x%.2f",
				g_blits.load(), g_frames.load(), calls, sec.simSpeed);
		}
	}
	else {
		sec.simSpeed = sec.turnsPerSec = sec.updateMs = sec.updateMaxMs = -1;
		sec.haveClock = false;
		sec.matchSeconds = 0;
	}
	g_blits = 0;
	g_frames = 0;
	sec.lastClock = game.clock;
	sec.lastClockAt = now;
	sec.lastTurns = turns;
	g_cpuHist.Push(sec.cpu.thread);
	g_memHist.Push(static_cast<float>(sec.addrUsed));
	if (net) {
		const std::vector<NetPeerStat> stats = SteamNetStats();
		for (auto &entry : g_pingHist) {
			bool seen = false;
			for (const NetPeerStat &n : stats) seen = seen || n.id == entry.first;
			if (!seen) entry.second.Push(-1);
		}
		for (const NetPeerStat &n : stats) g_pingHist[n.id].Push(n.connected ? static_cast<float>(n.ping) : -1.0f);
		if (stats.empty()) g_pingHist.clear();
	}
}

void BuildView(View &v, double now, float s, bool hover, bool menuOpen, int menuX, int menuY)
{
	const Config &c = GetConfig();
	v.set = SettingsOf(c);
	v.s = s;
	const double window = v.set.window * 1000.0;
	GameState game;
	perf::ScreenState state;
	float threshold = 0;
	{
		std::lock_guard<std::mutex> lock(g_frameMx);
		g_tracker.CopySince(now - window, g_window);
		game = g_game;
		state = g_tracker.State(now, game.continuous);
		if (g_tracker.MedianMs() > 0) threshold = g_tracker.HitchThresholdMs();
	}
	v.frames = perf::Summarize(g_window, now - window, now, g_scratch);
	v.drawing = state == perf::ScreenState::kActive;
	v.hitchLineMs = game.continuous ? threshold : 0;
	if (game.loading) v.screen = overlay::Screen::kLoading;
	else if (!game.match) v.screen = overlay::Screen::kMenu;
	else if (game.paused) v.screen = overlay::Screen::kPaused;
	else if (state == perf::ScreenState::kStalled) v.screen = overlay::Screen::kStalled;
	else v.screen = overlay::Screen::kMatch;
	v.columns.resize(static_cast<size_t>(overlay::GraphColumns(v)));
	perf::EventsSince(now - window, g_events);
	perf::BuildColumns(g_window, g_events, now - window, now, static_cast<int>(v.columns.size()), v.columns.data());
	const Second &sec = g_second;
	v.cpuThread = sec.cpu.thread;
	v.cpuProcess = sec.cpu.process;
	v.cores = sec.cores;
	v.addrUsedGB = sec.addrUsed;
	v.addrTotalGB = sec.addrTotal;
	v.privateGB = sec.privateBytes;
	v.simSpeed = sec.simSpeed;
	v.turnsPerSec = sec.turnsPerSec;
	v.updateMs = sec.updateMs;
	v.updateMaxMs = sec.updateMaxMs;
	v.turnAvgMs = sec.turn.avgMs;
	v.turnMaxMs = sec.turn.maxMs;
	v.turnSizeMs = sec.turn.sizeAvgMs;
	v.turnWaitMs = sec.turn.waitMs;
	v.turnLateMs = sec.turn.lateMaxMs;
	v.resends = sec.turn.resends;
	v.lagMs = sec.lagMs;
	v.delaySec = sec.delaySec;
	v.cpuSpark.color = ui::pal::warn;
	g_cpuHist.CopyTo(v.cpuSpark.values);
	v.memSpark.color = ui::pal::ok;
	g_memHist.CopyTo(v.memSpark.values);
	v.peers.clear();
	v.pingSparks.clear();
	if (v.set.net) {
		const std::vector<NetPeerStat> stats = SteamNetStats();
		for (size_t i = 0; i < stats.size(); i++) {
			const NetPeerStat &n = stats[i];
			overlay::PeerLine p;
			p.name = winpath::WidenLenient(n.name, CP_UTF8);
			p.color = PeerColor(n, i);
			p.connected = n.connected;
			p.relay = n.relay;
			p.ping = n.ping;
			p.lossPct = n.quality >= 0 ? static_cast<float>((1.0 - n.quality) * 100.0) : -1.0f;
			p.inKBs = n.inKBs;
			p.outKBs = n.outKBs;
			v.peers.push_back(p);
			const auto hist = g_pingHist.find(n.id);
			if (hist != g_pingHist.end()) {
				overlay::Spark spark;
				spark.color = p.color;
				hist->second.CopyTo(spark.values);
				v.pingSparks.push_back(spark);
			}
		}
	}
	v.hover = hover;
	v.menuOpen = menuOpen;
	v.menuMouseX = menuX;
	v.menuMouseY = menuY;
	v.recordingLeft = g_capture.on ? std::max(0, static_cast<int>((g_capture.end - now) / 1000.0 + 0.999)) : -1;
	v.toast = GetTickCount() < g_capture.toastUntil ? g_capture.toast : std::wstring();
	const ControlKey key = ControlsModKey(kModKeyPerfCapture);
	v.captureKey = key.vk ? winpath::WidenLenient(ControlsKeyLabel(key), CP_UTF8) : std::wstring();
}

int Margin(float s) { return static_cast<int>(8 * s); }

void PanelOrigin(int w, int h, float s, int &ox, int &oy)
{
	const int m = Margin(s);
	const int freeW = (std::max)(0, w - g_front.w - 2 * m), freeH = (std::max)(0, h - g_front.h - 2 * m);
	std::lock_guard<std::mutex> lock(g_uiMx);
	if (g_dragging && g_dragMoved) {
		ox = (std::max)(0, (std::min)(w - g_front.w, g_dragX));
		oy = (std::max)(0, (std::min)(h - g_front.h, g_dragY));
		return;
	}
	ox = m + freeW * (std::max)(0, (std::min)(1000, GetConfig().panelX)) / 1000;
	oy = m + freeH * (std::max)(0, (std::min)(1000, GetConfig().panelY)) / 1000;
}

// The menu beside the panel, towards the middle of the screen.
void MenuOrigin(int w, int h, int ox, int oy, float s, int &mx, int &my)
{
	const int gap = static_cast<int>(4 * s);
	const bool right = ox + g_front.w / 2 > w / 2, below = oy + g_front.h / 2 < h / 2;
	mx = right ? ox + g_front.w - g_frontMenu.w : ox;
	my = below ? oy + g_front.h + gap : oy - g_frontMenu.h - gap;
	mx = (std::max)(0, (std::min)(w - g_frontMenu.w, mx));
	my = (std::max)(0, (std::min)(h - g_frontMenu.h, my));
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

// A click on the gear's menu; under g_uiMx.
void ApplyMenuHit(const Hit &hit)
{
	Config &c = MutableConfig();
	switch (hit.kind) {
	case overlay::kHitMode: c.panelCompact = hit.arg != 0; break;
	case overlay::kHitSection:
		// The last section on stays: with none the panel and its menu would go.
		if (hit.arg == 0 && (!c.showPerf || c.showNet)) c.showPerf = !c.showPerf;
		if (hit.arg == 1 && (!c.showNet || c.showPerf)) c.showNet = !c.showNet;
		break;
	case overlay::kHitData: {
		bool *flags[] = {&c.perfShowLows, &c.perfShowCpu, &c.perfShowMemory, &c.perfShowSim};
		if (hit.arg >= 0 && hit.arg < 4) *flags[hit.arg] = !*flags[hit.arg];
		break;
	}
	case overlay::kHitGraph: {
		bool *flags[] = {&c.perfGraphFrame, &c.perfGraphCpu, &c.perfGraphMemory, &c.perfGraphPing};
		if (hit.arg >= 0 && hit.arg < 4) *flags[hit.arg] = !*flags[hit.arg];
		break;
	}
	case overlay::kHitWindow: c.perfWindow = overlay::kWindows[std::max(0, std::min(2, hit.arg))]; break;
	case overlay::kHitCorner:
		c.panelX = (hit.arg % 2) * 1000;
		c.panelY = (hit.arg / 2) * 1000;
		break;
	case overlay::kHitScale: c.perfScale = overlay::kScales[std::max(0, std::min(3, hit.arg))]; break;
	case overlay::kHitOpacity: c.panelOpacity = overlay::kOpacities[std::max(0, std::min(3, hit.arg))]; break;
	case overlay::kHitRefresh: c.perfRefresh = overlay::kRefreshes[std::max(0, std::min(2, hit.arg))]; break;
	case overlay::kHitCapture: g_captureWanted = true; break;
	default: return;
	}
	if (hit.kind != overlay::kHitCapture) {
		RequestSave();
		LogD("perf", "panel: menu setting %d (%d) changed", hit.kind, hit.arg);
	}
	g_dirty = g_menuDirty = true;
}

void CycleMode()
{
	Config &c = MutableConfig();
	const char *now = "compact";
	if (!c.showPerf && !c.showNet) {
		c.showPerf = g_restorePerf || !g_restoreNet;
		c.showNet = g_restoreNet;
		c.panelCompact = true;
	}
	else if (c.panelCompact) {
		c.panelCompact = false;
		now = "full";
	}
	else {
		g_restorePerf = c.showPerf;
		g_restoreNet = c.showNet;
		c.showPerf = c.showNet = false;
		now = "hidden";
		std::lock_guard<std::mutex> lock(g_uiMx);
		g_menuOpen = false;
	}
	SaveConfig();
	{
		std::lock_guard<std::mutex> lock(g_uiMx);
		g_dirty = true;
	}
	LogD("perf", "panel %s (keyboard shortcut)", now);
}

float PanelScale(int w, int h, int outW, int outH)
{
	if (outW <= 0 || outH <= 0) return (std::max)(0.8f, (std::min)(2.0f, h / 1080.0f * 1.15f));
	const float onScreen = (std::max)(0.8f, (std::min)(2.0f, outH / 1080.0f * 1.15f));
	const float shownH = (std::min)(static_cast<float>(outH), static_cast<float>(outW) * h / w);
	return (std::max)(0.3f, onScreen * h / shownH);
}

int __fastcall HookGameUpdate(void *game)
{
	CrashLogLoadingEnd();
	// A C++ exception must not unwind through the game's frames.
	Guard("game update ticks", [] {
		UnitArtPreload();
		g_updateTicks += ZoomFogGameThreadTick();
		QuickIconsGameThreadTick();
		HealthBarsGameTick();
		NoteIconsGameTick();
		ZoomViewGameTick();
		RespawnSitesGameTick();
		ControlsGameTick();
		DisplayRefreshWindowCursor();
	});
	if (g_updateDepth > 0 && g_updateDepth <= kUpdateMaxDepth) g_updateStack[g_updateDepth - 1].nested = true;
	if (g_updateDepth >= kUpdateMaxDepth) return g_origGameUpdate(game);
	const int d = g_updateDepth++;
	g_updateStack[d].nested = false;
	QueryPerformanceCounter(&g_updateStack[d].start);
	const int r = g_origGameUpdate(game);
	LARGE_INTEGER b;
	QueryPerformanceCounter(&b);
	g_updateDepth = d;
	if (!g_updateStack[d].nested) {
		const long long ticks = b.QuadPart - g_updateStack[d].start.QuadPart;
		g_updateTicks += ticks;
		g_updateLeafCalls++;
		if (ticks > g_updateMaxTicks.load(std::memory_order_relaxed)) g_updateMaxTicks.store(ticks, std::memory_order_relaxed);
	}
	return r;
}

}

void OverlayInstall(bool knownExe)
{
	if (!knownExe) return;
	DisplaySetWindowCursorCheck(&ControlsMapHasFocus);
	if (HookInstall(addr::kGameUpdate, &HookGameUpdate, &g_origGameUpdate)) {
		LogD("perf", "panel: game update time measured in CVXGame::Update");
	}
	else {
		LogE("perf", "panel: could not hook CVXGame::Update (no update time)");
	}
}

void OverlayNoteFrame()
{
	const double now = perf::QpcMs();
	if (!g_gameThread.load()) g_gameThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
	g_blits++;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	static char *lastGame = nullptr;
	static double gameSince = 0;
	if (game != lastGame) {
		if (game && !lastGame) perf::NoteEvent(perf::kEventStart);
		if (!game && lastGame) perf::NoteEvent(perf::kEventEnd);
		lastGame = game;
		gameSince = now;
	}
	const bool loading = LoadCurtainUp();
	static bool wasLoading = false, wasRejoin = false;
	if (loading && !wasLoading) perf::NoteEvent(perf::kEventLoad);
	wasLoading = loading;
	const bool rejoin = RejoinReloading();
	if (rejoin && !wasRejoin) perf::NoteEvent(perf::kEventRejoin);
	wasRejoin = rejoin;
	GameState state;
	// The first seconds of a match still load its map.
	state.match = game && now - gameSince > 3000;
	state.loading = loading || rejoin;
	state.paused = game && TweaksGamePaused(game);
	state.continuous = state.match && !state.loading && !state.paused;
	state.clock = game ? Field<uint32_t>(game, layout::kGameClock) : 0;
	std::lock_guard<std::mutex> lock(g_frameMx);
	g_game = state;
	if (g_tracker.AddFrame(now, state.continuous)) g_frames++;
}

bool OverlayNeedsRedraw()
{
	{
		std::lock_guard<std::mutex> lock(g_frontMx);
		if (g_frontSerial != g_blendSerial) return true;
	}
	std::lock_guard<std::mutex> lock(g_uiMx);
	return g_moved;
}

bool OverlayHandleMouse(HWND hwnd, UINT msg, WPARAM, LPARAM &lp)
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
	const bool inPanel = visible && PtInRect(&g_panel, pt);
	const bool inMenu = visible && g_menuOpen && PtInRect(&g_menu, pt);
	if (msg == WM_MOUSEMOVE) {
		if (g_hover != (inPanel || inMenu)) {
			g_hover = inPanel || inMenu;
			g_dirty = true;
		}
		if (inMenu) g_menuDirty = true;
		if (!g_dragging) return false;
		if (std::abs(x - g_downX) + std::abs(y - g_downY) > static_cast<int>(4 * g_scale)) g_dragMoved = true;
		g_dragX = x - g_dragDX;
		g_dragY = y - g_dragDY;
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
		else if (g_gearDown) g_menuOpen = !g_menuOpen;
		g_gearDown = false;
		g_dirty = g_menuDirty = g_moved = true;
		LogD("perf", "panel: %s", g_dragMoved ? "moved" : g_menuOpen ? "menu open" : "menu closed");
		return true;
	}
	const bool down = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK;
	if (inMenu) {
		if (down) {
			const POINT local = {x - g_menu.left, y - g_menu.top};
			for (const Hit &hit : g_menuHits) {
				if (hit.kind == overlay::kHitMenu || !PtInRect(&hit.r, local)) continue;
				ApplyMenuHit(hit);
				break;
			}
		}
		return !wheel;
	}
	if (down && inPanel) {
		const POINT local = {x - g_panel.left, y - g_panel.top};
		for (const Hit &hit : g_panelHits) {
			if (hit.kind != overlay::kHitGear || !PtInRect(&hit.r, local)) continue;
			g_gearDown = true;
			g_dragging = true;
			g_dragMoved = false;
			g_downX = x;
			g_downY = y;
			g_dragDX = x - g_panel.left;
			g_dragDY = y - g_panel.top;
			g_dragX = g_panel.left;
			g_dragY = g_panel.top;
			SetCapture(hwnd);
			return true;
		}
	}
	// Elsewhere the panel lets the clicks through to the game; with the menu open, a click outside only closes it.
	if (down && g_menuOpen) {
		g_menuOpen = false;
		g_dirty = g_moved = true;
		return true;
	}
	return false;
}

bool OverlayHandleKey(UINT msg, WPARAM wp, LPARAM lp)
{
	static bool eatChar = false;
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) eatChar = false;
	const bool keyDown = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
	if (keyDown && ControlsModKeyPressed(kModKeyPanel, static_cast<unsigned>(wp))) {
		eatChar = true;
		if (!(lp & (1 << 30))) CycleMode();
		return true;
	}
	if (keyDown && ControlsModKeyPressed(kModKeyPerfCapture, static_cast<unsigned>(wp))) {
		eatChar = true;
		if (!(lp & (1 << 30))) {
			// The panel shows the recording: hidden, it comes back compact.
			if (!GetConfig().showPerf && !GetConfig().showNet) CycleMode();
			std::lock_guard<std::mutex> lock(g_uiMx);
			g_captureWanted = true;
		}
		return true;
	}
	if (keyDown && wp == VK_ESCAPE) {
		std::lock_guard<std::mutex> lock(g_uiMx);
		if (g_menuOpen) {
			g_menuOpen = false;
			g_dirty = g_moved = true;
			eatChar = true;
			return true;
		}
	}
	if ((msg == WM_CHAR || msg == WM_SYSCHAR) && eatChar) {
		eatChar = false;
		return true;
	}
	return false;
}

namespace {

// One pass of the painter: the second's figures, the capture, and the panel and its menu painted again when due.
void PaintTick()
{
	const Config &cfg = GetConfig();
	const double now = perf::QpcMs();
	const DWORD tick = GetTickCount();
	if (const DWORD due = g_saveAt; due && static_cast<int>(tick - due) >= 0) {
		g_saveAt = 0;
		SaveConfig();
	}
	Want want;
	{
		std::lock_guard<std::mutex> lock(g_frontMx);
		want = g_want;
	}
	bool dirty, menuDirty, hover, menuOpen, capture;
	int menuX = -1, menuY = -1;
	{
		std::lock_guard<std::mutex> lock(g_uiMx);
		dirty = g_dirty;
		menuDirty = g_menuDirty;
		hover = g_hover;
		menuOpen = g_menuOpen;
		capture = g_captureWanted;
		g_captureWanted = false;
		menuX = g_mouseX - g_menu.left;
		menuY = g_mouseY - g_menu.top;
		g_dirty = g_menuDirty = false;
	}
	if (capture) {
		if (g_capture.on) FinishCapture(now);
		else StartCapture(now);
		dirty = true;
	}
	if (g_capture.on && now >= g_capture.end) {
		FinishCapture(now);
		dirty = true;
	}
	if (!cfg.showPerf && !cfg.showNet) return;
	if (now - g_lastSecond >= 1000) {
		g_lastSecond = now;
		TakeSecond(now, cfg.showNet);
	}
	if (want.w <= 0 || want.s <= 0) return;
	bool painted = false;
	if (now - g_lastRefresh >= cfg.perfRefresh || dirty || g_builtW != want.w || g_builtH != want.h || g_builtScale != want.s) {
		g_lastRefresh = now;
		g_builtW = want.w;
		g_builtH = want.h;
		g_builtScale = want.s;
		BuildView(g_view, now, want.s, hover, menuOpen, menuX, menuY);
		std::vector<Hit> hits;
		overlay::PaintPanel(g_canvas, g_view, hits);
		g_empty = !g_canvas.bits || (!g_view.set.perf && g_view.peers.empty() && g_view.lagMs < 0 && g_view.turnAvgMs < 0);
		if (g_empty) g_baked.w = g_baked.h = 0;
		else g_baked.Bake(g_canvas, cfg.panelOpacity);
		{
			std::lock_guard<std::mutex> lock(g_frontMx);
			std::swap(g_front, g_baked);
		}
		std::lock_guard<std::mutex> lock(g_uiMx);
		g_panelHits.swap(hits);
		painted = menuDirty = true;
	}
	if (menuOpen && menuDirty) {
		g_view.menuOpen = true;
		g_view.menuMouseX = menuX;
		g_view.menuMouseY = menuY;
		std::vector<Hit> hits;
		overlay::PaintMenu(g_menuCanvas, g_view, hits);
		g_menuBaked.Bake(g_menuCanvas, 100);
		{
			std::lock_guard<std::mutex> lock(g_frontMx);
			std::swap(g_frontMenu, g_menuBaked);
		}
		std::lock_guard<std::mutex> lock(g_uiMx);
		g_menuHits.swap(hits);
		painted = true;
	}
	if (painted) {
		std::lock_guard<std::mutex> lock(g_frontMx);
		g_frontSerial++;
	}
}

DWORD WINAPI PainterThread(LPVOID)
{
	// Painting takes a few milliseconds a few times a second: never at the expense of the game.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	for (;;) {
		// The mouse over the menu and the keys are seen within this; the numbers change at the panel's own rate.
		Sleep(30);
		Guard("performance panel", [] { PaintTick(); });
	}
}

}

void DrawPerfOverlay(uint32_t *buf, int w, int h, int, int outW, int outH)
{
	if (!g_painterStarted.exchange(true)) {
		HANDLE thread = CreateThread(nullptr, 0, PainterThread, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
	}
	// Below 0.8 of its size the captions drop under 8 pixels and can't be read, whatever the size picked.
	const float s = (std::max)(0.8f, PanelScale(w, h, outW, outH) * GetConfig().perfScale / 100.0f);
	bool menuOpen;
	{
		std::lock_guard<std::mutex> lock(g_uiMx);
		menuOpen = g_menuOpen;
		g_moved = false;
	}
	std::lock_guard<std::mutex> lock(g_frontMx);
	g_want.w = w;
	g_want.h = h;
	g_want.s = s;
	g_blendSerial = g_frontSerial;
	if (g_front.w <= 0) {
		std::lock_guard<std::mutex> ui(g_uiMx);
		g_panel = g_menu = {};
		return;
	}
	int ox = 0, oy = 0;
	PanelOrigin(w, h, s, ox, oy);
	g_front.Blend(buf, w, h, ox, oy);
	int mx = 0, my = 0;
	const bool menu = menuOpen && g_frontMenu.w > 0;
	if (menu) {
		MenuOrigin(w, h, ox, oy, s, mx, my);
		g_frontMenu.Blend(buf, w, h, mx, my);
	}
	std::lock_guard<std::mutex> ui(g_uiMx);
	g_panel = {ox, oy, ox + g_front.w, oy + g_front.h};
	g_menu = menu ? RECT{mx, my, mx + g_frontMenu.w, my + g_frontMenu.h} : RECT{};
	g_panelTick = GetTickCount();
	g_frameW = w;
	g_frameH = h;
	g_scale = s;
}
