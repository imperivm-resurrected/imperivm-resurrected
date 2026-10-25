// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "hooks/hookcount.h"

#include "addr/ui.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/loadcurtain.h"
#include "hooks/gameload.h"

// A match exists, with its bars and windows, before its view is painted for the first time: until then the screen keeps
// what was there (the menus, the dialog that started it) under the new bars. From the moment a match is created or loaded
// until its view has been painted and shown whole, the screen keeps its last picture.
namespace {
const DWORD kMaxMs = 30000;

std::atomic<bool> g_up{false}, g_painted{false};
std::atomic<DWORD> g_since{0};

typedef void *(__fastcall *CreateGame_t)(void *ecx, void *edx);
typedef void *(__cdecl *LoadReplay_t)(const char *path);
typedef bool(__cdecl *MainMenuLoop_t)(void *settings, int *args);
typedef void *(__fastcall *MapViewCtor_t)(void *self, void *edx, void *a, void *b, void *c, int d);
typedef int(__fastcall *MapViewRender_t)(void *self, void *edx);
typedef int(__fastcall *ConquestMap_t)(void *ecx, void *edx);
CreateGame_t g_origCreateGame = nullptr;
ConquestMap_t g_origConquestMap = nullptr;
LoadReplay_t g_origLoadReplay = nullptr;
MainMenuLoop_t g_origMainMenuLoop = nullptr;
MapViewCtor_t g_origMapViewCtor = nullptr;
MapViewRender_t g_origMapViewRender = nullptr;

void Lower(const char *why)
{
	if (g_up) return;
	g_painted = false;
	g_since = GetTickCount();
	g_up = true;
	LogI("render", "loading curtain: the screen keeps its picture (%s)", why);
}

void LowerForLoad() { Lower("a game is loaded"); }

void *__fastcall HookCreateGame(void *ecx, void *edx)
{
	Lower("a match is created");
	return g_origCreateGame(ecx, edx);
}

void *__cdecl HookLoadReplay(const char *path)
{
	Lower("a replay is loaded");
	return g_origLoadReplay(path);
}

bool __cdecl HookMainMenuLoop(void *settings, int *args)
{
	LoadCurtainLift("back in the menus");
	return g_origMainMenuLoop(settings, args);
}

// A conquest opens its map of territories inside the match, before any battle is painted.
int __fastcall HookConquestMap(void *ecx, void *edx)
{
	LoadCurtainLift("the map of the conquest");
	return g_origConquestMap(ecx, edx);
}

void *__fastcall HookMapViewCtor(void *self, void *edx, void *a, void *b, void *c, int d)
{
	Lower("the view of a match is created");
	return g_origMapViewCtor(self, edx, a, b, c, d);
}

// The frame of the first paint is copied to the screen after the paint: the next paint starts once it has been shown whole.
int __fastcall HookMapViewRender(void *self, void *edx)
{
	if (g_painted && g_up) LoadCurtainLift("the match is on screen");
	const int painted = g_origMapViewRender(self, edx);
	if (painted && g_up) g_painted = true;
	return painted;
}

bool Hook(uintptr_t target, void *detour, void **original)
{
	return HookCreate(reinterpret_cast<void *>(target), detour, original) == MH_OK && HookEnable(reinterpret_cast<void *>(target)) == MH_OK;
}
}

bool LoadCurtainUp()
{
	if (!g_up) return false;
	if (GetTickCount() - g_since < kMaxMs) return true;
	LoadCurtainLift("too long without painting the match");
	return false;
}

void LoadCurtainLift(const char *why)
{
	if (g_up.exchange(false)) LogI("render", "loading curtain: lifted after %lu ms (%s)", GetTickCount() - g_since, why);
}

void InstallLoadCurtain(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = Hook(addr::kCreateGameFromSettings, reinterpret_cast<void *>(&HookCreateGame), reinterpret_cast<void **>(&g_origCreateGame)) &&
		Hook(addr::kLoadReplayFile, reinterpret_cast<void *>(&HookLoadReplay), reinterpret_cast<void **>(&g_origLoadReplay)) &&
		Hook(addr::kRunMainMenuLoop, reinterpret_cast<void *>(&HookMainMenuLoop), reinterpret_cast<void **>(&g_origMainMenuLoop)) &&
		Hook(addr::kMapViewCtor, reinterpret_cast<void *>(&HookMapViewCtor), reinterpret_cast<void **>(&g_origMapViewCtor)) &&
		Hook(addr::kMapViewRender, reinterpret_cast<void *>(&HookMapViewRender), reinterpret_cast<void **>(&g_origMapViewRender)) &&
		Hook(addr::kShowConquestGameDialog, reinterpret_cast<void *>(&HookConquestMap), reinterpret_cast<void **>(&g_origConquestMap)) &&
		InstallGameLoadHook(knownExe);
	if (ok) OnGameLoading(&LowerForLoad);
	LogAt(ok ? kLogInfo : kLogError, "render", "loading curtain: %s", ok ? "ready" : "NOT available");
}
