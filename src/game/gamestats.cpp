// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/gamestats.h"

#include <windows.h>

#include <cstddef>

#include "addr/state.h"
#include "addr/world.h"

namespace {
const size_t kGameClock = 0x1258, kLocalPlayer = 0x12c8, kPlayersStart = 0x12cc, kPlayerSize = 800;
const size_t kGameStats = 0x258, kStatsRecordSize = 0xd0;
const size_t kPlayerId = 8, kPlayerDiplomacy = 0x24, kPlayerActive = 0x64, kPlayerAi = 0x88, kPlayerName = 0x94, kPlayerNameCapacity = 0xa8, kPlayerState = 0x290, kPlayerColor = 0x314;
const int kStatePlaying = 1;

// The totals function adds up into this: mask at 0, gold 1, food 2, units on the map 3, units in buildings 4, city points 5, mask again at 6.
struct Totals {
	unsigned mask;
	int gold, food, unitsOutside, unitsInside, cityPoints;
	unsigned maskAgain;
};

typedef void(__fastcall *ComputeTotals_t)(Totals *totals);

StatsHistory g_history;
StatsPlayerInfo g_info[kStatMaxPlayers];
bool g_seen[kStatMaxPlayers] = {};
char *g_game = nullptr;
uint32_t g_lastClock = 0, g_lastSample = 0;
bool g_sampled = false;
std::shared_ptr<const StatsSnapshot> g_snapshot;
bool g_dirty = true;

template <class T>
T Field(const char *base, size_t offset)
{
	return *reinterpret_cast<const T *>(base + offset);
}

bool ReadClock(const char *game, uint32_t &clock)
{
	__try {
		clock = Field<uint32_t>(game, kGameClock);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadTotals(int player, Totals &totals)
{
	totals = {};
	totals.mask = totals.maskAgain = 1u << player;
	__try {
		reinterpret_cast<ComputeTotals_t>(addr::kComputePlayerTotals)(&totals);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

struct RawPlayer {
	bool active = false, playing = false, human = false, local = false, ally = false;
	char name[64] = {};
	uint32_t rgb = 0;
};

bool ReadPlayer(const char *game, int index, RawPlayer &out)
{
	__try {
		const char *player = game + kPlayersStart + index * kPlayerSize;
		const int state = Field<int>(player, kPlayerState);
		out.active = Field<int>(player, kPlayerActive) != 0 || state != 0;
		if (!out.active) return true;
		out.playing = state == kStatePlaying;
		out.human = Field<void *>(player, kPlayerAi) == nullptr;
		const char *local = Field<const char *>(game, kLocalPlayer);
		out.local = local == player;
		if (local && !out.local) {
			const int ours = Field<int>(local, kPlayerId), theirs = Field<int>(player, kPlayerId);
			const bool weAlly = (Field<unsigned char>(local, kPlayerDiplomacy + theirs * 4) & 1) != 0;
			const bool theyAlly = (Field<unsigned char>(player, kPlayerDiplomacy + ours * 4) & 1) != 0;
			out.ally = weAlly && theyAlly;
		}
		const char *name = Field<unsigned>(player, kPlayerNameCapacity) < 0x10 ? player + kPlayerName : Field<const char *>(player, kPlayerName);
		for (size_t i = 0; name && i + 1 < sizeof(out.name) && name[i]; i++) out.name[i] = name[i];
		const unsigned color = Field<unsigned>(player, kPlayerColor);
		out.rgb = ((color >> 10 & 0x1f) * 255 / 31) << 16 | ((color >> 5 & 0x1f) * 255 / 31) << 8 | ((color & 0x1f) * 255 / 31);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadRecord(const char *game, int index, StatSample &s, int &killRatio, int &storedStrength)
{
	__try {
		const char *record = game + kGameStats + (index + 1) * kStatsRecordSize;
		const int goldSpent = Field<int>(record, 0x38), built = Field<int>(record, 0x40), lost = Field<int>(record, 0x48);
		s.values[kStatGoldSpent] = goldSpent;
		s.values[kStatFoodSpent] = Field<int>(record, 0x3c);
		s.values[kStatUnitsBuilt] = built;
		s.values[kStatKilled] = Field<int>(record, 0x44);
		s.values[kStatLost] = lost;
		s.values[kStatUnitsMax] = Field<int>(record, 0x4c);
		s.values[kStatLevelMax] = Field<int>(record, 0x50);
		s.values[kStatUnitsAlive] = built > lost ? built - lost : 0;
		s.values[kStatDamageTaken] = Field<int>(record, 0xa0);
		s.values[kStatDamageDealt] = Field<int>(record, 0xa4);
		s.values[kStatGoldCaptured] = Field<int>(record, 0xac);
		s.values[kStatGoldTaxes] = Field<int>(record, 0xb0);
		s.values[kStatGoldOther] = Field<int>(record, 0xb4);
		s.values[kStatGoldTotal] = s.values[kStatGoldCaptured] + s.values[kStatGoldTaxes] + s.values[kStatGoldOther];
		s.values[kStatDevelopment] = static_cast<int>(static_cast<unsigned>(goldSpent) / 500);
		const unsigned killed = Field<unsigned>(record, 0xbc) >> 1, died = Field<unsigned>(record, 0xc0) >> 1;
		// As CPlayerStats::GetKillRatioPercent.
		killRatio = static_cast<int>((killed + 1000 + Field<int>(record, 0xa4)) * 100 / (died + 10000 + Field<int>(record, 0xa0)));
		s.values[kStatMilitary] = killRatio;
		storedStrength = Field<int>(record, 0xb8);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void Sample(const char *game, uint32_t clock)
{
	for (int index = 0; index < kStatMaxPlayers; index++) {
		RawPlayer raw;
		if (!ReadPlayer(game, index, raw) || !raw.active) continue;
		StatSample s;
		s.ms = clock;
		int killRatio = 100, strength = 0;
		if (!ReadRecord(game, index, s, killRatio, strength)) continue;
		// A player out of the game keeps the strength the game stored when they lost; the totals would find nothing.
		Totals totals;
		if (raw.playing && ReadTotals(index, totals)) {
			s.values[kStatGoldHeld] = totals.gold;
			s.values[kStatFoodHeld] = totals.food;
			strength = totals.cityPoints + static_cast<int>((static_cast<unsigned>(totals.food) >> 1) + totals.unitsInside + totals.unitsOutside + totals.gold) / 100;
		}
		s.values[kStatArmyStrength] = strength;
		// As CPlayerStats::CalcPowerScore.
		s.values[kStatScore] = strength * killRatio / 100 + s.values[kStatDevelopment];
		g_history.Add(index, s);
		StatsPlayerInfo &info = g_info[index];
		info.index = index;
		info.name = raw.name;
		info.rgb = raw.rgb;
		info.human = raw.human;
		info.local = raw.local;
		info.ally = raw.ally;
		g_seen[index] = true;
	}
	g_lastSample = clock;
	g_sampled = true;
	g_dirty = true;
}

void Reset(char *game)
{
	g_history.Clear();
	for (bool &seen : g_seen) seen = false;
	g_game = game;
	g_lastClock = 0;
	g_lastSample = 0;
	g_sampled = false;
	g_dirty = true;
}
}

void GameStatsTick()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) {
		if (g_game) Reset(nullptr);
		return;
	}
	uint32_t clock = 0;
	if (!ReadClock(game, clock)) return;
	if (game != g_game || clock < g_lastClock) Reset(game);
	// Only a game that advances is sure to be fully there: loading one also shows frames.
	const bool advanced = clock != g_lastClock;
	const bool firstTick = g_lastClock == 0;
	g_lastClock = clock;
	if (!advanced || firstTick) return;
	if (g_sampled && clock - g_lastSample < g_history.IntervalMs()) return;
	Sample(game, clock);
}

void GameStatsSampleNow()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) return;
	uint32_t clock = 0;
	if (!ReadClock(game, clock)) return;
	if (game != g_game || clock < g_lastClock) Reset(game);
	g_lastClock = clock;
	Sample(game, clock);
}

std::shared_ptr<const StatsSnapshot> GameStatsSnapshot()
{
	if (g_dirty || !g_snapshot) {
		auto fresh = std::make_shared<StatsSnapshot>();
		fresh->history = g_history;
		for (int index = 0; index < kStatMaxPlayers; index++) {
			if (g_seen[index]) fresh->players.push_back(g_info[index]);
		}
		g_snapshot = fresh;
		g_dirty = false;
	}
	return g_snapshot;
}
