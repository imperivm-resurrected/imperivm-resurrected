// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "addr/state.h"
#include "addr/world.h"
#include "core/config.h"
#include "core/log.h"
#include "game/gamelayout.h"
#include "game/healthbars.h"
#include "game/loadcurtain.h"
#include "game/viewprefs.h"
#include "game/gamehook.h"

// The key ` of the game shows the health bars: kHealthBarMode is 0 hidden, 1 every bar, 2 and 3 the allies or the others of the
// local player, and only CVXMapView::OnKey (00627ef0) writes it: a tap shows or hides, held longer than 500 ms it hides again on
// release, and Ctrl+` writes (mode + 1) % 3 + 1. The options and the key are one setting: the mod sets the options' choice when the
// view of a match is made and whenever the options change, and a change of the key is written back to the options (Ctrl+` moves
// through every unit, my units and enemy units instead of the game's modes 2 and 3). "My units", "enemy units" and "heroes only" are
// the mode 1 with the other bars left out while drawing. Enemies are decided as the game's modes 2 and 3 do: the allied flag (1) of the
// local player's relation[] towards the owner. Units that are neither the player's nor allied count as enemies, neutrals and animals
// included, as in the game's mode 3. Nothing of the match changes: the mode is local and is not sent.
namespace {
// DrawVisibleObjHealthBar (0062ac80): thiscall, ECX = the device context, one argument (the visible object), ret 4.
typedef void(__fastcall *DrawBar_t)(void *dc, void *edx, char *visible);
// CVXVisibleMgr::RefreshAllBounds (00627db0): thiscall, one argument it does not read, ret 4; SetHealthBarMode calls it after the write.
typedef void(__fastcall *RefreshAllBounds_t)(void *manager, void *edx, void *unused);
typedef char *(__fastcall *IsKindOf_t)(void *self, void *edx, const char *name);

DrawBar_t g_origDrawBar = nullptr;
bool g_installed = false, g_pending = false, g_filterFailed = false, g_active = false;
HealthBarsChoice g_choice;
bool g_filtering = false;
// The game's mode the mod last set, to tell a press of the key; whose bars the key shows again after hiding them.
int g_lastNative = 0, g_lastShown = kBarsAll;

const unsigned kOffVisibleScript = 0x60c, kOffFlags = 0x2c, kOffOwner = 0x70;
const unsigned kRelationAllied = 1;
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
	char *local = game ? layout::LocalPlayer(game) : nullptr;
	char *owner = *reinterpret_cast<char **>(script + kOffOwner);
	const int index = owner ? Field<int>(owner, layout::kPlayerId) : -1;
	const bool known = local && owner && index >= 0 && index < layout::kPlayerCount;
	const bool allied = known && (Field<unsigned>(local, layout::kPlayerDiplomacy + index * 4) & kRelationAllied) != 0;
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
	if (g_filtering && !g_filterFailed && HealthBarsFilterHolds(*reinterpret_cast<int *>(addr::kHealthBarMode))) {
		if (!SafeKeep(visible, g_choice)) return;
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

namespace {
// The view the bars were set on: another one is another match, which sets them again when it starts.
void *g_manager = nullptr;

void Set(void *manager, const char *why)
{
	g_manager = manager;
	if (g_choice.owners != kBarsNone) g_lastShown = g_choice.owners;
	g_filtering = g_origDrawBar && HealthBarsFilters(g_choice);
	const int mode = HealthBarsNativeMode(g_choice);
	static const char *const kNames[] = {"hidden", "every unit", "your units", "enemy units"};
	if (Apply(manager, mode)) {
		g_lastNative = mode;
		LogI("game", "health bars %s: %s%s", why, kNames[g_choice.owners], g_choice.owners != kBarsNone && g_choice.heroesOnly ? ", heroes only" : "");
	}
	else {
		g_active = false;
		LogE("game", "health bars: ERROR setting them %s", why);
	}
}
}

void HealthBarsGameTick()
{
	if (!g_installed) return;
	void *manager = *reinterpret_cast<void **>(addr::kVisibleMgr);
	if (!manager || !*reinterpret_cast<void **>(addr::kGame)) {
		g_active = false;
		return;
	}
	if (g_active && manager != g_manager && !g_pending) {
		g_active = false;
		return;
	}
	const Config &config = GetConfig();
	const int owners = HealthBarsClampOwners(config.healthBarOwners);
	if (g_pending || (g_active && (owners != g_choice.owners || config.healthBarHeroesOnly != g_choice.heroesOnly))) {
		const bool start = g_pending;
		g_pending = false;
		g_active = true;
		g_choice.owners = owners;
		g_choice.heroesOnly = config.healthBarHeroesOnly;
		Set(manager, start ? "at the start of the match" : "changed in the options");
		return;
	}
	if (!g_active) return;
	const int now = *reinterpret_cast<int *>(addr::kHealthBarMode);
	if (now == g_lastNative) return;
	// Without the drawing hook only the game's own modes work: its key keeps doing what it always did.
	if (!g_origDrawBar) {
		g_lastNative = now;
		return;
	}
	g_choice.owners = HealthBarsOwnersAfterKey(g_choice.owners, g_lastShown, g_lastNative, now);
	Set(manager, "changed with the key");
	MutableConfig().healthBarOwners = g_choice.owners;
	SaveHealthBarOwners();
}

void InstallHealthBars(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookFunction(addr::kDrawVisibleObjHealthBar, &HookDrawBar, &g_origDrawBar);
	if (!ok) g_origDrawBar = nullptr;
	LoadCurtainOnMatchView(&OnMatchView);
	g_installed = true;
	LogAt(ok ? kLogInfo : kLogError, "game", "health bars at the start of a match: %s", ok ? "ready" : "NOT available for your units, enemy units and heroes only");
}
