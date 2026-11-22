// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

// Respawn timers: the sites of the map where items come back (poison mushrooms, healing water, the loot of the ruins) are marked
// on the big map once the player's troops have seen them, with the time left until their item is back. Based on the learning
// mode of a community build (game/resourcealerts there). Only what the player's own troops saw counts: a site emptied under the
// fog shows no time, as nobody saw when. In multiplayer the room's rule decides whether the timers are allowed (kRuleNoTimers).

enum RespawnKind : unsigned { kRespawnNone = 0, kRespawnMushrooms = 1, kRespawnWater = 2, kRespawnRuin = 4 };

// The kind of a map object by its class id.
constexpr RespawnKind RespawnKindOf(std::string_view classId)
{
	if (classId == "StonePile") return kRespawnMushrooms;
	if (classId == "OakStump" || classId == "Tombstone" || classId == "TombStone") return kRespawnWater;
	if (classId == "Ruins1") return kRespawnRuin;
	return kRespawnNone;
}

// The ruins' script fills them again ten game minutes after they were emptied; a hero inside can delay it, so that time is shown
// as approximate.
constexpr unsigned kRuinRefillMs = 600000;

// What the player knows of one site, sample after sample.
struct RespawnSiteState {
	const void *site = nullptr;
	bool discovered = false;
	bool stocked = false;
	// Game time (ms) its item is back, 0 when unknown or already there; approximate for the ruins.
	unsigned due = 0;
	bool approximate = false;
	// Seen on the previous sample too: only then was its emptying seen.
	bool watching = false;
	// Seen with its item at least once: only such a site's item can be "back". The game fills the ruins a moment after the start, and
	// a site found already empty was emptied before the player knew it.
	bool everStocked = false;

	// `available`, `nativeDue` (the game's own respawn event, 0 when none) matter only while `visible`. True when a site seen
	// with its item, and then without it, is seen with it again.
	bool Observe(const void *source, bool visible, bool available, unsigned clock, unsigned nativeDue, RespawnKind kind)
	{
		if (site != source) *this = RespawnSiteState{source};
		const bool sawBefore = watching;
		watching = visible;
		if (!visible) return false;
		const bool back = everStocked && !stocked && available;
		if (available) {
			due = 0;
			approximate = false;
		}
		else if (kind == kRespawnRuin) {
			// Emptied before the player's eyes: the refill is due. Found empty, or emptied in the fog: nobody saw when.
			if (discovered && stocked && sawBefore) {
				due = clock + kRuinRefillMs;
				approximate = true;
			}
			else if (!discovered || stocked) {
				due = 0;
				approximate = false;
			}
		}
		else {
			due = nativeDue;
			approximate = false;
		}
		discovered = true;
		stocked = available;
		everStocked = everStocked || available;
		return back;
	}
};

struct RespawnMark {
	int x = 0, y = 0;
	RespawnKind kind = kRespawnNone;
	bool stocked = false;
	unsigned due = 0;
	bool approximate = false;
};

// Whole seconds until `due`, rounded up; 0 once it has come.
inline unsigned RespawnSecondsUntil(unsigned due, unsigned clock)
{
	if (due <= clock) return 0;
	const unsigned ms = due - clock;
	return ms / 1000 + (ms % 1000 != 0);
}

// "4:05", or "~9:58" for an approximate time.
inline void RespawnTimeText(unsigned seconds, bool approximate, char (&out)[16])
{
	snprintf(out, sizeof(out), "%s%u:%02u", approximate ? "~" : "", seconds / 60, seconds % 60);
}

// The colour of a site's mark, RGB555: green mushrooms, blue water, gold ruins; the same darker while it is empty.
inline unsigned RespawnMarkColor(RespawnKind kind, bool stocked)
{
	unsigned c = 0x7fff;
	switch (kind) {
	case kRespawnMushrooms: c = 0x7ee8; break;
	case kRespawnWater: c = 0x23ff; break;
	case kRespawnRuin: c = 0x6dff; break;
	default: break;
	}
	return stocked ? c : (c >> 1) & 0x3def;
}

// A value of [begin, end] on a pixel of [left, right]; false outside.
inline bool RespawnProjectAxis(int value, int begin, int end, int left, int right, int &pixel)
{
	if (end <= begin || right <= left || value < begin || value > end) return false;
	const int64_t span = static_cast<int64_t>(end) - begin;
	const int64_t distance = static_cast<int64_t>(value) - begin;
	pixel = left + static_cast<int>((distance * (right - left) + span / 2) / span);
	return true;
}

void InstallRespawnSites(bool knownExe);
// On the game thread, once per frame: samples the sites twice a second and says which items are back.
void RespawnSitesGameTick();
// Whether the timers are on now: the player's option, and in multiplayer the room's rule.
bool RespawnTimersOn();
// The marks of the sites discovered in this match, and the game's clock they were taken at.
const std::vector<RespawnMark> &RespawnMarks();
unsigned RespawnClock();
