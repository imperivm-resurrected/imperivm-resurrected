// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "game/packrules.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/json.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/features.h"
#include "mods/mods.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
typedef int(__fastcall *IsKindOfName_t)(void *self, void *edx, const char *name);
typedef int(__fastcall *ObjMethod_t)(char *self, void *edx, intptr_t arg);

const unsigned kObjFlags = 0x2c;
const unsigned kObjClass = 0x3c;
const uint32_t kIsUnit = 0x400000;
const uint8_t kDetachCall[] = {0xe8, 0x72, 0x33, 0x1c, 0x00};
const uint8_t kSentryFlagsGame = 0x04;
const uint8_t kSentryFlagsUnselectable = 0x84;

struct Rules {
	bool armyStaysWithHero = false;
	bool sentriesUnselectable = false;
	std::vector<std::string> protectedClasses;
};

std::mutex g_lock;
Rules g_rules;
std::vector<std::string> g_protected;
volatile bool g_protecting = false;
std::map<std::string, std::vector<std::string>> g_errors;
bool g_hooked = false;
ObjMethod_t g_origDamage = nullptr, g_origIsValidTarget = nullptr;

void AddError(const std::string &folder, const std::string &message)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		std::vector<std::string> &list = g_errors[folder];
		if (std::find(list.begin(), list.end(), message) == list.end()) list.push_back(message);
	}
	LogW("game", "rules (%s): %s", folder.empty() ? "imperivm-resurrected\\rules" : folder.c_str(), message.c_str());
}

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

bool ValidClassName(const std::string &s)
{
	if (s.empty() || s.size() > 64) return false;
	for (char c : s) {
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
	}
	return true;
}

void LoadRules(const std::string &folder, const std::string &path, Rules &out)
{
	Json j;
	std::string err;
	if (!ReadJsonFile(path, j, &err) || j.type != Json::Object) {
		AddError(folder, TrFmt("rules\\game.json: not valid JSON (%s)", {err.empty() ? Tr("not an object") : err}));
		return;
	}
	static const char *const kKnown[] = {"armyStaysWithHero", "sentriesUnselectable", "protectedClasses"};
	for (const auto &kv : j.obj) {
		if (std::none_of(std::begin(kKnown), std::end(kKnown), [&](const char *k) { return kv.first == k; })) {
			AddError(folder, TrFmt("rules\\game.json: \"%s\" is not a rule of the game", {kv.first}));
		}
	}
	if (j.GetBool("armyStaysWithHero")) out.armyStaysWithHero = true;
	if (j.GetBool("sentriesUnselectable")) out.sentriesUnselectable = true;
	if (const Json *list = j.Get("protectedClasses")) {
		if (list->type != Json::Array) {
			AddError(folder, Tr("rules\\game.json: \"protectedClasses\" is a list of class names"));
			return;
		}
		for (const Json &c : list->arr) {
			if (c.type != Json::String || !ValidClassName(c.str)) {
				AddError(folder, TrFmt("rules\\game.json: \"%s\" in \"protectedClasses\" is not a class name", {c.type == Json::String ? c.str : c.Dump()}));
				continue;
			}
			if (std::find(out.protectedClasses.begin(), out.protectedClasses.end(), c.str) == out.protectedClasses.end()) out.protectedClasses.push_back(c.str);
		}
	}
}

bool Protected(char *obj)
{
	if (!obj || !(*reinterpret_cast<uint32_t *>(obj + kObjFlags) & kIsUnit)) return false;
	void *cls = *reinterpret_cast<void **>(obj + kObjClass);
	if (!cls) return false;
	for (const std::string &name : g_protected) {
		if (reinterpret_cast<IsKindOfName_t>(addr::kIsKindOfName)(cls, nullptr, name.c_str())) return true;
	}
	return false;
}

int __fastcall HookDamage(char *self, void *edx, intptr_t damage)
{
	if (g_protecting && Protected(self)) return 0;
	return g_origDamage(self, edx, damage);
}

int __fastcall HookIsValidTarget(char *self, void *edx, intptr_t target)
{
	if (g_protecting && Protected(reinterpret_cast<char *>(target))) return 0;
	return g_origIsValidTarget(self, edx, target);
}

bool WriteCode(uintptr_t site, const uint8_t *expectedA, const uint8_t *expectedB, const uint8_t *want, size_t n)
{
	uint8_t *at = reinterpret_cast<uint8_t *>(site);
	if (memcmp(at, want, n) == 0) return true;
	if (memcmp(at, expectedA, n) != 0 && memcmp(at, expectedB, n) != 0) return false;
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, want, n);
	VirtualProtect(at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	return true;
}

void Apply()
{
	if (!g_hooked) return;
	Rules r;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		r = g_rules;
	}
	const uint8_t nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
	if (!WriteCode(addr::kTransportChangeDetachHero, kDetachCall, nops, r.armyStaysWithHero ? nops : kDetachCall, sizeof(nops))) {
		LogW("game", "rules: the code that detaches units from their hero at walls is not the expected one; left as it is");
	}
	const uint8_t sentryGame = kSentryFlagsGame, sentryUnselectable = kSentryFlagsUnselectable;
	if (!WriteCode(addr::kSentryFlagsByte, &sentryGame, &sentryUnselectable, r.sentriesUnselectable ? &sentryUnselectable : &sentryGame, 1)) {
		LogW("game", "rules: the code that creates sentries is not the expected one; left as it is");
	}
	g_protecting = false;
	g_protected = r.protectedClasses;
	g_protecting = !g_protected.empty();
}

void PackRulesRebuild()
{
	Rules merged;
	std::vector<std::pair<std::string, std::string>> roots;
	for (const std::string &dir : ModsMountedDirs()) roots.push_back({ModsPackFolder(dir), dir + "\\rules\\game.json"});
	roots.push_back({"", GameDir() + "\\imperivm-resurrected\\rules\\game.json"});
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_errors.clear();
	}
	for (const auto &root : roots) {
		if (GetFileAttributesA(root.second.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		Rules one;
		LoadRules(root.first, root.second, one);
		std::string what;
		if (one.armyStaysWithHero) {
			merged.armyStaysWithHero = true;
			what += " armyStaysWithHero";
		}
		if (one.sentriesUnselectable) {
			merged.sentriesUnselectable = true;
			what += " sentriesUnselectable";
		}
		for (const std::string &c : one.protectedClasses) {
			if (std::find(merged.protectedClasses.begin(), merged.protectedClasses.end(), c) == merged.protectedClasses.end()) merged.protectedClasses.push_back(c);
		}
		if (!one.protectedClasses.empty()) what += " protectedClasses(" + std::to_string(one.protectedClasses.size()) + ")";
		LogI("game", "rules of %s:%s", root.first.empty() ? "imperivm-resurrected\\rules" : root.first.c_str(), what.empty() ? " none" : what.c_str());
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_rules = merged;
	}
	Apply();
}

std::vector<std::string> PackRulesProblemsFor(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_errors.find(folder);
	return it == g_errors.end() ? std::vector<std::string>() : it->second;
}

const bool g_featureRegistered = (ModsRegisterFeature({40, &PackRulesRebuild, &PackRulesProblemsFor}), true);
}

void InstallPackRules(bool knownExe)
{
	if (!knownExe) return;
	const struct {
		uintptr_t at;
		void *detour;
		void **orig;
		const char *what;
	} hooks[] = {
		{addr::kScriptObjDamage, reinterpret_cast<void *>(&HookDamage), reinterpret_cast<void **>(&g_origDamage), "CVXScriptObj::Damage"},
		{addr::kScriptObjIsValidTarget, reinterpret_cast<void *>(&HookIsValidTarget), reinterpret_cast<void **>(&g_origIsValidTarget), "CVXScriptObj::IsValidTarget"},
	};
	for (const auto &h : hooks) {
		if (HookCreate(reinterpret_cast<void *>(h.at), h.detour, h.orig) != MH_OK || HookEnable(reinterpret_cast<void *>(h.at)) != MH_OK) {
			LogE("game", "rules: could not hook %s; the protected classes of content packs are not protected", h.what);
		}
	}
	g_hooked = true;
	Apply();
}
