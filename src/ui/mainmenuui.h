// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// The main menu drawn by the mod over the game's own dialog, whose buttons it hides and clicks.
void MainMenuUiGameTick();
void MainMenuUiShown(void *dlg);
void MainMenuUiDestroyed(void *dlg);
// When the menu next comes back, the multiplayer group's submenu is open (the multiplayer screen's Back).
void MainMenuUiOpenMultiplayerGroup();
bool MainMenuUiNeedsDraw();
// While the submenu slides, the menu is drawn on every pass of the screen, not at the pace of a still menu.
bool MainMenuUiAnimating();
void MainMenuUiDraw(uint32_t *buf, int w, int h);
bool MainMenuUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
