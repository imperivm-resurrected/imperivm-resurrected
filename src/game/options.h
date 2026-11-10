// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

void InstallOptionsHooks(bool knownExe);

// The game's options dialog stays open under the mod's screen and keeps doing the work: these read and change its controls.
// Null when it is not open. Everything but this call belongs on the game thread.
void *OptionsDialogOpen();
bool OptionsReadCheck(int id, bool fallback);
void OptionsWriteCheck(int id, bool value);
int OptionsReadScroll(int id, int fallback);
void OptionsWriteScroll(int id, int value);
bool OptionsControlEnabled(int id);
int OptionsReadCombo(int id);
std::vector<std::string> OptionsComboItems(int id);
void OptionsWriteCombo(int id, int index);
void OptionsClick(int id);
// Fills the resolution list for a monitor, mode and extended view not applied yet, keeping the size shown if it is there.
void OptionsPreviewResolutions(int monitor, bool windowed, bool extendedView);
