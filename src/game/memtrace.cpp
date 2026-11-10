// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "addr/engine.h"
#include "core/log.h"
#include "game/memtrace.h"
#include "hooks/hookcount.h"

namespace {

const unsigned kSites = 5;
const unsigned kSlots = 1u << 15;
const unsigned kProbe = 64;
// The table is cleared when it is this full: a diagnostic that loses old blocks, not one that grows.
const unsigned kMostEntries = 24000;
const unsigned kGroups = 512;
const unsigned kStackWords = 160;
const unsigned kHeaps = 8;
const unsigned kUnseenKept = 4096;
const unsigned kDumped = 40;
const unsigned kReferrers = 3;
const unsigned kTargets = 64;
// The blocks the heap functions of Windows are followed for: those of the size the leak has, and larger.
const size_t kHeapLarge = 0x20000;
// The Wine heap keeps blocks of up to 32 KB in groups of 31 blocks that it allocates itself (a "low fragmentation heap" group), and the census sees
// the group, not the blocks inside: the blocks that leak show as groups of 269840 bytes (31 blocks of 8704 bytes and a header of 16: a request of
// 8185 to 8696 bytes). So the blocks of that size are followed one by one, from malloc until free.
const size_t kWindowLow = 8184;
const size_t kWindowHigh = 8700;
const unsigned kLiveSlots = 1u << 14;
const unsigned kMostLive = 12000;
// The data of the executable (the globals of the game) and the end of its image, where a pointer to a block can be kept.
const uintptr_t kDataBegin = addr::kGameRdataEnd;
const uintptr_t kDataEnd = addr::kGameImageEnd;

// Which path asked for the block: the game's own allocator functions, or a heap function of Windows (which any module of the process can
// call: the game's runtime ends in them, and so do the DLLs it loads).
enum Path : unsigned {
	kPathMalloc = 1,
	kPathRealloc,
	kPathCalloc,
	kPathHeapAlloc,
	kPathHeapRealloc
};

const char *PathName(unsigned path)
{
	switch (path) {
	case kPathMalloc: return "game malloc";
	case kPathRealloc: return "game realloc";
	case kPathCalloc: return "game calloc";
	case kPathHeapAlloc: return "HeapAlloc";
	case kPathHeapRealloc: return "HeapReAlloc";
	default: return "?";
	}
}

struct Record {
	uintptr_t block;
	size_t size;
	unsigned game;
	unsigned path;
	uintptr_t site[kSites];
};

struct Group {
	uintptr_t site[3];
	unsigned game;
	unsigned path;
	unsigned heap;
	unsigned blocks;
	unsigned long long bytes;
	// A record for the address exists but the block is not the size that was asked for: the size of the first block of the group that asked.
	bool sizeDiffers;
	size_t recordedSize;
};

// A block alive in a heap, 128 KB or more, that no hook was seen to allocate (there is no record for its address).
struct Unseen {
	uintptr_t block;
	size_t size;
	unsigned heap;
};

// Who points to a block: a global of the executable, or another block of a heap (its address, size and first word, which is a vtable for an object).
struct Referrer {
	unsigned target;
	uintptr_t at;
	uintptr_t holder;
	size_t holderSize;
	unsigned holderHeap;
	unsigned holderFirstWord;
};

// A block of the size of the leaking group that is alive: who asked for it, and in which game.
struct Live {
	uintptr_t block;
	unsigned size;
	unsigned game;
	uintptr_t site[3];
};
const uintptr_t kDeadSlot = 1;

// All static: the hooks run inside the allocator, and the census asks with a heap locked.
Record g_table[kSlots];
unsigned g_entries = 0;
volatile LONG g_lock = 0;
volatile LONG g_game = 0;
bool g_gameReloaded = false;

Live g_live[kLiveSlots];
volatile LONG g_liveCount = 0;
unsigned g_liveReset = 0;

Group g_group[kGroups];
unsigned g_groups = 0;
unsigned g_groupsLost = 0;
unsigned g_noRecord[kHeaps] = {};
unsigned long long g_noRecordBytes[kHeaps] = {};
Unseen g_unseen[kUnseenKept];
unsigned g_unseenCount = 0;
bool g_unseenFull = false;
// The unseen blocks after Shutdown (what the reload starts from) and at the end of the previous game.
Unseen g_unseenShutdown[kUnseenKept], g_unseenUnload[kUnseenKept];
unsigned g_unseenShutdownCount = 0, g_unseenUnloadCount = 0;
bool g_haveShutdown = false, g_haveUnload = false;
Unseen g_target[kTargets];
Referrer g_referrer[kTargets * kReferrers];
unsigned g_referrers = 0;

typedef void *(__cdecl *Malloc_t)(size_t size);
typedef void *(__cdecl *Realloc_t)(void *block, size_t size);
typedef void *(__cdecl *Calloc_t)(size_t count, size_t size);
typedef void(__cdecl *Free_t)(void *block);
Free_t g_origFree = nullptr;
Malloc_t g_origMalloc = nullptr;
Realloc_t g_origRealloc = nullptr;
Calloc_t g_origCalloc = nullptr;
typedef PVOID(WINAPI *HeapAllocFn)(PVOID heap, ULONG flags, SIZE_T size);
typedef PVOID(WINAPI *HeapReAllocFn)(PVOID heap, ULONG flags, PVOID block, SIZE_T size);
HeapAllocFn g_origHeapAlloc = nullptr;
HeapReAllocFn g_origHeapReAlloc = nullptr;

void Lock()
{
	while (InterlockedCompareExchange(&g_lock, 1, 0) != 0) YieldProcessor();
}

void Unlock()
{
	InterlockedExchange(&g_lock, 0);
}

unsigned SlotOf(uintptr_t block)
{
	return static_cast<unsigned>((block >> 4) * 2654435761u) & (kSlots - 1);
}

// A value on the stack that could be the return address of a call into the game's code: right after a call instruction.
bool LooksLikeReturn(uintptr_t v)
{
	if (v < addr::kGameTextBegin || v >= addr::kGameTextEnd) return false;
	const unsigned char *code = reinterpret_cast<const unsigned char *>(v);
	return code[-5] == 0xE8 || code[-6] == 0xFF || code[-2] == 0xFF || code[-3] == 0xFF;
}

// The first return addresses of the game's code found going up the stack, whatever system frames are in between (a walk of the frames
// does not work under Wine); and the address the hook was called from, in case no code of the game is on the stack (a DLL asked).
void Capture(uintptr_t *site, void *returnAddressSlot)
{
	const uintptr_t *sp = static_cast<const uintptr_t *>(returnAddressSlot);
	const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
	const uintptr_t *top = static_cast<const uintptr_t *>(tib->StackBase);
	unsigned found = 0;
	for (unsigned i = 0; i < kStackWords && sp + i < top && found < kSites; i++) {
		if (LooksLikeReturn(sp[i])) site[found++] = sp[i];
	}
	if (!found && sp < top) site[0] = sp[0];
}

void Remember(void *block, size_t size, unsigned path, void *returnAddressSlot)
{
	Record fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.block = reinterpret_cast<uintptr_t>(block);
	fresh.size = size;
	fresh.game = static_cast<unsigned>(g_game);
	fresh.path = path;
	Capture(fresh.site, returnAddressSlot);
	Lock();
	if (g_entries >= kMostEntries) {
		memset(g_table, 0, sizeof(g_table));
		g_entries = 0;
	}
	unsigned at = SlotOf(fresh.block);
	for (unsigned i = 0; i < kProbe; i++) {
		const unsigned slot = (at + i) & (kSlots - 1);
		if (g_table[slot].block == fresh.block || g_table[slot].block == 0) {
			if (g_table[slot].block == 0) g_entries++;
			g_table[slot] = fresh;
			Unlock();
			return;
		}
	}
	g_table[at] = fresh;
	Unlock();
}

bool InWindow(size_t size)
{
	return size >= kWindowLow && size <= kWindowHigh;
}

void LiveAdd(void *block, size_t size, void *returnAddressSlot)
{
	Live fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.block = reinterpret_cast<uintptr_t>(block);
	fresh.size = static_cast<unsigned>(size);
	fresh.game = static_cast<unsigned>(g_game);
	uintptr_t sites[kSites] = {};
	Capture(sites, returnAddressSlot);
	memcpy(fresh.site, sites, sizeof(fresh.site));
	Lock();
	if (static_cast<unsigned>(g_liveCount) >= kMostLive) {
		memset(g_live, 0, sizeof(g_live));
		g_liveCount = 0;
		g_liveReset++;
	}
	const unsigned at = SlotOf(fresh.block);
	unsigned use = kLiveSlots;
	for (unsigned i = 0; i < kProbe; i++) {
		const unsigned slot = (at + i) & (kLiveSlots - 1);
		if (g_live[slot].block == fresh.block) {
			use = slot;
			g_liveCount--;
			break;
		}
		if ((g_live[slot].block == 0 || g_live[slot].block == kDeadSlot) && use == kLiveSlots) use = slot;
		if (g_live[slot].block == 0) break;
	}
	if (use == kLiveSlots) use = at & (kLiveSlots - 1);
	g_live[use] = fresh;
	g_liveCount++;
	Unlock();
}

// The block is being freed (or moved): forget it. Most blocks are not followed, so the table is read without the lock first.
void LiveRemove(void *block)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(block);
	const unsigned at = SlotOf(key);
	for (unsigned i = 0; i < kProbe; i++) {
		const uintptr_t seen = g_live[(at + i) & (kLiveSlots - 1)].block;
		if (seen == 0) return;
		if (seen != key) continue;
		Lock();
		for (unsigned k = 0; k < kProbe; k++) {
			Live &entry = g_live[(at + k) & (kLiveSlots - 1)];
			if (entry.block == key) {
				entry.block = kDeadSlot;
				g_liveCount--;
				break;
			}
			if (entry.block == 0) break;
		}
		Unlock();
		return;
	}
}

void __cdecl HookFree(void *block)
{
	if (block && g_liveCount > 0) LiveRemove(block);
	g_origFree(block);
}

void *__cdecl HookMalloc(size_t size)
{
	void *block = g_origMalloc(size);
	if (block && size >= kMemTraceLarge) Remember(block, size, kPathMalloc, _AddressOfReturnAddress());
	if (block && InWindow(size)) LiveAdd(block, size, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookRealloc(void *old, size_t size)
{
	if (old && g_liveCount > 0) LiveRemove(old);
	void *block = g_origRealloc(old, size);
	if (block && size >= kMemTraceLarge) Remember(block, size, kPathRealloc, _AddressOfReturnAddress());
	if (block && InWindow(size)) LiveAdd(block, size, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookCalloc(size_t count, size_t size)
{
	void *block = g_origCalloc(count, size);
	if (block && count && size && count >= kMemTraceLarge / size + 1) Remember(block, count * size, kPathCalloc, _AddressOfReturnAddress());
	if (block && count && size && InWindow(count * size)) LiveAdd(block, count * size, _AddressOfReturnAddress());
	return block;
}

PVOID WINAPI HookHeapAlloc(PVOID heap, ULONG flags, SIZE_T size)
{
	PVOID block = g_origHeapAlloc(heap, flags, size);
	if (block && size >= kHeapLarge) Remember(block, size, kPathHeapAlloc, _AddressOfReturnAddress());
	return block;
}

PVOID WINAPI HookHeapReAlloc(PVOID heap, ULONG flags, PVOID old, SIZE_T size)
{
	PVOID block = g_origHeapReAlloc(heap, flags, old, size);
	if (block && size >= kHeapLarge) Remember(block, size, kPathHeapRealloc, _AddressOfReturnAddress());
	return block;
}

bool Hook(uintptr_t target, void *detour, void **original, const char *name)
{
	if (HookCreate(reinterpret_cast<void *>(target), detour, original) != MH_OK || HookEnable(reinterpret_cast<void *>(target)) != MH_OK) {
		LogW("game", "memory trace: could not hook %s; the large blocks are not followed", name);
		return false;
	}
	return true;
}

bool InUnseen(const Unseen *list, unsigned count, uintptr_t block, size_t size)
{
	for (unsigned i = 0; i < count; i++) {
		if (list[i].block == block && list[i].size == size) return true;
	}
	return false;
}

// "module.dll+offset" of an address, to say whose code asked for a block.
void Describe(uintptr_t address, char *out, size_t cap)
{
	if (!address) {
		_snprintf_s(out, cap, _TRUNCATE, "-");
		return;
	}
	HMODULE module = nullptr;
	char file[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module) {
		GetModuleFileNameA(module, file, MAX_PATH);
		const char *slash = strrchr(file, '\\');
		_snprintf_s(out, cap, _TRUNCATE, "%s+%x", slash ? slash + 1 : file, static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(module)));
	}
	else {
		_snprintf_s(out, cap, _TRUNCATE, "%08x", static_cast<unsigned>(address));
	}
}

// The first bytes of a block, and how many of the first 4096 are 0: to tell a bitmap from a table of zeros from a sound.
void Content(const Unseen &u, unsigned char *head, unsigned *zeros, unsigned *sample)
{
	memset(head, 0, 12);
	*zeros = 0;
	*sample = 0;
	__try {
		const unsigned char *bytes = reinterpret_cast<const unsigned char *>(u.block);
		memcpy(head, bytes, 12);
		const unsigned n = u.size < 4096 ? static_cast<unsigned>(u.size) : 4096u;
		for (unsigned i = 0; i < n; i++) *zeros += bytes[i] == 0;
		*sample = n;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		*sample = 0;
	}
}

// Looks for who points into the blocks in g_target (a word inside [block, block + size)): the globals of the executable, then every block of
// every heap. One pass over the memory for all of them, with the heap locked and without allocating.
void FindReferrers(unsigned targets)
{
	g_referrers = 0;
	if (!targets) return;
	uintptr_t low = ~static_cast<uintptr_t>(0), high = 0;
	for (unsigned i = 0; i < targets; i++) {
		if (g_target[i].block < low) low = g_target[i].block;
		if (g_target[i].block + g_target[i].size > high) high = g_target[i].block + g_target[i].size;
	}
	const unsigned limit = static_cast<unsigned>(sizeof(g_referrer) / sizeof(g_referrer[0]));
	// The globals.
	uintptr_t at = kDataBegin;
	while (at < kDataEnd && g_referrers < limit) {
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof(info)) == 0) break;
		const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
		const bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
		if (readable) {
			const uintptr_t end = regionEnd < kDataEnd ? regionEnd : kDataEnd;
			__try {
				for (uintptr_t a = at & ~static_cast<uintptr_t>(3); a + 4 <= end && g_referrers < limit; a += 4) {
					const uintptr_t v = *reinterpret_cast<const uintptr_t *>(a);
					if (v < low || v >= high) continue;
					for (unsigned i = 0; i < targets; i++) {
						if (v >= g_target[i].block && v < g_target[i].block + g_target[i].size) {
							unsigned have = 0;
							for (unsigned k = 0; k < g_referrers; k++) have += g_referrer[k].target == i;
							if (have < kReferrers) g_referrer[g_referrers++] = {i, a, 0, 0, 0, 0};
							break;
						}
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}
		if (regionEnd <= at) break;
		at = regionEnd;
	}
	// The heaps.
	HANDLE heaps[64];
	const DWORD total = GetProcessHeaps(64, heaps);
	for (DWORD h = 0; h < total && h < 64 && g_referrers < limit; h++) {
		const bool locked = HeapLock(heaps[h]) != FALSE;
		__try {
			PROCESS_HEAP_ENTRY entry;
			entry.lpData = nullptr;
			while (g_referrers < limit && HeapWalk(heaps[h], &entry)) {
				if (!(entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) || entry.cbData < 4) continue;
				const uintptr_t base = reinterpret_cast<uintptr_t>(entry.lpData);
				const uintptr_t *words = static_cast<const uintptr_t *>(entry.lpData);
				const size_t count = entry.cbData / 4;
				for (size_t w = 0; w < count; w++) {
					const uintptr_t v = words[w];
					if (v < low || v >= high) continue;
					for (unsigned i = 0; i < targets; i++) {
						if (v >= g_target[i].block && v < g_target[i].block + g_target[i].size && base != g_target[i].block) {
							unsigned have = 0;
							for (unsigned k = 0; k < g_referrers; k++) have += g_referrer[k].target == i;
							if (have < kReferrers && g_referrers < limit) g_referrer[g_referrers++] = {i, base + w * 4, base, entry.cbData, h, words[0]};
							break;
						}
					}
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
		if (locked) HeapUnlock(heaps[h]);
	}
}

// The blocks of the size of the leaking groups that are alive, by the game that made them and the code that asked for them. The first word of
// a block that is an object is its vtable, which names its class.
void LogLive(const char *when)
{
	struct LiveGroup {
		uintptr_t site[3];
		unsigned game;
		unsigned blocks;
		unsigned long long bytes;
		uintptr_t first;
	};
	static LiveGroup group[kGroups];
	unsigned groups = 0, lost = 0;
	unsigned long long total = 0;
	unsigned alive = 0;
	Lock();
	for (unsigned i = 0; i < kLiveSlots; i++) {
		const Live &e = g_live[i];
		if (e.block == 0 || e.block == kDeadSlot) continue;
		alive++;
		total += e.size;
		unsigned g = 0;
		while (g < groups && !(group[g].game == e.game && group[g].site[0] == e.site[0] && group[g].site[1] == e.site[1] && group[g].site[2] == e.site[2])) g++;
		if (g == groups) {
			if (groups == kGroups) {
				lost++;
				continue;
			}
			groups++;
			memset(&group[g], 0, sizeof(group[g]));
			memcpy(group[g].site, e.site, sizeof(group[g].site));
			group[g].game = e.game;
			__try {
				group[g].first = *reinterpret_cast<const uintptr_t *>(e.block);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}
		group[g].blocks++;
		group[g].bytes += e.size;
	}
	Unlock();
	LogI("game", "live blocks of %u to %u bytes (%s): %u alive (%.1f MB), in %u groups%s%s", static_cast<unsigned>(kWindowLow + 1), static_cast<unsigned>(kWindowHigh), when, alive,
		static_cast<double>(total) / 1048576.0, groups, lost ? " (groups lost)" : "", g_liveReset ? " (the table was emptied once, older blocks are not listed)" : "");
	bool shown[kGroups] = {};
	for (unsigned k = 0; k < 14; k++) {
		int best = -1;
		for (unsigned i = 0; i < groups; i++) {
			if (!shown[i] && (best < 0 || group[i].blocks > group[best].blocks)) best = static_cast<int>(i);
		}
		if (best < 0) break;
		shown[best] = true;
		const LiveGroup &g = group[best];
		char s0[96], s1[96], s2[96];
		Describe(g.site[0], s0, sizeof(s0));
		Describe(g.site[1], s1, sizeof(s1));
		Describe(g.site[2], s2, sizeof(s2));
		LogI("game", "live blocks (%s):   game #%u, %u blocks, %.0f KB, first word of the first %08x, from %s <- %s <- %s", when, g.game, g.blocks, static_cast<double>(g.bytes) / 1024.0,
			static_cast<unsigned>(g.first), s0, s1, s2);
	}
}

void LogUnseen(const char *when, const char *what, const Unseen *list, unsigned count, const Unseen *than, unsigned thanCount, bool referrers)
{
	// The blocks of the list that the other list does not have.
	unsigned fresh = 0, targets = 0;
	unsigned long long freshBytes = 0;
	for (unsigned i = 0; i < count; i++) {
		if (InUnseen(than, thanCount, list[i].block, list[i].size)) continue;
		fresh++;
		freshBytes += list[i].size;
		if (targets < kTargets && targets < kDumped) g_target[targets++] = list[i];
	}
	LogI("game", "large blocks (%s): %u blocks of 128 KB or more that no hook saw allocated are new %s (%.1f MB)%s", when, fresh, what, static_cast<double>(freshBytes) / 1048576.0,
		g_unseenFull ? " (the list of the walk was full)" : "");
	if (referrers) FindReferrers(targets);
	for (unsigned i = 0; i < targets; i++) {
		const Unseen &u = g_target[i];
		unsigned char head[12];
		unsigned zeros, sample;
		Content(u, head, &zeros, &sample);
		char line[700];
		int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "block %08x, heap %u, %u bytes, starts %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x, %u of the first %u bytes are 0", static_cast<unsigned>(u.block),
			u.heap, static_cast<unsigned>(u.size), head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], head[8], head[9], head[10], head[11], zeros, sample);
		unsigned seen = 0;
		for (unsigned k = 0; k < g_referrers && referrers; k++) {
			if (g_referrer[k].target != i) continue;
			const Referrer &r = g_referrer[k];
			const int w = r.holder ? _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "; held by %08x in the block %08x (heap %u, %u bytes, first word %08x)", static_cast<unsigned>(r.at),
				static_cast<unsigned>(r.holder), r.holderHeap, static_cast<unsigned>(r.holderSize), r.holderFirstWord)
				: _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "; held by the global %08x", static_cast<unsigned>(r.at));
			n = w < 0 ? static_cast<int>(strlen(line)) : n + w;
			seen++;
		}
		if (referrers && !seen) {
			const int w = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "; nothing in the executable's data or in a heap block points into it");
			n = w < 0 ? static_cast<int>(strlen(line)) : n + w;
		}
		LogI("game", "large blocks (%s):   %s", when, line);
	}
}

}

void InstallMemTrace(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = Hook(addr::kMalloc, reinterpret_cast<void *>(&HookMalloc), reinterpret_cast<void **>(&g_origMalloc), "malloc") &&
		Hook(addr::kRealloc, reinterpret_cast<void *>(&HookRealloc), reinterpret_cast<void **>(&g_origRealloc), "realloc") &&
		Hook(addr::kCalloc, reinterpret_cast<void *>(&HookCalloc), reinterpret_cast<void **>(&g_origCalloc), "calloc") &&
		Hook(addr::kGameFree, reinterpret_cast<void *>(&HookFree), reinterpret_cast<void **>(&g_origFree), "free");
	// The heap functions of Windows (kernel32's HeapAlloc forwards to these): every module of the process ends in them, the game's runtime too.
	if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
		const uintptr_t alloc = reinterpret_cast<uintptr_t>(GetProcAddress(ntdll, "RtlAllocateHeap"));
		const uintptr_t realloc = reinterpret_cast<uintptr_t>(GetProcAddress(ntdll, "RtlReAllocateHeap"));
		if (alloc) Hook(alloc, reinterpret_cast<void *>(&HookHeapAlloc), reinterpret_cast<void **>(&g_origHeapAlloc), "RtlAllocateHeap");
		if (realloc) Hook(realloc, reinterpret_cast<void *>(&HookHeapReAlloc), reinterpret_cast<void **>(&g_origHeapReAlloc), "RtlReAllocateHeap");
	}
	if (ok) LogI("game", "memory trace: following the blocks of %u KB or more of the game's allocator and of %u KB or more of the Windows heaps", static_cast<unsigned>(kMemTraceLarge >> 10),
		static_cast<unsigned>(kHeapLarge >> 10));
}

void MemTraceNewGame()
{
	InterlockedIncrement(&g_game);
	g_gameReloaded = false;
}

void MemTraceGameReloaded()
{
	g_gameReloaded = true;
}

void MemTraceBegin()
{
	memset(g_group, 0, sizeof(g_group));
	g_groups = 0;
	memset(g_noRecord, 0, sizeof(g_noRecord));
	memset(g_noRecordBytes, 0, sizeof(g_noRecordBytes));
	g_groupsLost = 0;
	g_unseenCount = 0;
	g_unseenFull = false;
}

void MemTraceNote(const void *block, size_t size, unsigned heap)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(block);
	const unsigned h = heap < kHeaps ? heap : kHeaps - 1;
	Record found;
	bool have = false;
	Lock();
	const unsigned at = SlotOf(key);
	for (unsigned i = 0; i < kProbe && !have; i++) {
		const Record &r = g_table[(at + i) & (kSlots - 1)];
		if (r.block == 0) break;
		if (r.block == key) {
			found = r;
			have = true;
		}
	}
	Unlock();
	if (!have) {
		g_noRecord[h]++;
		g_noRecordBytes[h] += size;
		if (size >= kHeapLarge) {
			if (g_unseenCount < kUnseenKept) g_unseen[g_unseenCount++] = {key, size, h};
			else g_unseenFull = true;
		}
		return;
	}
	// The heap rounds a block up a little; a bigger difference is a block that grew or an address that was freed and made again by something else.
	const bool differs = !(size >= found.size && size < found.size + 4096);
	for (unsigned i = 0; i < g_groups; i++) {
		Group &g = g_group[i];
		if (g.game == found.game && g.path == found.path && g.heap == h && g.sizeDiffers == differs && g.site[0] == found.site[0] && g.site[1] == found.site[1] && g.site[2] == found.site[2]) {
			g.blocks++;
			g.bytes += size;
			return;
		}
	}
	if (g_groups == kGroups) {
		g_groupsLost++;
		return;
	}
	Group &g = g_group[g_groups++];
	g.game = found.game;
	g.path = found.path;
	g.heap = h;
	g.sizeDiffers = differs;
	g.recordedSize = found.size;
	memcpy(g.site, found.site, sizeof(g.site));
	g.blocks = 1;
	g.bytes = size;
}

void MemTraceReport(const char *when, TraceAt at)
{
	if (at == TraceAt::kLoad && !g_gameReloaded) return;
	// How many large blocks are alive from each game (the last games that have any).
	unsigned long long bytes[64] = {};
	unsigned blocks[64] = {};
	unsigned newest = 0;
	for (unsigned i = 0; i < g_groups; i++) {
		if (g_group[i].game > newest) newest = g_group[i].game;
	}
	for (unsigned i = 0; i < g_groups; i++) {
		const unsigned back = newest - g_group[i].game;
		if (back >= 64) continue;
		blocks[back] += g_group[i].blocks;
		bytes[back] += g_group[i].bytes;
	}
	char line[900];
	int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "large blocks (%u KB+) alive (%s), by the game that made them, game now %ld:", static_cast<unsigned>(kMemTraceLarge >> 10), when,
		static_cast<long>(g_game));
	for (unsigned back = 0; back < 64 && n > 0 && n < 780; back++) {
		if (!blocks[back]) continue;
		const int written = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " #%u %u blocks %.1f MB;", newest - back, blocks[back], static_cast<double>(bytes[back]) / 1048576.0);
		n = written < 0 ? static_cast<int>(strlen(line)) : n + written;
	}
	LogI("game", "%s%s", line, g_groupsLost ? " (groups lost)" : "");
	// What no hook saw, by heap: it was alive before the hooks, or came by a path that is not followed.
	for (unsigned h = 0; h < kHeaps; h++) {
		if (g_noRecord[h]) {
			LogI("game", "large blocks (%s): no record of %u blocks (%.1f MB) in heap %u", when, g_noRecord[h], static_cast<double>(g_noRecordBytes[h]) / 1048576.0, h);
		}
	}
	// The biggest groups, with the code that asked for them.
	bool shown[kGroups] = {};
	for (unsigned k = 0; k < 12; k++) {
		int best = -1;
		for (unsigned i = 0; i < g_groups; i++) {
			if (!shown[i] && (best < 0 || g_group[i].bytes > g_group[best].bytes)) best = static_cast<int>(i);
		}
		if (best < 0 || g_group[best].bytes < 512 * 1024) break;
		shown[best] = true;
		const Group &g = g_group[best];
		char s0[96], s1[96], s2[96], differs[64] = "";
		Describe(g.site[0], s0, sizeof(s0));
		Describe(g.site[1], s1, sizeof(s1));
		Describe(g.site[2], s2, sizeof(s2));
		if (g.sizeDiffers) _snprintf_s(differs, sizeof(differs), _TRUNCATE, ", asked for %u KB", static_cast<unsigned>(g.recordedSize >> 10));
		LogI("game", "large blocks (%s):   game #%u, heap %u, %s, %u blocks of %u KB%s, %.1f MB, from %s <- %s <- %s", when, g.game, g.heap, PathName(g.path), g.blocks,
			static_cast<unsigned>(g.bytes / g.blocks >> 10), differs, static_cast<double>(g.bytes) / 1048576.0, s0, s1, s2);
	}
	// The blocks nobody was seen to allocate: which ones the reload made, who holds them, and what they hold.
	switch (at) {
	case TraceAt::kShutdown:
		memcpy(g_unseenShutdown, g_unseen, sizeof(Unseen) * g_unseenCount);
		g_unseenShutdownCount = g_unseenCount;
		g_haveShutdown = true;
		break;
	case TraceAt::kLoad:
		if (g_haveShutdown && g_gameReloaded) LogUnseen(when, "since the reload started (Init and LoadGameData)", g_unseen, g_unseenCount, g_unseenShutdown, g_unseenShutdownCount, true);
		if (g_gameReloaded) LogLive(when);
		break;
	case TraceAt::kUnload:
		if (g_haveUnload && g_gameReloaded) LogUnseen(when, "since the end of the previous game", g_unseen, g_unseenCount, g_unseenUnload, g_unseenUnloadCount, false);
		if (g_gameReloaded) LogLive(when);
		memcpy(g_unseenUnload, g_unseen, sizeof(Unseen) * g_unseenCount);
		g_unseenUnloadCount = g_unseenCount;
		g_haveUnload = true;
		break;
	default:
		break;
	}
}
