// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// Drawn over the game's diplomacy dialog, which stays underneath and does the work: the mod only moves its check boxes and presses its buttons.
void DiplomacyUiGameTick();
bool DiplomacyUiNeedsDraw();
void DiplomacyUiDraw(uint32_t *buf, int w, int h);
bool DiplomacyUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
