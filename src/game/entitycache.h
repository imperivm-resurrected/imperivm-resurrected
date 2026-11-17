// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The game keeps every entity it loads (LoadEntity: the .ent.xml of a unit, building, map object or visual with all the pictures and animations it names)
// under the name of the class that asked for it, until it exits or the season changes; since the reload keeps the cache (see game/hotreload.cpp) a ruleset
// that changes the art of an entity would be handed the old one. Every file of an entity is in the folder of its .ent.xml, so the reload takes out of the
// cache (with the game's own UnloadEntity, which frees what the entity holds) the entities of the folders where a file changed, and nothing else.
struct EntityInvalidation {
	bool ok = false;
	unsigned cached = 0;                 // entities in the cache
	std::vector<std::string> unloaded;   // the names taken out
	// One of the entities to take out is a decoration, which the passability grid and the map view keep a pointer to (the array g_decorEntities) until the cache
	// is emptied for a new season: nothing was taken out, and the whole cache has to be emptied (and the decor array and its marks set back) instead.
	bool decorHit = false;
};

// folders: lowercase, "/" (see HotSwapEntityFolders). Between the tables being freed and the classes being read again: nothing holds an entity then but the
// decor array and the marks that go with it, which the caller sets back when something was unloaded.
EntityInvalidation InvalidateEntityFolders(const std::vector<std::string> &folders, const std::vector<unsigned> &decorEntities);
