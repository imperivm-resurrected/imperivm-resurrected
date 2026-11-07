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
// chosen in the options the same way the key does. "Heroes" and "my units" are the mode 1 with the other bars left out while drawing.
// Nothing of the match changes: the mode is local and is not saved or sent.
namespace {
// DrawVisibleObjHealthBar (0062ac80): thiscall, ECX = the device context, one argument (the visible object), ret 4.
typedef void(__fastcall *DrawBar_t)(void *dc, void *edx, char *visible);
// CVXVisibleMgr::RefreshAllBounds (00627db0): thiscall, one argument it does not read, ret 4; SetHealthBarMode calls it after the write.
typedef void(__fastcall *RefreshAllBounds_t)(void *manager, void *edx, void *unused);
typedef char *(__fastcall *IsKindOf_t)(void *self, void *edx, const char *name);

DrawBar_t g_origDrawBar = nullptr;
bool g_installed = false, g_pending = false, g_filterFailed = false;
int g_filter = kBarsAll;

const unsigned kOffVisibleScript = 0x60c, kOffFlags = 0x2c, kOffOwner = 0x70, kOffLocalPlayer = 0x12c8;
const unsigned kHeroFlag = 0x1000000;

// The cast the game does itself before reading the object: the virtual base, then IsKindOf (slot 1) with the class name.
char *CastTo(char *object, uintptr_t className)
{
	char *base = object + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(object + 4) + 4);
	return reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(base))[1])(base, nullptr, *reinterpret_cast<const char *const *>(className));
}

bool Keep(char *visible, int filter)
{
	char *shown = visible ? CastTo(visible, addr::kVisibleObjClassName) : nullptr;
	char *holder = shown ? *reinterpret_cast<char **>(shown + kOffVisibleScript) : nullptr;
	char *script = holder ? CastTo(holder, addr::kScriptObjClassName) : nullptr;
	if (!script) return true;
	char *game = *reinterpret_cast<char **>(addr::kGame);
	const bool hero = (*reinterpret_cast<unsigned *>(script + kOffFlags) & kHeroFlag) != 0;
	const bool mine = game && *reinterpret_cast<char **>(script + kOffOwner) == *reinterpret_cast<char **>(game + kOffLocalPlayer);
	return HealthBarsKeep(filter, hero, mine);
}

bool SafeKeep(char *visible, int filter)
{
	__try {
		return Keep(visible, filter);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_filterFailed = true;
		return true;
	}
}

void __fastcall HookDrawBar(void *dc, void *edx, char *visible)
{
	if (g_filter != kBarsAll && !g_filterFailed) {
		if (!HealthBarsFilterHolds(*reinterpret_cast<int *>(addr::kHealthBarMode))) {
			g_filter = kBarsAll;
			LogI("game", "health bars: the mode was changed with Ctrl, the game's own modes from now on");
		}
		else if (!SafeKeep(visible, g_filter)) {
			return;
		}
		if (g_filterFailed) LogE("game", "health bars: ERROR reading a unit, every bar is drawn");
	}
	g_origDrawBar(dc, edx, visible);
}

void OnMatchView() { g_pending = true; }

bool Apply(void *manager, int start)
{
	__try {
		*reinterpret_cast<int *>(addr::kHealthBarMode) = HealthBarsNativeMode(start);
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
	const int start = HealthBarsClampStart(GetConfig().healthBarsAtStart);
	g_filter = g_origDrawBar ? HealthBarsFilter(start) : kBarsAll;
	static const char *const kNames[] = {"hidden", "every unit", "heroes only", "your units only"};
	if (Apply(manager, start)) LogI("game", "health bars at the start of the match: %s", kNames[start]);
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
	LogAt(ok ? kLogInfo : kLogError, "game", "health bars at the start of a match: %s", ok ? "ready" : "NOT available for heroes and your units only");
}
