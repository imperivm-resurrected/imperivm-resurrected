// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "addr/state.h"
#include "addr/world.h"
#include "core/framefade.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/gamelayout.h"
#include "game/seasons.h"
#include "mods/modoptions.h"

namespace {

// CVXBasicGame+0x174: 1 spring, 2 autumn, 3 winter (SetSeason ignores 0).
const size_t kGameSeason = 0x174;
const int kCycle[3] = {1, 2, 3};
const char *const kNames[4] = {"none", "spring", "autumn", "winter"};
// The rule's values: off, then every 10 seconds (to try it), 10 or 20 minutes of the match.
const uint32_t kIntervals[4] = {0, 10000, 10 * 60000, 20 * 60000};
const unsigned kFadeMs = 2500;
// How long the change waits for the renderer to keep the picture before it.
const DWORD kHoldWaitMs = 100;

typedef const char *(__fastcall *SetSeason_t)(void *game, void *edx, int season);

// The season the match had at its clock's 0, worked out from the one it has the first time it is seen: every computer, a rejoin and
// a loaded game land on the same seasons at the same time of the match.
struct Match {
	const char *game = nullptr;
	int baseIndex = 0;
	bool failed = false;
};
Match g_match;

int IndexOf(int season)
{
	for (int i = 0; i < 3; i++) {
		if (kCycle[i] == season) return i;
	}
	return -1;
}

uint32_t Interval()
{
	const int v = ModOptionValue(kDynamicSeasonsOptionId);
	return v > 0 && v < 4 ? kIntervals[v] : 0;
}

// The season the match should show now, 0 when none is due.
int Due(char *&gameOut)
{
	gameOut = nullptr;
	const uint32_t interval = Interval();
	if (!interval || *reinterpret_cast<int *>(addr::kEditorMode)) return 0;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !*reinterpret_cast<void **>(addr::kMap)) return 0;
	const int now = *reinterpret_cast<int *>(game + kGameSeason);
	const uint32_t turns = *reinterpret_cast<uint32_t *>(game + layout::kGameClock) / interval;
	if (g_match.game != game) {
		const int at = IndexOf(now);
		if (at < 0) return 0;
		g_match = Match();
		g_match.game = game;
		g_match.baseIndex = static_cast<int>((at + 3 - turns % 3) % 3);
		LogI("game", "seasons: dynamic, every %u s; %s now", interval / 1000, kNames[now]);
	}
	if (g_match.failed) return 0;
	const int want = kCycle[(g_match.baseIndex + turns) % 3];
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
	// The picture before the change stays on screen while the game reloads the terrain, and then fades into the new one.
	const unsigned hold = FrameFadeHold();
	const DWORD asked = GetTickCount();
	while (!FrameFadeTaken(hold) && GetTickCount() - asked < kHoldWaitMs) Sleep(1);
	const DWORD t0 = GetTickCount();
	const char *error = reinterpret_cast<SetSeason_t>(addr::kSetSeason)(game, nullptr, want);
	FrameFadeRelease(kFadeMs);
	if (error) {
		LogE("game", "seasons: the change to %s failed; the seasons stop changing in this match", kNames[want]);
		g_match.failed = true;
		return;
	}
	LogI("game", "seasons: %s -> %s in %lu ms", kNames[before >= 0 && before < 4 ? before : 0], kNames[want], GetTickCount() - t0);
}
