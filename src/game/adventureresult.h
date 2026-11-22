// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "game/battlelist.h"

// Watches the game declare a team's victory (DeclareTeamVictory): when the local player wins an adventure, its laurel, the hardest
// difficulty it was won at, goes into the active profile.
void InstallAdventureResult(bool knownExe);

// STAGING TEST TOOL, remove before a release: Ctrl+Shift+F12 in a single-player game gives the local player's team the victory, through the game's own end of game
// (CVXBasicGame::OnPlayerGameOver). On the game thread, once per frame.
void AdventureResultGameTick();

// The adventure the adventure screen has just started (its path and title in the game's list): its victory is kept by its path,
// until a saved game is loaded.
void AdventureResultNoteStart(const std::string &adventurePath, const std::string &title);

// The laurels of the active profile (Profiles/<profile>/imperivm-laurels.txt); empty when it has none.
std::vector<BattleLaurel> AdventureLaurels();
