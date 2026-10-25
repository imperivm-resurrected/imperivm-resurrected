// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

bool InstallDisplayModeHooks();

bool DisplayFakeMode(int w, int h, DWORD bpp, bool testOnly);

void DisplayAttachWindow(HWND hwnd);

void DisplayGetLogicalSize(HWND hwnd, int &w, int &h);

BOOL DisplayRealClientRect(HWND hwnd, RECT *rc);

void DisplayApplySettings(bool windowModeChanged);

typedef LRESULT (*DisplayWindowHandler)(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void DisplaySetWindowHandler(DisplayWindowHandler handler);

LRESULT DisplayForwardMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

bool DisplayIsWindowed();

std::vector<std::string> DisplayMonitorLabels();

void DisplayTargetSize(int &w, int &h);

void DisplayRestoreMonitor();

bool DisplayMonitorHasMode(int w, int h);

std::vector<POINT> DisplayExtendedSizes();

std::vector<POINT> DisplayWindowsSizes();

bool DisplayModeAllowed(int w, int h);

bool DisplayFakesModeChange();
