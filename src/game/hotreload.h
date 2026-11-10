// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Finishes a ruleset switch made without restarting: when a match is created, makes the game read its data again with the rules mounted
// now (the game reads them once per process). Any failure restarts the game with those rules.
void InstallHotReload(bool knownExe);
