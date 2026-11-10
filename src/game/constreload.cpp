// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <string>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "addr/classcache.h"
#include "addr/constcache.h"
#include "addr/engine.h"
#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/cmdcheck.h"
#include "game/constreload.h"
#include "game/scriptevents.h"
#include "hooks/gamestate.h"
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
//
// FullHotReload (experimental) goes further, for rulesets made only of patches to data/ (classes, commands, items, AI...): the game
// reads those once per process, in InitGameDataSystems, and frees them in ShutdownGameDataSystems, which it only runs at exit. Here both
// run between two matches, at the same point, with the const.ini reload before them and the "already looked up" class pointers set back
// after Shutdown. If anything fails after the game's data has been freed the game cannot go on: it restarts with the chosen rules.
namespace {
typedef int(__cdecl *ReloadConstIni_t)();
typedef const char *(__fastcall *HashIniGetInt_t)(void *self, void *edx, const char *section, const char *key, int *out);
typedef void *(__fastcall *LoadGameData_t)(void *self, void *edx);
typedef void(__cdecl *GameDataSystems_t)();
typedef void *(__fastcall *FindClass_t)(void *registry, void *edx, const char *name);

LoadGameData_t g_origLoadGameData = nullptr;

const unsigned kDecorTypes = 256;
const unsigned kFeedbackTypes = 56; // 28 ids with two variants, ending where the guard word starts

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

// ---- FullHotReload ----

struct MemoryCounters {
	DWORD cb, pageFaults;
	SIZE_T peakWorkingSet, workingSet, quotaPeakPaged, quotaPaged, quotaPeakNonPaged, quotaNonPaged, pagefile, peakPagefile;
};
typedef BOOL(WINAPI *GetProcessMemoryInfo_t)(HANDLE process, MemoryCounters *counters, DWORD size);

SIZE_T g_lastCommit = 0;

// How much memory the process holds and whether its heaps are consistent, to see what a reload leaks and whether something wrote into freed
// memory (HeapValidate walks every block of every heap of the process: it takes a moment and says nothing about the game's own small-block heap).
void LogProcessState(const char *when)
{
	g_crashLogQuiet++;
	__try {
		MemoryCounters counters = {};
		counters.cb = sizeof(counters);
		HMODULE kernel = GetModuleHandleA("kernel32.dll");
		const GetProcessMemoryInfo_t info = kernel ? reinterpret_cast<GetProcessMemoryInfo_t>(GetProcAddress(kernel, "K32GetProcessMemoryInfo")) : nullptr;
		const bool haveInfo = info && info(GetCurrentProcess(), &counters, sizeof(counters));
		MEMORYSTATUSEX status = {};
		status.dwLength = sizeof(status);
		const bool haveStatus = GlobalMemoryStatusEx(&status) != 0;
		HANDLE heaps[64];
		const DWORD heapCount = GetProcessHeaps(64, heaps);
		const DWORD start = GetTickCount();
		unsigned corrupt = 0;
		for (DWORD i = 0; i < heapCount && i < 64; i++) {
			if (!HeapValidate(heaps[i], 0, nullptr)) corrupt++;
		}
		const SIZE_T commit = haveInfo ? counters.pagefile : 0;
		LogI("game", "full reload (%s): process commit %u MB (%+d MB since the last line), working set %u MB, address space in use %u MB, %lu heaps, %u failed HeapValidate (%lu ms)",
			when, static_cast<unsigned>(commit >> 20), static_cast<int>((static_cast<long long>(commit) - static_cast<long long>(g_lastCommit)) >> 20),
			haveInfo ? static_cast<unsigned>(counters.workingSet >> 20) : 0u,
			haveStatus ? static_cast<unsigned>((status.ullTotalVirtual - status.ullAvailVirtual) >> 20) : 0u, static_cast<unsigned long>(heapCount), corrupt,
			static_cast<unsigned long>(GetTickCount() - start));
		g_lastCommit = commit;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogE("game", "full reload (%s): the process state could not be read (the heap may be damaged)", when);
	}
	g_crashLogQuiet--;
}

void *Pointer(uintptr_t address)
{
	return *reinterpret_cast<void *const *>(address);
}

// The class pointers that were looked up once and kept, and what says they were looked up (see addr/classcache.h).
void ResetClassCaches()
{
	const unsigned pointerCount = sizeof(addr::kClassPointerCaches) / sizeof(addr::kClassPointerCaches[0]);
	const unsigned flagCount = sizeof(addr::kClassFlagGuards) / sizeof(addr::kClassFlagGuards[0]);
	unsigned pointers = 0, flags = 0;
	for (uintptr_t p : addr::kClassPointerCaches) {
		if (*Word(p)) pointers++;
		*Word(p) = 0;
	}
	for (uintptr_t f : addr::kClassFlagGuards) {
		if (*Word(f)) flags++;
		*Word(f) = 0;
	}
	// The feedback classes are looked up once, for all ids, when the guard word is 0.
	unsigned feedback = 0;
	for (unsigned i = 0; i < kFeedbackTypes; i++) {
		if (Word(addr::kFeedbackTypeCache)[i]) feedback++;
		Word(addr::kFeedbackTypeCache)[i] = 0;
	}
	const unsigned feedbackInit = *Word(addr::kFeedbackTypeCacheInit);
	*Word(addr::kFeedbackTypeCacheInit) = 0;
	LogI("game", "full reload: feedback classes set back: %u of %u were set (looked up: %u)", feedback, kFeedbackTypes, feedbackInit);
	// BaseTownhall is kept by bit 2 of the word whose bits 0 and 1 guard the loyalty radii.
	*Word(addr::kLoyaltyRadiusFlags) &= ~7u;
	// CounterUnits.xml is read again by the next AI that starts.
	*Word(addr::kCounterUnits + 4) = 0;
	LogI("game", "full reload: class pointers set back: %u of %u were set, %u of %u lookup flags; CounterUnits will be read again", pointers, pointerCount,
		flags, flagCount);
}

// Shutdown freed the entities (ClearEntityCache) but the game keeps pointers to some of them: the array of decor entities that the passability
// grid and the map view read (g_decorEntities, filled by LoadDecorsIni), and the marks that say "the decorations were read for this season".
// The first match of a process reads decors.ini in CreateWorldCore only when the season is not the one in DAT_00996ffc; the values of a
// new process are 0 everywhere, so that is what they are set to.
void ResetEntityCaches()
{
	void **decors = reinterpret_cast<void **>(addr::kDecorEntities);
	unsigned decorCount = 0;
	for (unsigned i = 0; i < kDecorTypes; i++) {
		if (decors[i]) decorCount++;
		decors[i] = nullptr;
	}
	const unsigned loaded = *Word(addr::kDecorsLoaded), loadedSeason = *Word(addr::kDecorsLoadedSeason), season = *Word(addr::kEntityCacheSeason);
	*Word(addr::kDecorsLoaded) = 0;
	*Word(addr::kDecorsLoadedSeason) = 0;
	*Word(addr::kEntityCacheSeason) = 0;
	LogI("game", "full reload: decor entities set back: %u of %u were set; decors.ini marks (loaded %u, season %u) and the entity cache season (%u) set to 0",
		decorCount, kDecorTypes, loaded, loadedSeason, season);
}

// Makes the game free its data tables and make them again. Only plain data in here, so the __try can protect it.
bool RebuildGameData(DWORD *shutdownMs, DWORD *initMs)
{
	bool ok = false;
	g_crashLogQuiet++;
	__try {
		const DWORD start = GetTickCount();
		reinterpret_cast<GameDataSystems_t>(addr::kShutdownGameDataSystems)();
		const DWORD freed = GetTickCount();
		ResetClassCaches();
		ResetEntityCaches();
		reinterpret_cast<GameDataSystems_t>(addr::kInitGameDataSystems)();
		*shutdownMs = freed - start;
		*initMs = GetTickCount() - freed;
		ok = true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogE("game", "full reload: the game faulted while freeing or making its data tables");
		ok = false;
	}
	g_crashLogQuiet--;
	return ok;
}

// What Init makes has to be there. The class registry is still empty: LoadGameData fills it, after this.
bool TablesExist()
{
	const struct {
		const char *name;
		uintptr_t address;
	} tables[] = {{"script class registry", addr::kRegistry}, {"script library", addr::kScriptLibrary}, {"command registry", addr::kCmdRegistry},
		{"persistent class registry", addr::kPersistClassRegistry}, {"item definitions", addr::kBaseItemDefs}, {"custom item definitions", addr::kCustomItemDefs},
		{"hero skills", addr::kHeroSkillTable}, {"unit specials", addr::kUnitSpecialTable}};
	bool ok = true;
	g_crashLogQuiet++;
	__try {
		for (const auto &t : tables) {
			if (Pointer(t.address)) continue;
			LogE("game", "full reload: the %s was not made", t.name);
			ok = false;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	g_crashLogQuiet--;
	return ok;
}

bool SafeCommandCost(const char *name, int *gold, int *food)
{
	bool found = false;
	g_crashLogQuiet++;
	__try {
		found = GameCommandCost(name, gold, food);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		found = false;
	}
	g_crashLogQuiet--;
	return found;
}

bool SafeHasClass(const char *name)
{
	bool found = false;
	g_crashLogQuiet++;
	__try {
		void *registry = Pointer(addr::kRegistry);
		found = registry && reinterpret_cast<FindClass_t>(addr::kFindClassByName)(registry, nullptr, name) != nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		found = false;
	}
	g_crashLogQuiet--;
	return found;
}

// The classes the game looks up by name and keeps (the pointers set back in ResetClassCaches). A missing one is only logged: a ruleset may
// remove a class, and the game copes with a null pointer there.
void CheckCachedClassNames()
{
	static const char *const kNames[] = {"Tower", "BaseTownhall", "BaseVillage", "Outpost", "BaseShipyard", "TTent", "Barrack", "RBarracks", "MBarracks",
		"CBarracks", "IBarracks", "BBarracks", "EBarracks", "TBarracks", "Sentry", "Military", "BaseMage", "ShipBattle", "Animal", "Peaceful", "Peasant",
		"PeasantAmbient", "RamUnit", "Gate", "LionF", "LionM", "Boar", "Bear", "Wolf", "HeroGrave", "DefItemHolder", "FormDir", "Trader", "CartTrader",
		"Tradecar_Donkey", "Camel", "ShipS"};
	unsigned found = 0, total = 0;
	for (const char *name : kNames) {
		total++;
		if (SafeHasClass(name)) found++;
		else LogW("game", "full reload: the class %s that the game looks up by name is not in the new registry", name);
	}
	LogI("game", "full reload (after LoadGameData): %u of %u classes the game looks up by name are in the new registry", found, total);
}

// The log itself says which rules are live: the cost the game's command table holds for the Roman hastatus.
bool CommandSanity(const char *when)
{
	int gold = 0, food = 0;
	if (!SafeCommandCost("trainRHastatus", &gold, &food)) {
		LogE("game", "full reload (%s): the command table has no trainRHastatus", when);
		return false;
	}
	LogI("game", "full reload (%s): trainRHastatus costgold = %d, costfood = %d; %d commands in the table", when, gold, food, GameCommandCount());
	return true;
}

void RestartNow(const char *why)
{
	LogE("game", "full reload: %s; the game restarts with the chosen rules", why);
	ModsRestartGame();
}

// Before the match is created. Returns false if the game could not be made to use the mounted rules (the caller restarts).
bool ReloadFull()
{
	const DWORD start = GetTickCount();
	if (GameIsMultiplayer()) {
		LogW("game", "full reload: not for a multiplayer game");
		return false;
	}
	if (*reinterpret_cast<const int *>(addr::kLoadingSavedGame)) {
		LogW("game", "full reload: not for a saved game (its scripts are already compiled in the library that would be freed)");
		return false;
	}
	if (!GuardsLookRight()) return false;
	if (UpdateCheckRunning()) {
		LogW("game", "full reload: the update check is still reading the table");
		return false;
	}
	LogI("game", "full reload: starting");
	LogProcessState("before");
	// The patched copies of every file are made again from the patches mounted now.
	VfsResetPatchedAll();
	if (!FileIsUsable()) return false;
	const DWORD patched = GetTickCount();
	if (ReloadTable() != Reload::kDone) return false;
	const DWORD constDone = GetTickCount();
	DWORD shutdownMs = 0, initMs = 0;
	if (!RebuildGameData(&shutdownMs, &initMs)) return false;
	if (!TablesExist()) return false;
	LogProcessState("after Init");
	// The event scripts were compiled in the library that is gone.
	ScriptEventsReset();
	LogI("game", "full reload: patches %lu ms, const.ini %lu ms, shutdown %lu ms, init %lu ms", static_cast<unsigned long>(patched - start),
		static_cast<unsigned long>(constDone - patched), static_cast<unsigned long>(shutdownMs), static_cast<unsigned long>(initMs));
	return CommandSanity("after Init");
}

// After LoadGameData has read the classes with the new tables.
void CheckFullReload(const void *loadResult)
{
	if (loadResult) {
		LogE("game", "full reload: LoadGameData reported an error after the data tables were made again");
		return;
	}
	const bool hastatus = SafeHasClass("RHastatus"), barracks = SafeHasClass("RBarracks");
	LogI("game", "full reload (after LoadGameData): class RHastatus %s, class RBarracks %s", hastatus ? "found" : "NOT FOUND", barracks ? "found" : "NOT FOUND");
	CheckCachedClassNames();
	LogProcessState("after LoadGameData");
	if (!hastatus || !barracks) RestartNow("the class registry lacks the Roman classes");
	else if (!CommandSanity("after LoadGameData")) RestartNow("the command table lacks the Roman commands");
}

void *__fastcall HookLoadGameData(void *self, void *edx)
{
	bool full = false;
	switch (ModsReloadPlan()) {
	case ModsReload::kConst: {
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
		break;
	}
	case ModsReload::kFull:
		if (ReloadFull()) {
			ModsConstReloadDone();
			full = true;
		}
		else {
			RestartNow("it could not be done");
		}
		break;
	case ModsReload::kRestart:
		RestartNow("the mounted rules differ from the loaded ones in more than a hot reload can change");
		break;
	default:
		break;
	}
	void *result = g_origLoadGameData(self, edx);
	if (full) CheckFullReload(result);
	return result;
}
}

void InstallConstReload(bool knownExe)
{
	if (!knownExe || !(GetConfig().constHotReload || GetConfig().fullHotReload)) return;
	if (HookCreate(reinterpret_cast<void *>(addr::kLoadGameData), reinterpret_cast<void *>(&HookLoadGameData), reinterpret_cast<void **>(&g_origLoadGameData)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kLoadGameData)) != MH_OK) {
		LogE("game", "const reload: could not hook LoadGameData; a ruleset change will still need a restart");
		return;
	}
	ModsConstReloadSetReady(GetConfig().constHotReload);
	ModsFullReloadSetReady(GetConfig().fullHotReload);
	LogI("game", "const reload: ready (ConstHotReload=%d, FullHotReload=%d)", GetConfig().constHotReload ? 1 : 0, GetConfig().fullHotReload ? 1 : 0);
}
