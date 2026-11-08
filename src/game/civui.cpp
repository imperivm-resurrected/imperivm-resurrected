// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "hooks/selectionguard.h"
#include "game/civs.h"
#include "core/log.h"

#include <cstdint>

#include "addr/world.h"

namespace {

typedef void *(__fastcall *InitForPlayer_t)(void *self, void *edx, void *player);
typedef void *(__fastcall *LocalRaceUser_t)(void *self, void *edx);
typedef void *(__cdecl *EmptySelectionMenu_t)(int race);

InitForPlayer_t g_origInitForPlayer = nullptr;
LocalRaceUser_t g_origCorners = nullptr, g_origQuickIcons = nullptr;
EmptySelectionMenu_t g_origEmptyMenu = nullptr;

void LogScope(const CivRaceAsBase &scope, const char *what)
{
	static int logged = 0;
	if (scope.slot && logged++ < 8) LogD("civilizations", "%s of civilization %d: with the interface of its base (%d)", what, scope.real, *scope.slot);
}

void *__fastcall HookInitForPlayer(void *self, void *edx, void *player)
{
	DropStaleEffectiveSelection(self);
	CivRaceAsBase own(player);
	CivRaceAsBase local(CivsLocalPlayer());
	LogScope(own, "bars and menus");
	return g_origInitForPlayer(self, edx, player);
}

void *__fastcall HookCorners(void *self, void *edx)
{
	CivRaceAsBase local(CivsLocalPlayer());
	LogScope(local, "corners");
	return g_origCorners(self, edx);
}

void *__fastcall HookQuickIcons(void *self, void *edx)
{
	CivRaceAsBase local(CivsLocalPlayer());
	LogScope(local, "quick icon bar");
	return g_origQuickIcons(self, edx);
}

void *__cdecl HookEmptyMenu(int race)
{
	const Civ *c = race >= 10 ? CivByIndex(race) : nullptr;
	return g_origEmptyMenu(c ? c->base : race);
}

}

void InstallCivUi(bool knownExe)
{
	if (!knownExe) return;
	struct Site {
		uintptr_t addr;
		void *detour;
		void **orig;
		const char *what;
	} sites[] = {
		{addr::kLoadCornerImages, reinterpret_cast<void *>(&HookCorners), reinterpret_cast<void **>(&g_origCorners), "CSelectionManager::LoadCornerImages"},
		{addr::kInitForPlayer, reinterpret_cast<void *>(&HookInitForPlayer), reinterpret_cast<void **>(&g_origInitForPlayer), "CSelectionManager::InitForPlayer"},
		{addr::kQuickIconBarLoadImages, reinterpret_cast<void *>(&HookQuickIcons), reinterpret_cast<void **>(&g_origQuickIcons), "CUIQuickIconBar::LoadImages"},
		{addr::kCreateEmptySelectionMenu, reinterpret_cast<void *>(&HookEmptyMenu), reinterpret_cast<void **>(&g_origEmptyMenu), "CreateEmptySelectionMenu"},
	};
	int failed = 0;
	for (const Site &s : sites) {
		if (HookCreate(reinterpret_cast<void *>(s.addr), s.detour, s.orig) != MH_OK || HookEnable(reinterpret_cast<void *>(s.addr)) != MH_OK) {
			LogE("civilizations", "could not hook %s", s.what);
			failed++;
		}
	}
	LogI("civilizations", "interface: %s", failed ? "hooked with errors" : "hooked");
}
