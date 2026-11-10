// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void InstallZoomView(bool knownExe);
bool ZoomViewHandleMouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void ZoomViewGameTick();
// The point of the open zoom map under (x, y), in the view coordinates the game's location messages carry.
bool ZoomViewScreenPoint(int x, int y, int *point);
bool ZoomViewOpen();
bool ZoomViewContains(int x, int y);
// The box selection is on and held with this key.
bool ZoomViewBoxUses(unsigned vk);
// Called for a click on the big map made with the box key but without a drag; true when it was used.
typedef bool (*ZoomClickFn)(int x, int y);
void ZoomViewSetClickHook(ZoomClickFn fn);
