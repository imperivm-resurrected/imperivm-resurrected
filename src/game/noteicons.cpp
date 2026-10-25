// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "game/noteicons.h"
#include "hooks/gameload.h"
#include "core/log.h"
#include "addr/state.h"
#include "addr/world.h"

namespace {
typedef void(__cdecl *RestoreNoteIcons_t)();
typedef void *(__cdecl *LoadRleFile_t)(const char *path, void **image);
std::atomic<bool> g_restorePending{false};

// RestoreNoteIcons loads the note's own .bmp icon as an .rle file, which always fails, so no icon is
// recreated; GiveNote uses the notification icon instead, and so do we.
void *__cdecl LoadNoteIconImage(const char *, void **image)
{
	return reinterpret_cast<LoadRleFile_t>(addr::kLoadRleFile)("gameres/Icons/Notifications/note.rle", image);
}

bool RedirectLoadImage()
{
	uint8_t *at = reinterpret_cast<uint8_t *>(addr::kRestoreNoteIconsLoadImage);
	uint8_t original[5] = {0xe8}, patch[5] = {0xe8};
	const int32_t oldRel = static_cast<int32_t>(addr::kLoadRleFile - (addr::kRestoreNoteIconsLoadImage + 5));
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&LoadNoteIconImage) - (addr::kRestoreNoteIconsLoadImage + 5));
	memcpy(original + 1, &oldRel, 4);
	memcpy(patch + 1, &rel, 4);
	if (memcmp(at, original, sizeof(original)) != 0) return false;
	DWORD old = 0;
	if (!VirtualProtect(at, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, patch, sizeof(patch));
	VirtualProtect(at, sizeof(patch), old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, sizeof(patch));
	return true;
}

void AfterGameLoad(int result)
{
	if (result == 0) g_restorePending = true;
}
}

void NoteIconsGameTick()
{
	if (!g_restorePending.exchange(false)) return;
	if (!*reinterpret_cast<void **>(addr::kNoteManager)) return;
	reinterpret_cast<RestoreNoteIcons_t>(addr::kRestoreNoteIcons)();
}

void InstallNoteIcons(bool knownExe)
{
	if (!knownExe) return;
	if (!RedirectLoadImage()) {
		LogE("game", "notes: could not patch RestoreNoteIcons; the objective icons stay off the map after loading, as in the original game");
		return;
	}
	if (!InstallGameLoadHook(knownExe)) {
		LogE("game", "notes: could not hook the game load; the objective icons stay off the map after loading, as in the original game");
		return;
	}
	OnGameLoaded(&AfterGameLoad);
	LogD("game", "notes: the objective icons are recreated after loading a game");
}
