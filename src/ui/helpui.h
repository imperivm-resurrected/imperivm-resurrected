// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// The help screen (F1), drawn over the game's help, which stays open under it and closes it.
void HelpUiGameTick();
bool HelpUiNeedsDraw();
void HelpUiDraw(uint32_t *buf, int w, int h);
bool HelpUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
