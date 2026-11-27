// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstdint>

void ControlsUiOpen();
// The same screen as the options' last tab: their header and tabs, and their Back.
void ControlsUiOpenInOptions();
bool ControlsUiNeedsDraw();
void ControlsUiDraw(uint32_t *buf, int w, int h);
bool ControlsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
