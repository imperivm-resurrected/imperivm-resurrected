// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/gamelayout.h"
#include "game/seasons.h"

namespace {

// CVXBasicGame+0x174: 1 spring, 2 autumn, 3 winter (SetSeason ignores 0).
const size_t kGameSeason = 0x174;
const int kCycle[3] = {1, 2, 3};
const char *const kNames[4] = {"none", "spring", "autumn", "winter"};
// A test of how the change looks and costs: every 10 s of the match.
const uint32_t kSeasonMs = 10000;

typedef const char *(__fastcall *SetSeason_t)(void *game, void *edx, int season);

struct Match {
	const char *game = nullptr;
	int startIndex = 0;
	uint32_t startClock = 0;
};
Match g_match;

int IndexOf(int season)
{
	for (int i = 0; i < 3; i++) {
		if (kCycle[i] == season) return i;
	}
	return -1;
}

// The season the match should show now, 0 when none is due.
int Due(char *&gameOut)
{
	gameOut = nullptr;
	if (!GetConfig().dynamicSeasons || *reinterpret_cast<int *>(addr::kEditorMode)) return 0;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) return 0;
	const int now = *reinterpret_cast<int *>(game + kGameSeason);
	const uint32_t clock = *reinterpret_cast<uint32_t *>(game + layout::kGameClock);
	if (g_match.game != game) {
		const int at = IndexOf(now);
		if (at < 0) return 0;
		g_match.game = game;
		g_match.startIndex = at;
		g_match.startClock = clock;
	}
	const uint32_t passed = clock >= g_match.startClock ? clock - g_match.startClock : 0;
	const int want = kCycle[(g_match.startIndex + passed / kSeasonMs) % 3];
	if (want == now) return 0;
	gameOut = game;
	return want;
}

}

void SeasonsGameTick()
{
	char *game = nullptr;
	if (Due(game)) UiWakeGameThread();
}

void SeasonsQueueTick()
{
	char *game = nullptr;
	const int want = Due(game);
	if (!want) return;
	const int before = *reinterpret_cast<int *>(game + kGameSeason);
	const DWORD t0 = GetTickCount();
	const char *error = reinterpret_cast<SetSeason_t>(addr::kSetSeason)(game, nullptr, want);
	if (error) {
		LogE("game", "seasons: the change to %s failed; dynamic seasons stop for this match", kNames[want]);
		g_match.game = nullptr;
		MutableConfig().dynamicSeasons = false;
		return;
	}
	LogI("game", "seasons: %s -> %s in %lu ms", kNames[before >= 0 && before < 4 ? before : 0], kNames[want], GetTickCount() - t0);
}
