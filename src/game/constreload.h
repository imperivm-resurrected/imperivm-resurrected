// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Loads data/const.ini again, with the rules mounted now, when a match starts: the game reads it once per process. Installed only
// when [Mods] ConstHotReload=1.
void InstallConstReload(bool knownExe);
