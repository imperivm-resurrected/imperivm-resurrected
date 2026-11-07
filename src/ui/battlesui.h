// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

void BattlesUiGameTick();
bool BattlesUiNeedsDraw();
void BattlesUiDraw(uint32_t *buf, int w, int h);
bool BattlesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
