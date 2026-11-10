// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// The floating chat of a match: the bar that replaces the game's chat window and the lines that fade over the map.
void ChatUiInstall();
void ChatUiGameTick();
bool ChatUiNeedsDraw();
void ChatUiDraw(uint32_t *buf, int w, int h);
bool ChatUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
// The chat goes back to its place above the command bar.
void ChatUiResetPosition();
bool ChatUiPositionMoved();
