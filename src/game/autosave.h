// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

void InstallAutosave(bool knownExe);
void AutosaveTick();
bool AutosaveOnExit();
void AutosaveQuitIfReady();
std::string AutosaveRequestNamed(const std::string &name);
bool AutosavePending();
// The save file at gamePath (any separators, with or without the folders before it) is the last autosave this mod asked for.
bool AutosaveIsAutosaveFile(const std::string &gamePath);
