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

// The adventure the adventure screen has just started (its path and title in the game's list): its victory is kept by its path,
// until a saved game is loaded.
void AdventureResultNoteStart(const std::string &adventurePath, const std::string &title);

// The laurels of the active profile (Profiles/<profile>/imperivm-laurels.txt); empty when it has none.
std::vector<BattleLaurel> AdventureLaurels();

// The conquests' laurels of the active profile (Profiles/<profile>/imperivm-conquest-laurels.txt), and a count that changes each time
// one is saved, so a screen knows when to read them again.
std::vector<ConquestLaurel> ConquestLaurels();
unsigned ConquestLaurelsSerial();
// Keeps a victory of the conquest `key` at `level` (1 to 3): of a territory, or of the whole conquest with `territory` empty.
void ConquestLaurelNote(const std::string &key, const std::string &territory, int level);
