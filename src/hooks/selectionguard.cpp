// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>

#include "hooks/selectionguard.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "addr/script.h"
#include "addr/ui.h"
#include "addr/engine.h"
#include "addr/world.h"

namespace {
typedef void(__fastcall *RebuildCommandList_t)(char *manager);
typedef void(__fastcall *SelectionRevalidate_t)(char *selection);
typedef void(__fastcall *SelectionClear_t)(char *selection);
typedef void *(__fastcall *IsKindOf_t)(char *object, void *unused, const char *className);
RebuildCommandList_t g_origRebuild = nullptr;

const unsigned kEffectiveSelection = 0xf0;
const unsigned kSelectionHead = 0x18;
const unsigned kSelectionSize = 0x1c;
const unsigned kNodeHandle = 8;
const unsigned kObjectHandle = 8;

int GoneObjects(char *selection)
{
	char **table = reinterpret_cast<char **>(addr::kHandleTable);
	char *head = *reinterpret_cast<char **>(selection + kSelectionHead);
	const unsigned size = *reinterpret_cast<unsigned *>(selection + kSelectionSize);
	if (!head) return 0;
	int gone = 0;
	unsigned seen = 0;
	for (char *node = *reinterpret_cast<char **>(head); node && node != head && seen <= size; node = *reinterpret_cast<char **>(node), seen++) {
		if (!table[*reinterpret_cast<unsigned short *>(node + kNodeHandle)]) gone++;
	}
	return gone;
}

// Whether the object behind a selected handle is still a map object with that handle. When an adventure goes back to a
// map it left, the world is rebuilt but the effective selection still lists the handles of the old one; by then a handle
// may belong to a script object or to freed memory (a "Query" string where the vtable should be), and
// CSelectionManager::ApplySelection calls its virtual functions to clear the "selected" flag: a crash.
bool IsSelectedMapObject(char *object, unsigned short handle)
{
	__try {
		if (*reinterpret_cast<unsigned short *>(object + kObjectHandle) != handle) return false;
		// A virtual table lives in gbr.exe's .rdata and its functions in its .text; slot 1 is IsKindOf.
		const uintptr_t vtable = *reinterpret_cast<uintptr_t *>(object);
		if (vtable < addr::kGameRdataBegin || vtable >= addr::kGameRdataEnd) return false;
		const uintptr_t isKindOf = reinterpret_cast<uintptr_t *>(vtable)[1];
		if (isKindOf < addr::kGameTextBegin || isKindOf >= addr::kGameTextEnd) return false;
		return reinterpret_cast<IsKindOf_t>(isKindOf)(object, nullptr, *reinterpret_cast<const char **>(addr::kMapObjClassName)) != nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void __fastcall HookRebuildCommandList(char *manager)
{
	if (manager) {
		char *selection = manager + kEffectiveSelection;
		const int gone = GoneObjects(selection);
		if (gone) {
			LogW("game", "selection: %d selected objects no longer exist; the selection is checked again before its commands are rebuilt", gone);
			reinterpret_cast<SelectionRevalidate_t>(addr::kSelectionRevalidate)(selection);
		}
	}
	g_origRebuild(manager);
}
}

void DropStaleEffectiveSelection(void *manager)
{
	if (!manager) return;
	char *selection = static_cast<char *>(manager) + kEffectiveSelection;
	char **table = reinterpret_cast<char **>(addr::kHandleTable);
	char *head = *reinterpret_cast<char **>(selection + kSelectionHead);
	const unsigned size = *reinterpret_cast<unsigned *>(selection + kSelectionSize);
	if (!head || !size) return;
	int stale = 0;
	unsigned seen = 0;
	for (char *node = *reinterpret_cast<char **>(head); node && node != head && seen <= size; node = *reinterpret_cast<char **>(node), seen++) {
		const unsigned short handle = *reinterpret_cast<unsigned short *>(node + kNodeHandle);
		char *object = table[handle];
		if (object && !IsSelectedMapObject(object, handle)) stale++;
	}
	if (!stale) return;
	LogW("game", "selection: %d of %u selected objects belong to a world that is gone; the selection is emptied before the new world's bars are set up",
		stale, size);
	reinterpret_cast<SelectionClear_t>(addr::kSelectionClear)(selection);
}

void InstallSelectionGuard(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kRebuildCommandList);
	if (HookCreate(target, reinterpret_cast<void *>(&HookRebuildCommandList), reinterpret_cast<void **>(&g_origRebuild)) != MH_OK ||
		HookEnable(target) != MH_OK) {
		LogE("game", "selection: could not hook the rebuilding of the command bar; a crash of the original game when a selected object disappears stays possible");
		return;
	}
	LogD("game", "selection: objects that no longer exist are dropped before the command bar is rebuilt");
}
