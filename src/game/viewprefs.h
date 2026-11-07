// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The health bars a match starts with, as saved in the .ini ([Game] HealthBars).
enum HealthBarsStart { kBarsHidden = 0, kBarsAll = 1, kBarsHeroes = 2, kBarsMine = 3 };
inline constexpr int kBarsStartCount = 4;

int HealthBarsClampStart(int value);
// The game's own mode for that start: 0 hidden, 1 every bar.
int HealthBarsNativeMode(int start);
// What the mod leaves out on top of the game's mode: kBarsHeroes, kBarsMine or kBarsAll for nothing.
int HealthBarsFilter(int start);
// The filter lasts while the game's mode is the one it was set on, or hidden (the key hides and shows that same mode);
// cycling the modes with Ctrl gives the game's own ones back.
bool HealthBarsFilterHolds(int nativeMode);
bool HealthBarsKeep(int filter, bool hero, bool mine);

// How often the zoom map is drawn again while it is open ([Map] RefreshMs): smooth, normal, light.
inline constexpr int kZoomRefreshPresetCount = 3;
int ZoomRefreshPresetMs(int preset);
// The preset closest to a pause in ms; 0 (only when the game redraws it) is the lightest.
int ZoomRefreshPreset(int ms);
