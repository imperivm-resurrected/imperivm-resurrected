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

struct StatsLivePlayer {
	StatsPlayerInfo info;
	// The game's own state of the player: 1 playing, 2 defeated, 3 victorious.
	int state = 0;
	// A player of the game, not the map's neutral one.
	bool playable = true;
};
// The players as they are at this moment, without taking a sample, and the game's clock. False outside a game.
bool GameStatsLive(std::vector<StatsLivePlayer> &players, uint32_t &clockMs);
// Writes the history next to a save just made, as <save>.stats.
void GameStatsWriteFile(const std::string &saveFile);
