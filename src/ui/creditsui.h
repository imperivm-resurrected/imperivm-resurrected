// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// The credits screen, drawn over the game's own, which stays open under it and closes it.
void CreditsUiGameTick();
bool CreditsUiNeedsDraw();
void CreditsUiDraw(uint32_t *buf, int w, int h);
bool CreditsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
