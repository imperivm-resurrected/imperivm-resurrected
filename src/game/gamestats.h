// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/statshistory.h"

struct StatsSnapshot {
	StatsHistory history;
	std::vector<StatsPlayerInfo> players;
};

void InstallGameStats(bool knownExe);

// Takes the samples as the game runs. Reads the game and changes nothing in it, so it is safe in a multiplayer game.
void GameStatsTick();
// One more sample now, whatever the time since the last one.
void GameStatsSampleNow();
std::shared_ptr<const StatsSnapshot> GameStatsSnapshot();
// Writes the history next to a save just made, as <save>.stats.
void GameStatsWriteFile(const std::string &saveFile);
