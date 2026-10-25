// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <mutex>
#include <vector>

#include "hooks/gameload.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "addr/world.h"

namespace {
typedef int(__cdecl *LoadGameFromFile_t)(const char *path);
LoadGameFromFile_t g_origLoadGameFromFile = nullptr;
std::mutex g_lock;
std::vector<GameLoadingFn> g_before;
std::vector<GameLoadedFn> g_listeners;
bool g_installed = false, g_ok = false;

int __cdecl HookLoadGameFromFile(const char *path)
{
	std::vector<GameLoadingFn> before;
	std::vector<GameLoadedFn> listeners;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		before = g_before;
		listeners = g_listeners;
	}
	for (GameLoadingFn fn : before) fn();
	const int result = g_origLoadGameFromFile(path);
	for (GameLoadedFn fn : listeners) fn(result);
	return result;
}
}

bool InstallGameLoadHook(bool knownExe)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_installed) return g_ok;
	g_installed = true;
	if (!knownExe) return false;
	void *target = reinterpret_cast<void *>(addr::kLoadGameFromFile);
	g_ok = HookCreate(target, reinterpret_cast<void *>(&HookLoadGameFromFile), reinterpret_cast<void **>(&g_origLoadGameFromFile)) == MH_OK && HookEnable(target) == MH_OK;
	return g_ok;
}

void OnGameLoading(GameLoadingFn fn)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_before.push_back(fn);
}

void OnGameLoaded(GameLoadedFn fn)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_listeners.push_back(fn);
}
