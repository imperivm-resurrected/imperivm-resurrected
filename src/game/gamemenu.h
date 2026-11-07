// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

void InstallGameMenuHooks(bool knownExe);

void *GameMenuVisible();
void *ConfirmOpen();
std::string ConfirmText();

bool NativeButtonEnabled(void *dlg, int id, bool &enabled);
void NativeButtonClick(void *dlg, int id);
