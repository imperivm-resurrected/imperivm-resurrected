// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "addr/engine.h"
#include "core/config.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "game/memcensus.h"
#include "game/memtrace.h"

namespace {

const unsigned kMaxHeaps = 8;
// Block sizes are grouped: 64 bytes apart up to 64 KB (an entity or an image buffer shows as a size of its own), then one group per power of two.
const unsigned kSmallBins = 1024;
const unsigned kBins = kSmallBins + 16;
const unsigned kShownGrowth = 6;
// The walk of a very large heap can take long: if the first one is slower than this the census switches itself off.
const DWORD kTooSlowMs = 1500;
// Growth below this is not worth a line.
const long long kMinGrowth = 64 * 1024;

struct HeapCensus {
	HANDLE handle;
	bool isDefault;
	unsigned busyBlocks, freeBlocks;
	unsigned long long busyBytes, freeBytes, committed;
	unsigned count[kBins];
	unsigned bytes[kBins];
};

struct Census {
	bool valid;
	DWORD ms;
	unsigned heaps, heapsNotCounted;
	HeapCensus heap[kMaxHeaps];
	unsigned sbhRegions;
	unsigned long long sbhCommitted, heapsCommitted, privateCommit, mappedCommit;
};

// Static: a census is some 70 KB per slot and is taken with a heap locked, where nothing may allocate.
Census g_now;
Census g_slot[static_cast<unsigned>(CensusPoint::kCount)];
bool g_off = false;

unsigned BinOf(SIZE_T size)
{
	if (size < 65536) return static_cast<unsigned>(size >> 6);
	unsigned log = 16;
	while (log < 31 && (static_cast<SIZE_T>(1) << (log + 1)) <= size) log++;
	return kSmallBins + (log - 16);
}

// Walks one heap with it locked. Only plain data in here: nothing may allocate while the lock is held.
bool WalkHeap(HANDLE heap, HeapCensus &out, unsigned heapIndex)
{
	bool ok = true;
	const bool locked = HeapLock(heap) != FALSE;
	__try {
		PROCESS_HEAP_ENTRY entry;
		entry.lpData = nullptr;
		while (HeapWalk(heap, &entry)) {
			if (entry.wFlags & PROCESS_HEAP_REGION) {
				out.committed += entry.Region.dwCommittedSize;
			}
			else if (entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) {
				continue;
			}
			else if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
				out.busyBlocks++;
				out.busyBytes += entry.cbData;
				const unsigned bin = BinOf(entry.cbData);
				out.count[bin]++;
				out.bytes[bin] += entry.cbData;
				if (entry.cbData >= kMemTraceLarge) MemTraceNote(entry.lpData, entry.cbData, heapIndex);
			}
			else {
				out.freeBlocks++;
				out.freeBytes += entry.cbData;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	if (locked) HeapUnlock(heap);
	return ok;
}

// Committed bytes of the address range [base, base + size).
unsigned long long CommittedIn(uintptr_t base, uintptr_t size)
{
	unsigned long long total = 0;
	uintptr_t at = base;
	const uintptr_t end = base + size;
	while (at < end) {
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) == 0) break;
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		if (info.State == MEM_COMMIT) total += (regionEnd < end ? regionEnd : end) - at;
		if (regionEnd <= at) break;
		at = regionEnd;
	}
	return total;
}

void ReadSmallBlockHeap(Census &c)
{
	__try {
		const unsigned count = *reinterpret_cast<const unsigned *>(addr::kSbhRegionCount);
		const uintptr_t list = *reinterpret_cast<const uintptr_t *>(addr::kSbhRegionList);
		if (!list || count > 4096) return;
		for (unsigned i = 0; i < count; i++) {
			const uintptr_t base = *reinterpret_cast<const uintptr_t *>(list + i * 20 + 12);
			if (!base) continue;
			c.sbhRegions++;
			c.sbhCommitted += CommittedIn(base, 0x100000);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		c.sbhRegions = 0;
		c.sbhCommitted = 0;
	}
}

void ReadAddressSpace(Census &c)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	uintptr_t at = 0;
	const uintptr_t end = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
	while (at < end) {
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) == 0) break;
		if (info.State == MEM_COMMIT) {
			if (info.Type == MEM_PRIVATE) c.privateCommit += info.RegionSize;
			else if (info.Type == MEM_MAPPED) c.mappedCommit += info.RegionSize;
		}
		const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		if (next <= at) break;
		at = next;
	}
}

void Take(Census &c)
{
	memset(&c, 0, sizeof(c));
	MemTraceBegin();
	const DWORD start = GetTickCount();
	HANDLE handles[64];
	const DWORD total = GetProcessHeaps(64, handles);
	const HANDLE process = GetProcessHeap();
	for (DWORD i = 0; i < total && i < 64; i++) {
		if (c.heaps == kMaxHeaps) {
			c.heapsNotCounted++;
			continue;
		}
		HeapCensus &h = c.heap[c.heaps];
		h.handle = handles[i];
		h.isDefault = handles[i] == process;
		WalkHeap(handles[i], h, c.heaps);
		c.heapsCommitted += h.committed;
		c.heaps++;
	}
	ReadSmallBlockHeap(c);
	ReadAddressSpace(c);
	c.ms = GetTickCount() - start;
	c.valid = true;
}

double Mb(long long bytes)
{
	return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

const HeapCensus *Find(const Census &c, HANDLE handle)
{
	for (unsigned i = 0; i < c.heaps; i++) {
		if (c.heap[i].handle == handle) return &c.heap[i];
	}
	return nullptr;
}

void LogSummary(const char *when, const Census &c)
{
	char line[1200];
	int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "census (%s): ", when);
	for (unsigned i = 0; i < c.heaps && n > 0 && n < 1000; i++) {
		const HeapCensus &h = c.heap[i];
		const int written = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "heap %u%s %.1f MB in %u blocks, free %.1f MB, committed %.1f MB; ", i,
			h.isDefault ? " (process default)" : "", Mb(static_cast<long long>(h.busyBytes)), h.busyBlocks, Mb(static_cast<long long>(h.freeBytes)),
			Mb(static_cast<long long>(h.committed)));
		n = written < 0 ? static_cast<int>(strlen(line)) : n + written; // a truncated line returns -1
	}
	const long long other = static_cast<long long>(c.privateCommit) - static_cast<long long>(c.heapsCommitted) - static_cast<long long>(c.sbhCommitted);
	if (n > 0) {
		LogI("game", "%s", line);
	}
	LogI("game", "census (%s): game's small-block heap %u regions, %.1f MB committed; private commit %.1f MB of which outside the heaps and that %.1f MB; file mappings %.1f MB; "
		"%u heaps not counted (%lu ms)", when, c.sbhRegions, Mb(static_cast<long long>(c.sbhCommitted)), Mb(static_cast<long long>(c.privateCommit)), Mb(other),
		Mb(static_cast<long long>(c.mappedCommit)), c.heapsNotCounted, static_cast<unsigned long>(c.ms));
}

struct Growth {
	long long bytes;
	long long blocks;
	unsigned heap, bin;
};

void LogGrowth(const char *when, const char *since, const Census &now, const Census &before)
{
	Growth top[kShownGrowth] = {};
	unsigned shown = 0;
	long long sumGrow = 0, sumShrink = 0;
	for (unsigned i = 0; i < now.heaps; i++) {
		const HeapCensus &h = now.heap[i];
		const HeapCensus *old = Find(before, h.handle);
		for (unsigned bin = 0; bin < kBins; bin++) {
			const long long dBytes = static_cast<long long>(h.bytes[bin]) - (old ? static_cast<long long>(old->bytes[bin]) : 0);
			const long long dBlocks = static_cast<long long>(h.count[bin]) - (old ? static_cast<long long>(old->count[bin]) : 0);
			if (dBytes > 0) sumGrow += dBytes;
			else sumShrink += dBytes;
			if (dBytes < kMinGrowth) continue;
			// Keep the largest few, in order.
			if (shown == kShownGrowth && dBytes <= top[shown - 1].bytes) continue;
			unsigned at = shown < kShownGrowth ? shown : kShownGrowth - 1;
			while (at > 0 && top[at - 1].bytes < dBytes) {
				top[at] = top[at - 1];
				at--;
			}
			top[at].bytes = dBytes;
			top[at].blocks = dBlocks;
			top[at].heap = i;
			top[at].bin = bin;
			if (shown < kShownGrowth) shown++;
		}
	}
	const long long sbh = static_cast<long long>(now.sbhCommitted) - static_cast<long long>(before.sbhCommitted);
	const long long outside = (static_cast<long long>(now.privateCommit) - static_cast<long long>(now.heapsCommitted) - static_cast<long long>(now.sbhCommitted)) -
		(static_cast<long long>(before.privateCommit) - static_cast<long long>(before.heapsCommitted) - static_cast<long long>(before.sbhCommitted));
	LogI("game", "census (%s) since %s: blocks in use in the heaps %+.1f MB (%.1f MB more sizes grew, %.1f MB fewer); small-block heap committed %+.1f MB (%+d regions); "
		"outside the heaps %+.1f MB; file mappings %+.1f MB", when, since, Mb(sumGrow + sumShrink), Mb(sumGrow), Mb(-sumShrink), Mb(sbh),
		static_cast<int>(now.sbhRegions) - static_cast<int>(before.sbhRegions), Mb(outside),
		Mb(static_cast<long long>(now.mappedCommit) - static_cast<long long>(before.mappedCommit)));
	for (unsigned k = 0; k < shown; k++) {
		const Growth &g = top[k];
		if (g.bin < kSmallBins) {
			LogI("game", "census (%s) since %s:   heap %u, blocks of %u-%u bytes: %+lld blocks, %+.2f MB", when, since, g.heap, g.bin << 6, (g.bin << 6) + 63, g.blocks,
				Mb(g.bytes));
		}
		else {
			LogI("game", "census (%s) since %s:   heap %u, blocks of %u KB or more (up to twice that): %+lld blocks, %+.2f MB", when, since, g.heap,
				(1u << (g.bin - kSmallBins + 16)) >> 10, g.blocks, Mb(g.bytes));
		}
	}
}

const char *NameOf(CensusPoint p)
{
	switch (p) {
	case CensusPoint::kBeforeReload: return "before the reload";
	case CensusPoint::kAfterShutdown: return "after Shutdown";
	case CensusPoint::kAfterInit: return "after Init";
	case CensusPoint::kAfterLoad: return "after LoadGameData";
	case CensusPoint::kBeforeUnload: return "before UnloadGameData";
	case CensusPoint::kAfterUnload: return "after UnloadGameData";
	default: return "?";
	}
}

}

void MemoryCensus(CensusPoint at, const char *when, CensusPoint compare, CensusPoint also)
{
	// A diagnostic: it walks every heap at each step of every game, so it is off unless [Debug] MemoryTrace=1 is in imperivm-mod.ini.
	if (!GetConfig().memoryTrace) return;
	static volatile LONG running = 0;
	if (g_off || InterlockedExchange(&running, 1)) return;
	g_crashLogQuiet++;
	Take(g_now);
	if (g_now.ms > kTooSlowMs) {
		LogW("game", "census: it took %lu ms; it is switched off for the rest of the session", static_cast<unsigned long>(g_now.ms));
		g_off = true;
	}
	LogSummary(when, g_now);
	// The large blocks that are alive, by game and by the code that made them: what the engine keeps for ever shows up as old games.
	if (at == CensusPoint::kBeforeReload) MemTraceReport(when, TraceAt::kSummary);
	else if (at == CensusPoint::kAfterShutdown) MemTraceReport(when, TraceAt::kShutdown);
	else if (at == CensusPoint::kAfterLoad) MemTraceReport(when, TraceAt::kLoad);
	else if (at == CensusPoint::kAfterUnload) MemTraceReport(when, TraceAt::kUnload);
	for (const CensusPoint other : {compare, also}) {
		if (other == CensusPoint::kNone) continue;
		const Census &before = g_slot[static_cast<unsigned>(other)];
		if (before.valid) LogGrowth(when, NameOf(other), g_now, before);
	}
	if (at != CensusPoint::kNone) memcpy(&g_slot[static_cast<unsigned>(at)], &g_now, sizeof(Census));
	g_crashLogQuiet--;
	InterlockedExchange(&running, 0);
}
