// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <cstdint>
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

struct Record {
	uintptr_t block;
	size_t size;
	unsigned game;
	uintptr_t site[kSites];
};

struct Group {
	uintptr_t site[3];
	unsigned game;
	unsigned blocks;
	unsigned long long bytes;
};

// All static: the hooks run inside the game's malloc, and the census asks with a heap locked.
Record g_table[kSlots];
unsigned g_entries = 0;
volatile LONG g_lock = 0;
volatile LONG g_game = 0;

Group g_group[kGroups];
unsigned g_groups = 0;
unsigned g_noRecord = 0, g_groupsLost = 0;
unsigned long long g_noRecordBytes = 0;

typedef void *(__cdecl *Malloc_t)(size_t size);
typedef void *(__cdecl *Realloc_t)(void *block, size_t size);
typedef void *(__cdecl *Calloc_t)(size_t count, size_t size);
Malloc_t g_origMalloc = nullptr;
Realloc_t g_origRealloc = nullptr;
Calloc_t g_origCalloc = nullptr;

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

void Remember(void *block, size_t size, void *returnAddressSlot)
{
	Record fresh;
	memset(&fresh, 0, sizeof(fresh));
	fresh.block = reinterpret_cast<uintptr_t>(block);
	fresh.size = size;
	fresh.game = static_cast<unsigned>(g_game);
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

void *__cdecl HookMalloc(size_t size)
{
	void *block = g_origMalloc(size);
	if (block && size >= kMemTraceLarge) Remember(block, size, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookRealloc(void *old, size_t size)
{
	void *block = g_origRealloc(old, size);
	if (block && size >= kMemTraceLarge) Remember(block, size, _AddressOfReturnAddress());
	return block;
}

void *__cdecl HookCalloc(size_t count, size_t size)
{
	void *block = g_origCalloc(count, size);
	if (block && count && size && count >= kMemTraceLarge / size + 1) Remember(block, count * size, _AddressOfReturnAddress());
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
	if (ok) LogI("game", "memory trace: following the blocks of %u KB or more", static_cast<unsigned>(kMemTraceLarge >> 10));
}

void MemTraceNewGame()
{
	InterlockedIncrement(&g_game);
}

void MemTraceBegin()
{
	memset(g_group, 0, sizeof(g_group));
	g_groups = 0;
	g_noRecord = 0;
	g_noRecordBytes = 0;
	g_groupsLost = 0;
}

void MemTraceNote(const void *block, size_t size)
{
	const uintptr_t key = reinterpret_cast<uintptr_t>(block);
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
		g_noRecord++;
		g_noRecordBytes += size;
		return;
	}
	for (unsigned i = 0; i < g_groups; i++) {
		Group &g = g_group[i];
		if (g.game == found.game && g.site[0] == found.site[0] && g.site[1] == found.site[1] && g.site[2] == found.site[2]) {
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
	memcpy(g.site, found.site, sizeof(g.site));
	g.blocks = 1;
	g.bytes = size;
}

void MemTraceReport(const char *when)
{
	// How many large blocks are alive from each game (the last twelve games that have any).
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
	LogI("game", "%s no record of %u blocks (%.1f MB)%s", line, g_noRecord, static_cast<double>(g_noRecordBytes) / 1048576.0, g_groupsLost ? " (groups lost)" : "");
	// The biggest groups, with the code that asked for them.
	bool shown[kGroups] = {};
	for (unsigned k = 0; k < 10; k++) {
		int best = -1;
		for (unsigned i = 0; i < g_groups; i++) {
			if (!shown[i] && (best < 0 || g_group[i].bytes > g_group[best].bytes)) best = static_cast<int>(i);
		}
		if (best < 0 || g_group[best].bytes < 512 * 1024) break;
		shown[best] = true;
		const Group &g = g_group[best];
		LogI("game", "large blocks (%s):   game #%u, %u blocks of %u KB, %.1f MB, from %08x <- %08x <- %08x", when, g.game, g.blocks,
			static_cast<unsigned>(g.bytes / g.blocks >> 10), static_cast<double>(g.bytes) / 1048576.0, static_cast<unsigned>(g.site[0]), static_cast<unsigned>(g.site[1]),
			static_cast<unsigned>(g.site[2]));
	}
}
