// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The caches the game keeps its interface images in (see game/artcache.h).
namespace addr {

// CCachingRLCache::Trim(this = g_pResourceManager, uint size): frees the unreferenced entries, least recently used first, until the cache holds no more than size bytes.
inline constexpr uintptr_t kResourceCacheTrim = 0x006591a0;
// CCachingRLCache::FindEntry(this, type, name): the entry of the cache for a resource (type 9 is a bitmap), or null. The name is compared without case.
inline constexpr uintptr_t kResourceCacheFindEntry = 0x00659220;
// AcquireCachedFile(name, int *error): the entry {CBitmap *, refcount} of a file by name, loading it the first time; the entry counts one reference.
inline constexpr uintptr_t kAcquireCachedFile = 0x0067d5c0;
// The word the cache of files compares with the count of zero-reference releases (a purge of every entry with no reference runs when the count goes above it).
inline constexpr uintptr_t kFileCacheGcThreshold = 0x0082a454;
// The race the command-bar frame bitmap (gameini/cmdbar/frames.ini) was loaded for; another race, or -2, makes the next button image load it again.
inline constexpr uintptr_t kCmdBarFrameRace = 0x0082e7a0;
// The CCachingRLCache of the interface (g_pResourceManager): a pointer. Its total of unreferenced bytes is the word 200 bytes into it.
inline constexpr uintptr_t kResourceManager = 0x00a08360;
// The count of releases that left an entry of the file cache with no reference.
inline constexpr uintptr_t kFileCacheReleaseCounter = 0x00a6e660;
// The by-entry map of the file cache (std::map<entry *, node of the by-name map>): a pointer to its head node, and the entries it holds.
inline constexpr uintptr_t kFileCacheByPtrHead = 0x00a6e674;
inline constexpr uintptr_t kFileCacheByPtrCount = 0x00a6e678;

}
