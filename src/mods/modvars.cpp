// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <mutex>
#include <map>

#include "hooks/gameload.h"
#include "core/log.h"
#include "mods/modvars.h"

namespace {
const size_t kMaxKeyLength = 64;
const size_t kMaxVars = 1024;

std::mutex g_lock;
std::map<std::string, int> g_vars;
bool g_pendingValid = false;
Json g_pending;
bool g_savedRulesValid = false;
unsigned g_savedRules = 0;
unsigned long g_savedRulesTick = 0;

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

void AfterGameLoad(int)
{
	Json pending;
	bool valid;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		valid = g_pendingValid;
		pending = g_pending;
		g_pendingValid = false;
		g_pending = Json();
	}
	if (valid) ModVarsFromJson(pending);
	else ModVarsClear();
	const int n = ModVarsCount();
	if (n) LogI("content", "mod variables: %d recovered from the loaded game", n);
}

}

void ModVarsClear()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_vars.clear();
}

bool ModVarsHas(const std::string &key)
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_vars.count(key) != 0;
}

int ModVarsGet(const std::string &key)
{
	std::lock_guard<std::mutex> lock(g_lock);
	const auto it = g_vars.find(key);
	return it == g_vars.end() ? 0 : it->second;
}

bool ModVarsSet(const std::string &key, int value)
{
	if (key.empty() || key.size() > kMaxKeyLength) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_vars.size() >= kMaxVars && !g_vars.count(key)) return false;
	g_vars[key] = value;
	return true;
}

int ModVarsCount()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return static_cast<int>(g_vars.size());
}

Json ModVarsToJson()
{
	Json j = Json::MakeObject();
	std::lock_guard<std::mutex> lock(g_lock);
	for (const auto &kv : g_vars) j.Set(kv.first, Json::MakeNumber(kv.second));
	return j;
}

void ModVarsFromJson(const Json &obj)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_vars.clear();
	if (obj.type != Json::Object) return;
	for (const auto &kv : obj.obj) {
		if (kv.second.type != Json::Number || kv.first.empty() || kv.first.size() > kMaxKeyLength || g_vars.size() >= kMaxVars) continue;
		g_vars[kv.first] = static_cast<int>(kv.second.num);
	}
}

bool ModRulesFromSaveTake(unsigned &rules, unsigned long &tick)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_savedRulesValid) return false;
	g_savedRulesValid = false;
	rules = g_savedRules;
	tick = g_savedRulesTick;
	return true;
}

bool ModRulesFromSavePeek(unsigned &rules, unsigned long &tick)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_savedRulesValid) return false;
	rules = g_savedRules;
	tick = g_savedRulesTick;
	return true;
}

void ModVarsPendingFromSave(const std::string &gamePath)
{
	Json j;
	const Json *vars = nullptr;
	const bool read = ReadJsonFile(GameDir() + "\\" + gamePath + ".mods.json", j);
	if (read) vars = j.Get("variables");
	std::lock_guard<std::mutex> lock(g_lock);
	g_savedRulesValid = read;
	g_savedRules = read ? static_cast<unsigned>(j.GetNumber("rules", 0)) : 0;
	g_savedRulesTick = GetTickCount() | 1;
	g_pendingValid = vars && vars->type == Json::Object;
	g_pending = g_pendingValid ? *vars : Json();
}

void InstallModVars(bool knownExe)
{
	if (!knownExe) return;
	if (!InstallGameLoadHook(knownExe)) {
		LogE("content", "mod variables: could not hook LoadGameFromFile (they will not be recovered when loading a game)");
		return;
	}
	OnGameLoaded(&AfterGameLoad);
	LogI("content", "mod variables: hooked");
}
