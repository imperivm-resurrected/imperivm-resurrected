// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// The "Change player" screen, drawn over the game's own dialog, which stays open under it and does the work.
void ProfileUiGameTick();
bool ProfileUiNeedsDraw();
void ProfileUiDraw(uint32_t *buf, int w, int h);
bool ProfileUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
