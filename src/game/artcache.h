// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The caches the game keeps the images of its interface in, which a ruleset that brings other images has to empty (the game empties them only at exit):
//   - the cache of files (AcquireCachedFile): one entry {bitmap, references} per file NAME, kept after its last user lets go and handed back to the next
//     one that asks for that name. Command icons, item, skill and class icons, unit portraits and queue icons, and the cost icons of the texts use it.
//   - the cache of the interface (g_pResourceManager, a CCachingRLCache): the images, INIs and fonts that the windows' layouts name; the ones nobody uses
//     wait in a list that is only trimmed when it passes 512 KB.
//   - the frame of the command-bar buttons (gameini/cmdbar/frames.ini), loaded once per race.
// None of them is a surface: the game draws from bitmaps in memory.
// ArtCacheFlush makes the game's own code free what nothing uses. What something still uses is not touched (the game would be left with a picture it
// is drawing from freed memory): if the name of an image that changes is among those, the game restarts instead of showing the old picture.

// For ModsSetArtProbe: which of the images a switch replaces (game paths ui/..., lowercase) are held now, as far as it can be known before the caches
// are emptied. Held by the resource cache: asked live (when no game is running, what its windows hold is what stays). Held by the file cache: what
// the last flush found still in use (the tables and the windows of the match that is gone also hold images, and they are freed by the flush: the count
// of references alone cannot tell them from what stays). False: the caches could not be read.
bool ArtCacheHeldForSwitch(const std::vector<std::string> &files, bool gameRunning, std::vector<std::string> &held);

// Empties what nothing holds, then checks the images the switch replaces (game paths): the ones whose name is still in a cache are the survivors, in use by
// something that outlives the reload. Runs between ShutdownGameDataSystems and InitGameDataSystems, when the tables and the old match's windows have let go.
// Logs what it did, with the numbers to compare from one switch to the next.
void ArtCacheFlush(const std::vector<std::string> &changed);

// What ArtCacheFlush found: the names that are still cached and cannot be replaced. Empty: the new images will be the ones loaded.
const std::vector<std::string> &ArtCacheSurvivors();

// One line with what the caches hold, for the memory lines of a game (MemoryTrace) and for the switches.
void ArtCacheLogCensus(const char *when);
