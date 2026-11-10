// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "game/healthbars.h"
#include "game/loadcurtain.h"
#include "game/viewprefs.h"

// The key ` of the game shows the health bars: kHealthBarMode is 0 hidden, 1 every bar, 2 and 3 the allies or the others of the
// local player (Ctrl+` cycles 1 to 3), and only CVXMapView::OnKey writes it. When the view of a match is made, the mod sets the mode
// chosen in the options the same way the key does. "My units", "enemy units" and "heroes only" are the mode 1 with the other bars
// left out while drawing. Enemies are decided as the game's modes 2 and 3 do: the allied flag (1) of the local player's relation[] towards
// the owner. Units that are neither the player's nor allied count as enemies, neutrals and animals included, as in the game's mode 3.
// Nothing of the match changes: the mode is local and is not saved or sent.
namespace {
// DrawVisibleObjHealthBar (0062ac80): thiscall, ECX = the device context, one argument (the visible object), ret 4.
typedef void(__fastcall *DrawBar_t)(void *dc, void *edx, char *visible);
// CVXVisibleMgr::RefreshAllBounds (00627db0): thiscall, one argument it does not read, ret 4; SetHealthBarMode calls it after the write.
typedef void(__fastcall *RefreshAllBounds_t)(void *manager, void *edx, void *unused);
typedef char *(__fastcall *IsKindOf_t)(void *self, void *edx, const char *name);

DrawBar_t g_origDrawBar = nullptr;
bool g_installed = false, g_pending = false, g_filterFailed = false;
HealthBarsChoice g_choice;
bool g_filtering = false;

const unsigned kOffVisibleScript = 0x60c, kOffFlags = 0x2c, kOffOwner = 0x70, kOffLocalPlayer = 0x12c8, kOffPlayerIndex = 8, kOffRelations = 0x24;
const unsigned kRelationAllied = 1;
const int kPlayers = 16;
const unsigned kHeroFlag = 0x1000000;

// The cast the game does itself before reading the object: the virtual base, then IsKindOf (slot 1) with the class name.
char *CastTo(char *object, uintptr_t className)
{
	char *base = object + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(object + 4) + 4);
	return reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(base))[1])(base, nullptr, *reinterpret_cast<const char *const *>(className));
}

UnitSide SideOf(char *script)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	char *local = game ? *reinterpret_cast<char **>(game + kOffLocalPlayer) : nullptr;
	char *owner = *reinterpret_cast<char **>(script + kOffOwner);
	const int index = owner ? *reinterpret_cast<int *>(owner + kOffPlayerIndex) : -1;
	const bool known = local && owner && index >= 0 && index < kPlayers;
	const bool allied = known && (*reinterpret_cast<unsigned *>(local + kOffRelations + index * 4) & kRelationAllied) != 0;
	return HealthBarsSide(local && owner == local, known, allied);
}

bool Keep(char *visible, const HealthBarsChoice &choice)
{
	char *shown = visible ? CastTo(visible, addr::kVisibleObjClassName) : nullptr;
	char *holder = shown ? *reinterpret_cast<char **>(shown + kOffVisibleScript) : nullptr;
	char *script = holder ? CastTo(holder, addr::kScriptObjClassName) : nullptr;
	if (!script) return true;
	const bool hero = (*reinterpret_cast<unsigned *>(script + kOffFlags) & kHeroFlag) != 0;
	return HealthBarsKeep(choice, hero, SideOf(script));
}

bool SafeKeep(char *visible, const HealthBarsChoice &choice)
{
	__try {
		return Keep(visible, choice);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_filterFailed = true;
		return true;
	}
}

void __fastcall HookDrawBar(void *dc, void *edx, char *visible)
{
	if (g_filtering && !g_filterFailed) {
		if (!HealthBarsFilterHolds(*reinterpret_cast<int *>(addr::kHealthBarMode))) {
			g_filtering = false;
			LogI("game", "health bars: the mode was changed with Ctrl, the game's own modes from now on");
		}
		else if (!SafeKeep(visible, g_choice)) {
			return;
		}
		if (g_filterFailed) LogE("game", "health bars: ERROR reading a unit, every bar is drawn");
	}
	g_origDrawBar(dc, edx, visible);
}

void OnMatchView() { g_pending = true; }

bool Apply(void *manager, int mode)
{
	__try {
		*reinterpret_cast<int *>(addr::kHealthBarMode) = mode;
		int unused = 0;
		reinterpret_cast<RefreshAllBounds_t>(addr::kVisibleMgrRefreshAllBounds)(manager, nullptr, &unused);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
}

void HealthBarsGameTick()
{
	if (!g_pending || !g_installed) return;
	void *manager = *reinterpret_cast<void **>(addr::kVisibleMgr);
	if (!manager || !*reinterpret_cast<void **>(addr::kGame)) return;
	g_pending = false;
	g_choice.owners = HealthBarsClampOwners(GetConfig().healthBarOwners);
	g_choice.heroesOnly = GetConfig().healthBarHeroesOnly;
	g_filtering = g_origDrawBar && HealthBarsFilters(g_choice);
	static const char *const kNames[] = {"hidden", "every unit", "your units", "enemy units"};
	if (Apply(manager, HealthBarsNativeMode(g_choice))) {
		LogI("game", "health bars at the start of the match: %s%s", kNames[g_choice.owners], g_choice.owners != kBarsNone && g_choice.heroesOnly ? ", heroes only" : "");
	}
	else LogE("game", "health bars: ERROR setting them at the start of the match");
}

void InstallHealthBars(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookCreate(reinterpret_cast<void *>(addr::kDrawVisibleObjHealthBar), reinterpret_cast<void *>(&HookDrawBar), reinterpret_cast<void **>(&g_origDrawBar)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kDrawVisibleObjHealthBar)) == MH_OK;
	if (!ok) g_origDrawBar = nullptr;
	LoadCurtainOnMatchView(&OnMatchView);
	g_installed = true;
	LogAt(ok ? kLogInfo : kLogError, "game", "health bars at the start of a match: %s", ok ? "ready" : "NOT available for your units, enemy units and heroes only");
}
