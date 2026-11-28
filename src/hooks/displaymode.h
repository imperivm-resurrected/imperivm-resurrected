// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

bool InstallDisplayModeHooks(bool knownExe);

bool DisplayFakeMode(int w, int h, DWORD bpp, bool testOnly);

void DisplayAttachWindow(HWND hwnd);
// The game's main window once attached; null before.
HWND DisplayGameWindow();

void DisplayGetLogicalSize(HWND hwnd, int &w, int &h);

BOOL DisplayRealClientRect(HWND hwnd, RECT *rc);

void DisplayApplySettings(bool windowModeChanged);
void DisplaySetResizeHandler(bool (*apply)(int width, int height));
void DisplayHoldResize(bool hold);
void DisplaySetWindowCursorCheck(bool (*active)());
void DisplayRefreshWindowCursor();

typedef LRESULT (*DisplayWindowHandler)(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void DisplaySetWindowHandler(DisplayWindowHandler handler);

LRESULT DisplayForwardMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

bool DisplayIsWindowed();

std::vector<std::string> DisplayMonitorLabels();

void DisplayTargetSize(int &w, int &h);

void DisplayRestoreMonitor();

bool DisplayMonitorHasMode(int w, int h);

// For these, a negative monitor is the one chosen in the settings.
std::vector<POINT> DisplayExtendedSizes(int monitor = -1);

std::vector<POINT> DisplayWindowsSizes(int monitor = -1);

void DisplayMonitorSize(int monitor, int &w, int &h);

// The largest inner size a window has in the monitor's work area.
void DisplayWindowArea(int monitor, int &w, int &h);

bool DisplayModeAllowed(int w, int h);

bool DisplayFakesModeChange();
