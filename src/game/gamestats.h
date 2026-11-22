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
// Every player's values now, without keeping them: what the league reports, read at the same tick of the game on every computer. The
// number of players read; `active` marks them. 0 outside a game.
int GameStatsReadAll(StatSample out[kStatMaxPlayers], bool active[kStatMaxPlayers]);
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

// The game's own record of a player so far (its CPlayerStats, what the profile keeps of each game): units killed and lost, the largest
// army, the gold gathered (taxes, captured and the rest) and the unit that reached the highest level, by the name the game gives it
// (for GameText). False outside a game.
struct GameRecordFacts {
	uint32_t killed = 0, lost = 0, armyMax = 0, gold = 0, bestLevel = 0, produced = 0;
	std::string bestUnit;
};
bool GameStatsRecordOf(int player, GameRecordFacts &out);
// Writes the history next to a save just made, as <save>.stats.
void GameStatsWriteFile(const std::string &saveFile);
