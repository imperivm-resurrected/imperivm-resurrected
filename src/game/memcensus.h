// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Where the memory of the process is, to find what a ruleset switch leaves behind. Each call walks every heap of the process (and the game's
// small-block heap regions) and writes to the log, by heap: the bytes and blocks in use, the free ones and what the heap has committed; and,
// against the census taken at another point, which sizes of block grew. Nothing here changes the game: it only reads.
enum class CensusPoint {
	kNone,
	kBeforeReload,   // the match is about to read the data again
	kAfterShutdown,  // ShutdownGameDataSystems and the entity cache are freed
	kAfterInit,      // InitGameDataSystems has made its tables
	kAfterLoad,      // LoadGameData has read the classes
	kBeforeUnload,   // the match ended, the engine has not freed its data yet
	kAfterUnload,    // UnloadGameData is done
	kCount
};

// Takes a census, writes it, and compares it with the one stored at `compare` (and at `also`, if given). Then stores it at `at`.
void MemoryCensus(CensusPoint at, const char *when, CensusPoint compare, CensusPoint also = CensusPoint::kNone);
