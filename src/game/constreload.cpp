// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <string>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "addr/constcache.h"
#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/constreload.h"
#include "i18n/tr.h"
#include "mods/constswap.h"
#include "mods/mods.h"
#include "mods/vfs.h"

// The game reads data/const.ini into a table once per process (DAT_00a877b4) and, besides, copies some of its values into globals
// the first time each is needed, guarded by a flag or a -1 that nothing sets back. A new table alone leaves those copies stale
// ("half applied" constants), so loading another ruleset's const.ini takes these steps, before the match is created:
//   1. serve the new bytes (the DLL's patched copy of the file is made again) and check they are a usable const.ini,
//   2. call the game's own ReloadConstIni (Clear + Load on the table it already has) and check the table is not empty,
//   3. set every "already copied" guard back, so each copy is made again the next time it is needed.
// It runs at the start of LoadGameData: skirmish, multiplayer, loaded games and restarts all pass there, and nothing of the match
// exists yet. Every call into the game is inside a __try: a fault there leaves the old rules and never ends the game.
namespace {
typedef int(__cdecl *ReloadConstIni_t)();
typedef const char *(__fastcall *HashIniGetInt_t)(void *self, void *edx, const char *section, const char *key, int *out);
typedef void *(__fastcall *LoadGameData_t)(void *self, void *edx);

LoadGameData_t g_origLoadGameData = nullptr;

enum class Reload {
	kDone,    // the table holds the new file and the guards are set back
	kRefused, // nothing of the game was changed
	kBroken   // the game's table may be empty or half made: it has to be loaded again from the previous file
};

// The guards are 32-bit words: the game's own code reads and writes them as 32-bit words (the exception-unwinding thunks of the
// flags do "mov eax,[flag] ... mov [flag],eax"), and nothing refers to the three bytes after the flags that Ghidra types as one byte.
volatile unsigned *Word(uintptr_t address)
{
	return reinterpret_cast<volatile unsigned *>(address);
}

void *&Table()
{
	return *reinterpret_cast<void **>(addr::kConstIni);
}

// CHashINI::GetInt(section, key, &value) is a thiscall with three stack arguments; it returns 0 when it found the key.
bool GamePlayInt(void *table, const char *key, int *value)
{
	*value = 0;
	return table && reinterpret_cast<HashIniGetInt_t>(addr::kHashIniGetInt)(table, nullptr, "GamePlay", key, value) == nullptr;
}

// Every const.ini of the game has these; a table that has none of them is empty or did not load.
bool TableHasConstants(void *table)
{
	static const char *const kKeys[] = {"DamageAmplify", "MinPopulation", "ProductionInterval"};
	for (const char *key : kKeys) {
		int value = 0;
		if (GamePlayInt(table, key, &value)) return true;
	}
	return false;
}

// The flags use a few low bits. A word that holds anything else is not the guard this code expects (another build of the game).
bool GuardsLookRight()
{
	if (!Table()) {
		LogW("game", "const reload: the game has no table of constants yet");
		return false;
	}
	for (uintptr_t flag : addr::kConstFlagGuards) {
		if (*Word(flag) > 0xff) {
			LogW("game", "const reload: the word at %08x holds %08x, not a flag; nothing is changed", static_cast<unsigned>(flag), *Word(flag));
			return false;
		}
	}
	if (*Word(addr::kLoyaltyRadiusFlags) > 0xff) {
		LogW("game", "const reload: the word at %08x holds %08x, not a flag; nothing is changed", static_cast<unsigned>(addr::kLoyaltyRadiusFlags),
			*Word(addr::kLoyaltyRadiusFlags));
		return false;
	}
	return true;
}

// The update check (UpdateCheckThreadProc) reads the table once, early, from a thread of its own, and then ends. If it is somehow still
// running, the table is not touched.
bool UpdateCheckRunning()
{
	bool running = false;
	g_crashLogQuiet++;
	__try {
		const HANDLE *info = *reinterpret_cast<HANDLE *const *>(addr::kUpdateCheckInfo);
		running = info && *info && WaitForSingleObject(*info, 0) == WAIT_TIMEOUT;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		running = false;
	}
	g_crashLogQuiet--;
	return running;
}

// The file as the game will read it now has to be there and be a const.ini, before the game's table is cleared.
bool FileIsUsable()
{
	VfsResetPatched("data/const.ini");
	std::string text;
	if (!VfsReadPatched("data/const.ini", text)) {
		LogE("game", "const reload: data/const.ini cannot be read");
		return false;
	}
	if (!ConstIniHasGamePlay(text)) {
		LogE("game", "const reload: data/const.ini (%u bytes) has no [GamePlay] section", static_cast<unsigned>(text.size()));
		return false;
	}
	LogI("game", "const reload: data/const.ini as served now: %u bytes", static_cast<unsigned>(text.size()));
	return true;
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
	const unsigned flagCount = sizeof(addr::kConstFlagGuards) / sizeof(addr::kConstFlagGuards[0]);
	const unsigned sentinelCount = sizeof(addr::kConstSentinelGuards) / sizeof(addr::kConstSentinelGuards[0]);
	const unsigned zeroCount = sizeof(addr::kConstZeroGuards) / sizeof(addr::kConstZeroGuards[0]);
	const unsigned loaded = *Word(addr::kGameplayConstantsLoaded), amplify = *Word(addr::kDamageAmplify);
	const unsigned flags = SetWords(addr::kConstFlagGuards, flagCount, 0);
	const unsigned loyalty = *Word(addr::kLoyaltyRadiusFlags);
	*Word(addr::kLoyaltyRadiusFlags) = loyalty & ~3u;
	LogD("game", "const reload: %08x was %08x, now %08x", static_cast<unsigned>(addr::kLoyaltyRadiusFlags), loyalty, *Word(addr::kLoyaltyRadiusFlags));
	LogI("game", "const reload: flags set back to 0: %u of %u had been set (g_bGameplayConstantsLoaded %u -> %u)", flags + ((loyalty & 3u) ? 1u : 0u),
		flagCount + 1u, loaded, *Word(addr::kGameplayConstantsLoaded));
	LogI("game", "const reload: sentinels set back to -1: %u of %u had been filled", SetWords(addr::kConstSentinelGuards, sentinelCount, 0xffffffffu), sentinelCount);
	LogI("game", "const reload: ghost values set back to 0: %u of %u had been filled", SetWords(addr::kConstZeroGuards, zeroCount, 0), zeroCount);
	LogI("game", "const reload: the copy of DamageAmplify (%08x) still reads %d until the next settlement is created", static_cast<unsigned>(addr::kDamageAmplify),
		static_cast<int>(amplify));
}

// Steps 2 and 3. Only plain data in here, so the __try can protect it.
Reload ReloadTable()
{
	Reload result = Reload::kBroken;
	g_crashLogQuiet++;
	__try {
		int before = 0, after = 0;
		GamePlayInt(Table(), "DamageAmplify", &before);
		reinterpret_cast<ReloadConstIni_t>(addr::kReloadConstIni)();
		if (TableHasConstants(Table())) {
			GamePlayInt(Table(), "DamageAmplify", &after);
			LogI("game", "const reload: table loaded again (DamageAmplify in the table %d -> %d)", before, after);
			ResetGuards();
			result = Reload::kDone;
		}
		else {
			LogE("game", "const reload: the table has no constants after loading data/const.ini");
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogE("game", "const reload: the game faulted while loading data/const.ini");
		result = Reload::kBroken;
	}
	g_crashLogQuiet--;
	return result;
}

Reload ReloadConst()
{
	if (!GuardsLookRight()) return Reload::kRefused;
	if (UpdateCheckRunning()) {
		LogW("game", "const reload: the update check is still reading the table; nothing is changed");
		return Reload::kRefused;
	}
	if (!FileIsUsable()) return Reload::kRefused;
	return ReloadTable();
}

// After a failure the previous ruleset is mounted again (ModsConstReloadFailed); load its file into the table.
void RestoreTable()
{
	bool restored = false;
	g_crashLogQuiet++;
	__try {
		reinterpret_cast<ReloadConstIni_t>(addr::kReloadConstIni)();
		restored = TableHasConstants(Table());
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		restored = false;
	}
	g_crashLogQuiet--;
	if (restored) LogW("game", "const reload: the table was loaded again from the previous rules");
	else LogE("game", "const reload: the table could not be restored; restart the game");
}

void *__fastcall HookLoadGameData(void *self, void *edx)
{
	if (ModsConstReloadPending()) {
		const Reload result = ReloadConst();
		if (result == Reload::kDone) {
			ModsConstReloadDone();
		}
		else {
			LogE("game", "const reload: failed; the match uses the rules the game started with");
			ModsConstReloadFailed();
			if (result == Reload::kBroken) RestoreTable();
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
