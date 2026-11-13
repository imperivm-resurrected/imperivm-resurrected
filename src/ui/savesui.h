// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>

void SavesUiOpen();
// Loads one game as the load screen's Load button does, with the same checks; the screen stays open on it when they stop the load.
void SavesUiLoadSave(const std::string &vfsPath);
void SavesUiOpenSave(void *dlg);
void SavesUiOpenLoad(void *dlg);
void InstallSavesUi(bool knownExe);
bool SavesUiIsOpen();

bool SavesUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);

bool SavesUiNeedsDraw();
void SavesUiDraw(uint32_t *buf, int w, int h);
