// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
void InstallSelectionGuard(bool knownExe);
// Called as the game sets up the selection bars for a player (a world was just created): empties the effective selection
// when it still holds objects of a world that is gone.
void DropStaleEffectiveSelection(void *manager);
