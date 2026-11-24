// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX

#include <windows.h>

#include <cstdint>

// A ranked search followed from every screen of the menus but the multiplayer one (which has its own): a pill at the top centre while
// searching, and a window to accept or decline when a match is found. Nothing during a game.
void LeagueSearchUiGameTick();
bool LeagueSearchUiNeedsDraw();
void LeagueSearchUiDraw(uint32_t *buf, int w, int h);
bool LeagueSearchUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
