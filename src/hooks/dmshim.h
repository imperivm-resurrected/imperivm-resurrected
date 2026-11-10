// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallModernAudio(bool knownExe);
#include <windows.h>

#include <string>
#include <vector>

bool DmShimCreate(REFCLSID clsid, LPVOID *out);
// 0 chat message, 1 invitation, 2 someone joins, 3 a patron joins.
void UiSoundPlay(int kind);
bool DmShimPausePath(void *path, bool pause);
// Whether the game's sound goes through miniaudio (the modern audio).
bool DmShimReady();
void DmShimSetMasterVolume(int percent);
std::vector<std::string> DmShimOutputDevices();
// An empty name, or one that is not connected, is the default device.
void DmShimSetOutputDevice(const std::string &name);
