// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallModernAudio(bool knownExe);
#include <windows.h>
bool DmShimCreate(REFCLSID clsid, LPVOID *out);
// 0 chat message, 1 invitation, 2 someone joins, 3 a patron joins.
void UiSoundPlay(int kind);
bool DmShimPausePath(void *path, bool pause);
