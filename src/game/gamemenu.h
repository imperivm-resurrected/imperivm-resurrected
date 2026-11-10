// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

void InstallGameMenuHooks(bool knownExe);

void *GameMenuVisible();
void *EndMenuVisible();
void *StatisticsOpen();
void *ConfirmOpen();
std::string ConfirmText();

struct NativeButtonState {
	bool exists = false, visible = false, enabled = false;
};
NativeButtonState NativeButton(void *dlg, int id);
void NativeButtonClick(void *dlg, int id);
void ToggleGameMenu();
void EndMenuLoadGame(void *dlg);
bool GameHasSeveralHumans();

// Empty when the control is missing or hidden.
std::string NativeText(void *dlg, int id);

int StatisticsPlayerCount(void *dlg);
int StatisticsPlayerIndex(void *dlg, int row);
