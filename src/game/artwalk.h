// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// One entry of the game's file cache: the name it was loaded under and how many users hold it.
const unsigned kArtNameChars = 256;
struct ArtRawEntry {
	char name[kArtNameChars];
	int refs;
};

// Reads the by-entry map of the game's file cache (g_fileCacheByPtr, a red-black tree of the VS2003 std::map: left, parent, right, key = the entry
// {bitmap, references}, value = the node of the by-name map, whose std::string is the name) from its head node, and copies up to capacity entries.
// The tree is the game's, in a 32-bit process: the words are 32 bits. No fault handling here (the caller wraps it).
unsigned ArtWalkFileCache(const uint32_t *head, ArtRawEntry *out, unsigned capacity);
