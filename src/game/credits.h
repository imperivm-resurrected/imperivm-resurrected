// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

void InstallCreditsHooks(bool knownExe);

// The game's credits screen (CVXUICreditsMenu) stays open under the mod's one, with its scrolling stopped, and its Close button
// takes the player back. Null when it is not open. Everything but this call belongs on the game thread.
void *CreditsDialogOpen();
// Its speed in pixels per second (PixPerSec of CreditsMenu.ini).
int CreditsDialogSpeed();
// The game's credits text in the player's language, without the block the mod puts before it for the game's own screen.
bool CreditsGameText(std::string &out);
bool CreditsDialogClose();
