// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

void ConquestUiGameTick();
bool ConquestUiNeedsDraw();
// The mouse is moving over the screen: it is drawn on every pass of the render thread (up to kAnimateHz), not 30 times a second.
bool ConquestUiAnimating();
void ConquestUiDraw(uint32_t *buf, int w, int h);
bool ConquestUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);

// The game shows its "you have won" box of a conquest (menuini/conquestvictory.ini) between these two calls.
void ConquestUiVictoryShown(const char *text);
void ConquestUiVictoryClosed();
