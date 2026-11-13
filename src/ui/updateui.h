// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstdint>

void UpdateUiDrawCorner(uint32_t *buf, int w, int h, const RECT &heart);
void UpdateUiDrawPanel(uint32_t *buf, int w, int h);
bool UpdateUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
bool UpdateUiPanelOpen();
