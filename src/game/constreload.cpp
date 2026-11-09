// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "addr/constcache.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/constreload.h"
#include "i18n/tr.h"
#include "mods/mods.h"
#include "mods/vfs.h"

// The game reads data/const.ini into a table once per process (DAT_00a877b4) and, besides, copies some of its values into globals
// the first time each is needed, guarded by a flag or a -1 that nothing sets back. A new table alone leaves those copies stale
// ("half applied" constants), so loading another ruleset's const.ini takes three steps, before the match is created:
//   1. serve the new bytes (the DLL's patched copy of the file is made again),
//   2. build a complete new table and put it in place of the old one,
//   3. set every "already copied" guard back, so each copy is made again the next time it is needed.
// It runs at the start of LoadGameData: skirmish, multiplayer, loaded games and restarts all pass there, and nothing of the match
// exists yet.
namespace {
typedef void *(__cdecl *OperatorNew_t)(size_t size);
typedef void *(__fastcall *HashIniCtor_t)(void *self, void *edx, unsigned sectionSlots, unsigned keySlots, void *pool, unsigned ownsPool);
typedef const char *(__fastcall *HashIniLoad_t)(void *self, void *edx, const char *path);
typedef const char *(__fastcall *HashIniFindValue_t)(void *self, void *edx, const char *section);
typedef const char *(__fastcall *HashIniGetInt_t)(void *self, void *edx, const char *section, const char *key, int *out);
typedef void *(__fastcall *LoadGameData_t)(void *self, void *edx);

LoadGameData_t g_origLoadGameData = nullptr;

volatile unsigned *Word(uintptr_t address)
{
	return reinterpret_cast<volatile unsigned *>(address);
}

void *&Table()
{
	return *reinterpret_cast<void **>(addr::kConstIni);
}

int GamePlayInt(void *table, const char *key)
{
	int value = 0;
	if (table) reinterpret_cast<HashIniGetInt_t>(addr::kHashIniGetInt)(table, nullptr, "GamePlay", key, &value);
	return value;
}

// The flags use a few low bits. A word that holds anything else is not the guard this code expects (another build of the game):
// nothing is written.
bool GuardsLookRight()
{
	if (!Table() || !*reinterpret_cast<void **>(addr::kPtrPool)) {
		LogW("game", "const reload: the game's table or its allocator are not there");
		return false;
	}
	for (uintptr_t flag : addr::kConstFlagGuards) {
		if (*Word(flag) > 0xff) {
			LogW("game", "const reload: the word at %08x holds %08x, not a flag; the guards are not set back", static_cast<unsigned>(flag), *Word(flag));
			return false;
		}
	}
	if (*Word(addr::kLoyaltyRadiusFlags) > 0xff) {
		LogW("game", "const reload: the word at %08x holds %08x, not a flag; the guards are not set back", static_cast<unsigned>(addr::kLoyaltyRadiusFlags),
			*Word(addr::kLoyaltyRadiusFlags));
		return false;
	}
	return true;
}

// A complete table of data/const.ini as the files serve it now, or nullptr. A failed attempt leaves the game's table as it was
// (the half-built one is left behind: a few kilobytes, and only on a failure).
void *BuildTable()
{
	void *pool = *reinterpret_cast<void **>(addr::kPtrPool);
	void *memory = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x28);
	if (!memory) {
		LogE("game", "const reload: no memory for the new table");
		return nullptr;
	}
	// The same arguments the game gives it when it creates its table (LoadGameConfigFile).
	void *table = reinterpret_cast<HashIniCtor_t>(addr::kHashIniCtor)(memory, nullptr, 0x20, 0x80, pool, 0);
	if (!table) {
		LogE("game", "const reload: the new table could not be created");
		return nullptr;
	}
	const char *error = reinterpret_cast<HashIniLoad_t>(addr::kHashIniLoad)(table, nullptr, "data/Const.ini");
	if (error) {
		LogE("game", "const reload: data/Const.ini could not be loaded");
		return nullptr;
	}
	if (!reinterpret_cast<HashIniFindValue_t>(addr::kHashIniFindValue)(table, nullptr, "GamePlay")) {
		LogE("game", "const reload: the new data/Const.ini has no [GamePlay] section");
		return nullptr;
	}
	return table;
}

// Sets the words to value and returns how many held something else (loaders that had already run).
unsigned SetWords(const uintptr_t *list, size_t count, unsigned value)
{
	unsigned changed = 0;
	for (size_t i = 0; i < count; i++) {
		const unsigned before = *Word(list[i]);
		if (before != value) changed++;
		*Word(list[i]) = value;
		LogD("game", "const reload: %08x was %08x, now %08x", static_cast<unsigned>(list[i]), before, value);
	}
	return changed;
}

void ResetGuards()
{
	const unsigned loaded = *Word(addr::kGameplayConstantsLoaded), amplify = *Word(addr::kDamageAmplify);
	const unsigned flags = SetWords(addr::kConstFlagGuards, sizeof(addr::kConstFlagGuards) / sizeof(addr::kConstFlagGuards[0]), 0);
	const unsigned loyalty = *Word(addr::kLoyaltyRadiusFlags);
	*Word(addr::kLoyaltyRadiusFlags) = loyalty & ~3u;
	LogD("game", "const reload: %08x was %08x, now %08x", static_cast<unsigned>(addr::kLoyaltyRadiusFlags), loyalty, *Word(addr::kLoyaltyRadiusFlags));
	LogI("game", "const reload: flags set back to 0: %u of %u had been set (g_bGameplayConstantsLoaded %u -> %u)", flags + ((loyalty & 3u) ? 1u : 0u),
		static_cast<unsigned>(sizeof(addr::kConstFlagGuards) / sizeof(addr::kConstFlagGuards[0])) + 1u, loaded, *Word(addr::kGameplayConstantsLoaded));
	const unsigned sentinels = SetWords(addr::kConstSentinelGuards, sizeof(addr::kConstSentinelGuards) / sizeof(addr::kConstSentinelGuards[0]), 0xffffffffu);
	LogI("game", "const reload: sentinels set back to -1: %u of %u had been filled", sentinels,
		static_cast<unsigned>(sizeof(addr::kConstSentinelGuards) / sizeof(addr::kConstSentinelGuards[0])));
	const unsigned zeros = SetWords(addr::kConstZeroGuards, sizeof(addr::kConstZeroGuards) / sizeof(addr::kConstZeroGuards[0]), 0);
	LogI("game", "const reload: ghost values set back to 0: %u of %u had been filled", zeros,
		static_cast<unsigned>(sizeof(addr::kConstZeroGuards) / sizeof(addr::kConstZeroGuards[0])));
	LogI("game", "const reload: the copy of DamageAmplify (%08x) still reads %d until the next settlement is created", static_cast<unsigned>(addr::kDamageAmplify),
		static_cast<int>(amplify));
}

bool ReloadConst()
{
	if (!GuardsLookRight()) return false;
	// Step 1: the next open of the file applies the patches of the ruleset mounted now.
	VfsResetPatched("data/const.ini");
	// Step 2: the old table is not freed. Another thread may still be reading it, and a failed load must never leave the game without one.
	void *fresh = BuildTable();
	if (!fresh) return false;
	void *old = Table();
	const int amplifyOld = GamePlayInt(old, "DamageAmplify"), amplifyNew = GamePlayInt(fresh, "DamageAmplify");
	Table() = fresh;
	LogI("game", "const reload: new table in place (DamageAmplify in the table %d -> %d)", amplifyOld, amplifyNew);
	// Step 3.
	ResetGuards();
	return true;
}

void *__fastcall HookLoadGameData(void *self, void *edx)
{
	if (ModsConstReloadPending()) {
		if (ReloadConst()) {
			ModsConstReloadDone();
		}
		else {
			LogE("game", "const reload: failed; the match uses the rules the game started with");
			ModsConstReloadFailed();
			UiToast(Tr("The new rules could not be loaded without restarting. This game uses the previous ones."), true);
		}
	}
	return g_origLoadGameData(self, edx);
}
}

void InstallConstReload(bool knownExe)
{
	if (!knownExe || !GetConfig().constHotReload) return;
	if (HookCreate(reinterpret_cast<void *>(addr::kLoadGameData), reinterpret_cast<void *>(&HookLoadGameData), reinterpret_cast<void **>(&g_origLoadGameData)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kLoadGameData)) != MH_OK) {
		LogE("game", "const reload: could not hook LoadGameData; a ruleset change will still need a restart");
		return;
	}
	ModsConstReloadSetReady(true);
	LogI("game", "const reload: ready ([Mods] ConstHotReload=1)");
}
