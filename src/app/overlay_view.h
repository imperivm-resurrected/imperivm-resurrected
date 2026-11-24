// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/perfstats.h"
#include "ui/uikit.h"

// What the performance panel shows, gathered by app/overlay.cpp, and how it is painted: the painting needs nothing of the game.
namespace overlay {

enum class Screen { kMatch, kMenu, kPaused, kLoading, kStalled };

struct PeerLine {
	std::wstring name;
	COLORREF color = 0;
	bool connected = false, relay = false;
	int ping = -1;
	// -1 when Steam has no figure.
	float lossPct = -1;
	float inKBs = 0, outKBs = 0;
};

struct Spark {
	COLORREF color = 0;
	// One value a second, oldest first; below 0 for none.
	std::vector<float> values;
};

// The panel's settings, as the gear's menu changes them.
struct Settings {
	bool perf = true, net = false, compact = true;
	bool showLows = true, showCpu = true, showMemory = true, showSim = true;
	bool graphFrame = true, graphCpu = false, graphMemory = false, graphPing = true;
	int window = 30, corner = 1, scale = 100, opacity = 90, refresh = 500;
	int captureSeconds = 30;
};

struct View {
	Settings set;
	float s = 1;
	Screen screen = Screen::kMenu;
	// The game draws continuously: the frame numbers mean something.
	bool drawing = false;
	perf::FrameSummary frames;
	float hitchLineMs = 0;
	std::vector<perf::GraphColumn> columns;
	// The game thread's share of one core and the process's share of all of them; below 0 before the first second.
	float cpuThread = -1, cpuProcess = -1;
	int cores = 1;
	// The address space the 32-bit game can use (4 GB with Large Address Aware on 64-bit Windows), how much of it is taken, and the
	// memory the process owns.
	double addrUsedGB = 0, addrTotalGB = 0, privateGB = 0;
	// In a match: the game's clock against the real one, command turns a second, and CVXGame::Update's time per call.
	float simSpeed = -1, turnsPerSec = -1, updateMs = -1, updateMaxMs = -1;
	std::vector<PeerLine> peers;
	int turnAvgMs = -1, turnMaxMs = 0, turnSizeMs = -1, turnWaitMs = 0, turnLateMs = -1, resends = 0;
	int lagMs = -1, delaySec = 0;
	Spark cpuSpark, memSpark;
	std::vector<Spark> pingSparks;
	bool hover = false, menuOpen = false;
	// The mouse over the menu, in its canvas's pixels.
	int menuMouseX = -1, menuMouseY = -1;
	int recordingLeft = -1;
	std::wstring toast, captureKey;
};

enum HitKind { kHitNone, kHitGear, kHitMode, kHitSection, kHitData, kHitGraph, kHitWindow, kHitCorner, kHitScale, kHitOpacity, kHitRefresh,
	kHitCapture, kHitMenu };
struct Hit {
	RECT r;
	int kind;
	int arg;
};

constexpr int kWindows[3] = {10, 30, 60};
constexpr int kScales[4] = {75, 100, 125, 150};
constexpr int kOpacities[4] = {60, 75, 90, 100};
constexpr int kRefreshes[3] = {250, 500, 1000};

// How many columns the frame-time graph has at the view's scale.
int GraphColumns(const View &v);
// The panel, into `cv` (sized to it). The hits are in the canvas's pixels.
void PaintPanel(ui::Canvas &cv, const View &v, std::vector<Hit> &hits);
// The gear's menu.
void PaintMenu(ui::Canvas &cv, const View &v, std::vector<Hit> &hits);

// The canvas as it goes over the game: each pixel with its alpha, the panel's background at `opacity` percent. Built once a refresh.
struct Baked {
	int w = 0, h = 0;
	std::vector<uint32_t> pre;
	std::vector<uint32_t> inv;
	void Bake(const ui::Canvas &cv, int opacity);
	void Blend(uint32_t *buf, int bw, int bh, int ox, int oy) const;
};

}
