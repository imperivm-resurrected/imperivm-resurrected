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
};

const unsigned kHeaps = 8;
const unsigned kUnknownKept = 2048;
// The blocks the heap functions of Windows are followed for: those of the size the leak has, and larger.
const size_t kHeapLarge = 0x20000;

struct Unknown {
	uintptr_t block;
	size_t size;
	unsigned heap;
};

// All static: the hooks run inside the game's malloc, and the census asks with a heap locked.
Record g_table[kSlots];
unsigned g_entries = 0;
volatile LONG g_lock = 0;
volatile LONG g_game = 0;

Group g_group[kGroups];
unsigned g_groups = 0;
unsigned g_groupsLost = 0;
unsigned g_noRecord[kHeaps] = {};
unsigned long long g_noRecordBytes[kHeaps] = {};
Unknown g_unknown[kUnknownKept], g_unknownBefore[kUnknownKept];
unsigned g_unknownCount = 0, g_unknownBeforeCount = 0;

typedef void *(__cdecl *Malloc_t)(size_t size);
typedef void *(__cdecl *Realloc_t)(void *block, size_t size);
typedef void *(__cdecl *Calloc_t)(size_t count, size_t size);
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

void Capture(uintptr_t *site, void *returnAddressSlot)
{
	const uintptr_t *sp = static_cast<const uintptr_t *>(returnAddressSlot);
	const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
	const uintptr_t *top = static_cast<const uintptr_t *>(tib->StackBase);
	unsigned found = 0;
	for (unsigned i = 0; i < kStackWords && sp + i < top && found < kSites; i++) {
		if (LooksLikeReturn(sp[i])) site[found++] = sp[i];
	}
}

void Remember(void *block, size_t size, unsigned path, void *returnAddressSlot)
{
	Record fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.block = reinterpret_cast<uintptr_t>(block);
	fresh.size = size;
	fresh.game = static_cast<unsigned>(g_game);
	fresh.path = path;
	if (path == kPathHeapAlloc || path == kPathHeapRealloc) {
		// The caller can be any module: the frames of the stack, whatever they belong to.
		void *frames[kSites + 1] = {};
		const USHORT got = CaptureStackBackTrace(1, kSites + 1, frames, nullptr);
		for (USHORT i = 1; i < got && i <= kSites; i++) fresh.site[i - 1] = reinterpret_cast<uintptr_t>(frames[i]);
	}
	else {
		Capture(fresh.site, returnAddressSlot);
	}
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

void *__cdecl HookMalloc(size_t size)
{
	void *block = g_origMalloc(size);
	if (block && size >= kMemTraceLarge) Remember(block, size, kPathMalloc, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookRealloc(void *old, size_t size)
{
	void *block = g_origRealloc(old, size);
	if (block && size >= kMemTraceLarge) Remember(block, size, kPathRealloc, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookCalloc(size_t count, size_t size)
{
	void *block = g_origCalloc(count, size);
	if (block && count && size && count >= kMemTraceLarge / size + 1) Remember(block, count * size, kPathCalloc, _AddressOfReturnAddress());
	return block;
}

PVOID WINAPI HookHeapAlloc(PVOID heap, ULONG flags, SIZE_T size)
{
	PVOID block = g_origHeapAlloc(heap, flags, size);
	if (block && size >= kHeapLarge) Remember(block, size, kPathHeapAlloc, nullptr);
	return block;
}

PVOID WINAPI HookHeapReAlloc(PVOID heap, ULONG flags, PVOID old, SIZE_T size)
{
	PVOID block = g_origHeapReAlloc(heap, flags, old, size);
	if (block && size >= kHeapLarge) Remember(block, size, kPathHeapRealloc, nullptr);
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

}

void InstallMemTrace(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = Hook(addr::kMalloc, reinterpret_cast<void *>(&HookMalloc), reinterpret_cast<void **>(&g_origMalloc), "malloc") &&
		Hook(addr::kRealloc, reinterpret_cast<void *>(&HookRealloc), reinterpret_cast<void **>(&g_origRealloc), "realloc") &&
		Hook(addr::kCalloc, reinterpret_cast<void *>(&HookCalloc), reinterpret_cast<void **>(&g_origCalloc), "calloc");
	// The heap functions of Windows (kernel32's HeapAlloc forwards to these): every module of the process ends in them, the game's runtime too.
	// A block that reaches a heap without going through the game's malloc is seen here.
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
}

void MemTraceBegin()
{
	memset(g_group, 0, sizeof(g_group));
	g_groups = 0;
	memset(g_noRecord, 0, sizeof(g_noRecord));
	memset(g_noRecordBytes, 0, sizeof(g_noRecordBytes));
	g_groupsLost = 0;
	memcpy(g_unknownBefore, g_unknown, sizeof(g_unknown));
	g_unknownBeforeCount = g_unknownCount;
	g_unknownCount = 0;
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
		// The heap rounds a block up a little; a different size means the address was reused by something that was not followed.
		if (r.block == key && size >= r.size && size < r.size + 4096) {
			found = r;
			have = true;
		}
	}
	Unlock();
	if (!have) {
		g_noRecord[h]++;
		g_noRecordBytes[h] += size;
		if (size >= kHeapLarge && g_unknownCount < kUnknownKept) g_unknown[g_unknownCount++] = {key, size, h};
		return;
	}
	for (unsigned i = 0; i < g_groups; i++) {
		Group &g = g_group[i];
		if (g.game == found.game && g.path == found.path && g.heap == h && g.site[0] == found.site[0] && g.site[1] == found.site[1] && g.site[2] == found.site[2]) {
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
	memcpy(g.site, found.site, sizeof(g.site));
	g.blocks = 1;
	g.bytes = size;
}

// "module.dll+offset" of an address, to say whose code asked for a block.
void Describe(uintptr_t address, char *out, size_t cap)
{
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

// What the first bytes of a block that nobody was seen to ask for look like, to tell a bitmap from a table of zeros from a sound.
void LogUnknownContent(const char *when, const Unknown &u)
{
	unsigned char head[16] = {};
	unsigned zeros = 0, sample = 0;
	__try {
		const unsigned char *bytes = reinterpret_cast<const unsigned char *>(u.block);
		memcpy(head, bytes, sizeof(head));
		sample = u.size < 4096 ? static_cast<unsigned>(u.size) : 4096u;
		for (unsigned i = 0; i < sample; i++) zeros += bytes[i] == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		sample = 0;
	}
	LogI("game", "large blocks (%s):   new block nobody was seen to allocate: heap %u, %u KB at %08x, starts %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x, %u of the first %u bytes are 0",
		when, u.heap, static_cast<unsigned>(u.size >> 10), static_cast<unsigned>(u.block), head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], head[8], head[9],
		head[10], head[11], zeros, sample);
}

void MemTraceReport(const char *when)
{
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
		char s0[96], s1[96], s2[96];
		Describe(g.site[0], s0, sizeof(s0));
		Describe(g.site[1], s1, sizeof(s1));
		Describe(g.site[2], s2, sizeof(s2));
		LogI("game", "large blocks (%s):   game #%u, heap %u, %s, %u blocks of %u KB, %.1f MB, from %s <- %s <- %s", when, g.game, g.heap, PathName(g.path), g.blocks,
			static_cast<unsigned>(g.bytes / g.blocks >> 10), static_cast<double>(g.bytes) / 1048576.0, s0, s1, s2);
	}
	// Blocks nobody was seen to allocate that were not in the list of the walk before: what they hold tells what they are.
	unsigned printed = 0;
	for (unsigned i = 0; i < g_unknownCount && printed < 6; i++) {
		bool old = false;
		for (unsigned j = 0; j < g_unknownBeforeCount && !old; j++) old = g_unknownBefore[j].block == g_unknown[i].block && g_unknownBefore[j].size == g_unknown[i].size;
		if (old || !g_unknownBeforeCount) continue;
		LogUnknownContent(when, g_unknown[i]);
		printed++;
	}
}
