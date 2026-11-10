// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// What the game keeps about its classes and data tables between matches, for game/hotreload.cpp. The tables
// below are freed by ShutdownGameDataSystems and made again by InitGameDataSystems.
namespace addr {

// CCounterUnits: its word at +4 says "CounterUnits.xml already read" (the table holds class names of its own, not class pointers).
inline constexpr uintptr_t kCounterUnits = 0x008c52d8;
// Pointers to classes (CVXScriptClass) that functions look up by name the first time they need them and keep, so they point into the
// class registry that Shutdown frees. Each one is set back to 0: the null-guarded ones look the class up again, the flag-guarded ones
// when their flag (next list) is 0 too. The writers of every one are the function that looks it up and nothing else (the last one,
// g_pPendingMutateClass, is set by Unit::Mutate and cleared when the unit has mutated: a game that ends in between leaves it set).
constexpr uintptr_t kClassPointerCaches[] = {0x008c8db8, 0x008c8df8, 0x008c8dfc, 0x008c8e00, 0x008c8e04, 0x008c8e08, 0x008c8e10, 0x008c8e14, 0x008c8e18, 0x008c8e1c, 0x008c8e20, 0x008c8e24, 0x008c8e28, 0x008c8e2c, 0x009a7220, 0x009a7228, 0x009a7230, 0x009a7238, 0x009a7240, 0x009a7248, 0x009a7250, 0x009a7258, 0x009a7260, 0x009a7268, 0x009bdb50, 0x009bdb54, 0x009bdb58, 0x009bdb5c, 0x009bdb60, 0x009bdb68, 0x009bdb6c, 0x009bf7c8, 0x009c082c, 0x009c0930, 0x009c0964, 0x009c0968, 0x009c096c, 0x009c0970, 0x009c0974};
// The "already looked up" flags of some of those pointers (32-bit words: 'or dword ptr [flag], bit').
constexpr uintptr_t kClassFlagGuards[] = {0x008c8e0c, 0x009a7224, 0x009a722c, 0x009a7234, 0x009a723c, 0x009a7244, 0x009a724c, 0x009a7254, 0x009a725c, 0x009a7264, 0x009a726c, 0x009bdb64, 0x009bdb70};
// CreateFeedbackById looks up the class of each feedback effect (Heal, Death, Parry, GetItem...) once and keeps all 56 pointers in an array
// (28 ids, two variants each), guarded by the word after it. They were stale after Shutdown: the first item script that called it made a
// feedback object of a freed class, whose script count was garbage, and operator new failed.
inline constexpr uintptr_t kFeedbackTypeCache = 0x00996e40;
inline constexpr uintptr_t kFeedbackTypeCacheInit = 0x00996f20;
// Which season the entity cache and the decorations were made for (DAT_00996ffc is CreateWorldCore's, g_decorsLoaded* are LoadDecorsIni's).
// A match flushes both caches and reads decors.ini again only when the season is not the one they were made for, so after Shutdown
// freed the entities these have to go back to what they are in a new process (0): the array of decor entities is in state.h.
inline constexpr uintptr_t kEntityCacheSeason = 0x00996ffc;
// The tables Init makes again; a null after Init means it failed. (kCustomItemDefs, 009a7214, is in state.h.)
inline constexpr uintptr_t kBaseItemDefs = 0x009a7218;
inline constexpr uintptr_t kDecorsLoadedSeason = 0x009b7e78;
inline constexpr uintptr_t kDecorsLoaded = 0x009b7e7c;
inline constexpr uintptr_t kHeroSkillTable = 0x009bf7d8;
inline constexpr uintptr_t kUnitSpecialTable = 0x009bf7dc;
inline constexpr uintptr_t kPersistClassRegistry = 0x00a76e84;

}
