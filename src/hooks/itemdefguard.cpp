// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "hooks/itemdefguard.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "addr/state.h"
#include "addr/world.h"

// Every world the game creates reloads CurrentGame/itemsCustom.xml (CVXGame::CreateWorldCore), and the loader frees the item
// types it had. An adventure that changes map keeps its party, and their items still point at the freed types: saving one of
// them (CVXItem::Persist writes its type's id) reads freed memory and crashes. No adventure of the original game changes map,
// so it never happened there. The types of the new load that are the same as old ones are swapped back for the old ones, so
// every item, old or new, points at a live type; old types that changed or are gone are kept alive, never freed.
namespace {
typedef void *(__fastcall *LoadFile_t)(char *reader, void *unused, const char *path);
LoadFile_t g_origLoadFile = nullptr;

// CItemsReader: std::vector<CItemDef *> at +0x08 (begin, end, capacity).
const unsigned kBegin = 0x08, kEnd = 0x0c, kCapacity = 0x10, kReaderLocale = 0x24;
// CItemDef (0x1e0 bytes): id, name, description, image and sound strings, then the values, then 9 script strings.
const unsigned kStringSize = 0x1c;
const unsigned kTexts[] = {0x00, 0x1c, 0x38, 0x54, 0x70};
const unsigned kValuesFirst = 0x90, kValuesEnd = 0xd8;
const unsigned kSerial = 0xd8;
const unsigned kScripts = 0xdc, kScriptCount = 9;
// The translations of itemsCustom.loc.xml the type was read with (CItemDef::GetDescription); the loader frees them on reload.
const unsigned kLocale = 0x1dc;

// The game's std::string (MSVC 7.1): text inline at +4 while capacity (+0x18) < 16, else a pointer at +4; size at +0x14.
bool SameString(const char *a, const char *b)
{
	const uint32_t sizeA = *reinterpret_cast<const uint32_t *>(a + 0x14), sizeB = *reinterpret_cast<const uint32_t *>(b + 0x14);
	if (sizeA != sizeB) return false;
	const char *textA = *reinterpret_cast<const uint32_t *>(a + 0x18) < 16 ? a + 4 : *reinterpret_cast<const char *const *>(a + 4);
	const char *textB = *reinterpret_cast<const uint32_t *>(b + 0x18) < 16 ? b + 4 : *reinterpret_cast<const char *const *>(b + 4);
	return memcmp(textA, textB, sizeA) == 0;
}

bool SameType(const char *a, const char *b)
{
	for (unsigned at : kTexts)
		if (!SameString(a + at, b + at)) return false;
	if (memcmp(a + kValuesFirst, b + kValuesFirst, kValuesEnd - kValuesFirst) != 0) return false;
	for (unsigned i = 0; i < kScriptCount; i++)
		if (!SameString(a + kScripts + i * kStringSize, b + kScripts + i * kStringSize)) return false;
	return true;
}

// Points every old type at the new translations, then puts back in the new list each old type that is the same as a new
// one. Returns how many were put back, or -1 if reading them failed.
int SwapBackSameTypes(char **begin, char **end, char **old, size_t count, void *locale)
{
	int kept = 0;
	__try {
		for (size_t i = 0; i < count; i++)
			if (old[i]) *reinterpret_cast<void **>(old[i] + kLocale) = locale;
		for (char **slot = begin; slot != end; ++slot) {
			for (size_t i = 0; i < count; i++) {
				char *type = old[i];
				if (!type || !SameType(type, *slot)) continue;
				// The new copy is left alive too: nothing else points at it, and freeing it needs the game's own heap.
				*reinterpret_cast<int *>(type + kSerial) = *reinterpret_cast<int *>(*slot + kSerial);
				*slot = type;
				old[i] = nullptr;
				kept++;
				break;
			}
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
	return kept;
}

void *__fastcall HookLoadFile(char *reader, void *unused, const char *path)
{
	if (reader != *reinterpret_cast<char **>(addr::kCustomItemDefs)) return g_origLoadFile(reader, unused, path);
	char **&begin = *reinterpret_cast<char ***>(reader + kBegin);
	char **&end = *reinterpret_cast<char ***>(reader + kEnd);
	std::vector<char *> old(begin, end);
	if (old.empty()) return g_origLoadFile(reader, unused, path);
	// The loader frees what the list holds: with an empty list it frees nothing. The old list's own buffer is left behind.
	begin = end = *reinterpret_cast<char ***>(reader + kCapacity) = nullptr;
	void *result = g_origLoadFile(reader, unused, path);
	const int kept = SwapBackSameTypes(begin, end, old.data(), old.size(), *reinterpret_cast<void **>(reader + kReaderLocale));
	if (kept < 0) {
		LogE("game", "items: could not compare the item types of %s; the old ones are kept alive", path ? path : "?");
		return result;
	}
	LogD("game", "items: %s reloaded; %d item types kept from before, %d old ones left alive for the items that use them", path ? path : "?",
		kept, static_cast<int>(old.size()) - kept);
	return result;
}
}

void InstallItemDefGuard(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kItemsReaderLoadFile);
	if (HookCreate(target, reinterpret_cast<void *>(&HookLoadFile), reinterpret_cast<void **>(&g_origLoadFile)) != MH_OK || HookEnable(target) != MH_OK) {
		LogE("game", "items: could not hook the item types loader; saving after an adventure changes map can crash, as in the original game");
		return;
	}
	LogD("game", "items: item types already in use survive a reload of itemsCustom.xml");
}
