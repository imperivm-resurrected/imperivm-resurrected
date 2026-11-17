// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "addr/artcache.h"
#include "addr/world.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "game/artcache.h"
#include "game/artwalk.h"
#include "mods/volumes.h"

namespace {
typedef void *(__fastcall *FindEntry_t)(void *self, void *edx, int type, const char *name);
typedef void(__fastcall *Trim_t)(void *self, void *edx, unsigned size);
typedef void *(__cdecl *Acquire_t)(const char *name, int *error);
typedef void(__cdecl *Release_t)(void *entry);

// The resource type of a bitmap in the cache of the interface (CResourceManager::Load, case 9).
const int kBitmapResource = 9;
const unsigned kEntryCapacity = 20000;

// The files the flush loads only to give back, so that the game's own purge runs (it runs when an entry's last reference is let go and the count of such
// releases passed the threshold): every one is a file of the game's interface that no mod has a reason to remove. The first that is not in use will do.
const char *const kPurgeFiles[] = {"gameres/cmdbar/_dummy_icon_.bmp", "gameres/common/plus.bmp", "gameres/common/uparrow.bmp", "gameres/common/downarrow.bmp",
	"gameres/common/check.bmp"};

std::mutex g_learnedLock;
std::set<std::string> g_learned;       // held by the file cache after the last flush
std::vector<std::string> g_survivors;  // the survivors of the last flush that were among the images that change

volatile unsigned *Word(uintptr_t address)
{
	return reinterpret_cast<volatile unsigned *>(address);
}

// The name the caches use, as a game path: lowercase, "/", the volume turned into its folder (gameres/ is ui/).
std::string Normal(const char *name)
{
	std::string s(name);
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return VolumeRealPath(s);
}

// Copies every name of the file cache with its references (see ArtWalkFileCache). Only plain data in here, so the __try can protect it. -1: a fault.
int WalkFileCache(ArtRawEntry *out, unsigned capacity)
{
	int n;
	g_crashLogQuiet++;
	__try {
		n = static_cast<int>(ArtWalkFileCache(*reinterpret_cast<const uint32_t *const *>(addr::kFileCacheByPtrHead), out, capacity));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		n = -1;
	}
	g_crashLogQuiet--;
	return n;
}

// Whether the cache of the interface has the bitmap that name asks for, and how many windows hold it. Only plain data in here.
bool FindResource(const char *name, bool *found, int *refs)
{
	bool ok = true;
	*found = false;
	*refs = 0;
	g_crashLogQuiet++;
	__try {
		void *cache = *reinterpret_cast<void *const *>(addr::kResourceManager);
		const void *entry = cache ? reinterpret_cast<FindEntry_t>(addr::kResourceCacheFindEntry)(cache, nullptr, kBitmapResource, name) : nullptr;
		if (entry) {
			*found = true;
			*refs = *reinterpret_cast<const short *>(static_cast<const char *>(entry) + 10);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	g_crashLogQuiet--;
	return ok;
}

// The names the interface's layouts may give a picture of a game path: with the volume, and with its real folder.
std::vector<std::string> ResourceNames(const std::string &real)
{
	std::vector<std::string> names = {real};
	if (real.compare(0, 3, "ui/") == 0) names.push_back("gameres/" + real.substr(3));
	return names;
}

// The bytes the interface cache holds in unreferenced entries (its LRU total, +0xc8), or 0.
unsigned ResourceBytes()
{
	unsigned bytes = 0;
	__try {
		const char *cache = *reinterpret_cast<const char *const *>(addr::kResourceManager);
		if (cache) bytes = *reinterpret_cast<const unsigned *>(cache + 0xc8);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		bytes = 0;
	}
	return bytes;
}

// The three actions on the game's own caches. Only plain data in here. Returns how many purge files it took: 0 means the purge did not run.
int EmptyCaches(unsigned *resourceBefore, unsigned *resourceAfter)
{
	int taken = 0;
	g_crashLogQuiet++;
	__try {
		*resourceBefore = ResourceBytes();
		// The cache of files frees its unreferenced entries when the count of releases that left an entry with none passes the threshold: put the count on
		// the threshold, load a file nothing uses and give it back, which is the release that passes it. The game frees every entry with no reference.
		for (unsigned i = 0; i < sizeof(kPurgeFiles) / sizeof(kPurgeFiles[0]); i++) {
			*Word(addr::kFileCacheReleaseCounter) = *Word(addr::kFileCacheGcThreshold);
			int error = 0;
			void *entry = reinterpret_cast<Acquire_t>(addr::kAcquireCachedFile)(kPurgeFiles[i], &error);
			if (!entry) continue;
			reinterpret_cast<Release_t>(addr::kReleaseCachedFile)(entry);
			// The purge sets the count back to 0 before it runs.
			if (*Word(addr::kFileCacheReleaseCounter) == 0) {
				taken = static_cast<int>(i) + 1;
				break;
			}
		}
		// The interface cache: every unreferenced entry goes.
		if (void *cache = *reinterpret_cast<void *const *>(addr::kResourceManager)) reinterpret_cast<Trim_t>(addr::kResourceCacheTrim)(cache, nullptr, 0);
		*resourceAfter = ResourceBytes();
		// The frame of the command-bar buttons is loaded again (and the old one freed by the game) the next time a button image is made.
		*reinterpret_cast<int *>(addr::kCmdBarFrameRace) = -2;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		taken = -1;
	}
	g_crashLogQuiet--;
	return taken;
}

std::string Join(const std::vector<std::string> &names, size_t count)
{
	std::string out;
	for (size_t i = 0; i < names.size() && i < count; i++) out += (i ? ", " : "") + names[i];
	if (names.size() > count) out += ", ...";
	return out;
}

// Every cached name with its references, or false when the cache could not be read.
bool Census(std::vector<std::pair<std::string, int>> &out)
{
	out.clear();
	std::vector<ArtRawEntry> raw(kEntryCapacity);
	const int n = WalkFileCache(raw.data(), kEntryCapacity);
	// A full buffer may have left entries out: that is not a census.
	if (n < 0 || static_cast<unsigned>(n) >= kEntryCapacity) return false;
	for (int i = 0; i < n; i++) out.emplace_back(Normal(raw[i].name), raw[i].refs);
	return true;
}
}

bool ArtCacheHeldForSwitch(const std::vector<std::string> &files, bool gameRunning, std::vector<std::string> &held)
{
	held.clear();
	for (const std::string &f : files) {
		bool isHeld;
		{
			std::lock_guard<std::mutex> lock(g_learnedLock);
			isHeld = g_learned.count(f) != 0;
		}
		// What the windows of a running game hold is not what stays: only the menus' windows are there when no game runs.
		if (!isHeld && !gameRunning) {
			for (const std::string &name : ResourceNames(f)) {
				bool found = false;
				int refs = 0;
				if (!FindResource(name.c_str(), &found, &refs)) return false;
				if (found && refs > 0) isHeld = true;
			}
		}
		if (isHeld) held.push_back(f);
	}
	return true;
}

void ArtCacheFlush(const std::vector<std::string> &changed)
{
	g_survivors.clear();
	const DWORD start = GetTickCount();
	std::vector<std::pair<std::string, int>> before;
	const bool readBefore = Census(before);
	unsigned heldBefore = 0;
	for (const auto &e : before) heldBefore += e.second > 0;
	unsigned resourceBefore = 0, resourceAfter = 0;
	const int purge = EmptyCaches(&resourceBefore, &resourceAfter);
	std::vector<std::pair<std::string, int>> after;
	const bool readAfter = Census(after);
	// The survivors: of the images that change, those whose name is still in a cache. The purge freed every entry with no reference, so each one left has one
	// (or the purge did not run, which is the same for the new picture: the old one would be handed back).
	std::set<std::string> learned;
	unsigned heldAfter = 0;
	std::vector<std::string> stale;
	for (const auto &e : after) {
		if (e.second > 0) {
			learned.insert(e.first);
			heldAfter++;
		}
		if (std::find(changed.begin(), changed.end(), e.first) != changed.end() && std::find(stale.begin(), stale.end(), e.first) == stale.end()) stale.push_back(e.first);
	}
	for (const std::string &f : changed) {
		for (const std::string &name : ResourceNames(f)) {
			bool found = false;
			int refs = 0;
			if (!FindResource(name.c_str(), &found, &refs)) {
				stale.push_back(f);
				break;
			}
			if (found) {
				stale.push_back(f);
				break;
			}
		}
	}
	// A cache that could not be read, or a purge that did not run, is not knowing: every image that changes counts as held.
	if (!readBefore || !readAfter || purge <= 0) {
		stale = changed;
		LogE("game", "hot reload: the image caches could not be emptied or read (read before: %s, after: %s, purge: %d); the %u images that change count as in use",
			readBefore ? "yes" : "no", readAfter ? "yes" : "no", purge, static_cast<unsigned>(changed.size()));
	}
	{
		std::lock_guard<std::mutex> lock(g_learnedLock);
		g_learned.swap(learned);
	}
	std::sort(stale.begin(), stale.end());
	stale.erase(std::unique(stale.begin(), stale.end()), stale.end());
	g_survivors = stale;
	LogI("game", "hot reload: image caches emptied in %lu ms: files %u entries (%u in use) -> %u (%u in use), interface %u -> %u bytes unreferenced, command-bar frame set to load again, "
		"purge file %d; %u images change, %u of them still cached%s%s",
		static_cast<unsigned long>(GetTickCount() - start), static_cast<unsigned>(before.size()), heldBefore, static_cast<unsigned>(after.size()),
		heldAfter, resourceBefore, resourceAfter, purge, static_cast<unsigned>(changed.size()), static_cast<unsigned>(g_survivors.size()),
		g_survivors.empty() ? "" : ": ", Join(g_survivors, 8).c_str());
}

const std::vector<std::string> &ArtCacheSurvivors()
{
	return g_survivors;
}

void ArtCacheLogCensus(const char *when)
{
	std::vector<std::pair<std::string, int>> entries;
	if (!Census(entries)) {
		LogW("game", "art cache (%s): the file cache could not be read", when);
		return;
	}
	unsigned held = 0;
	for (const auto &e : entries) held += e.second > 0;
	LogI("game", "art cache (%s): file cache %u entries (%u in use; the game counts %u), interface cache %u bytes unreferenced", when, static_cast<unsigned>(entries.size()), held,
		*Word(addr::kFileCacheByPtrCount), ResourceBytes());
}
