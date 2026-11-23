// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "game/scriptapi.h"
#include "core/log.h"
#include "game/civs.h"
#include "game/cmdcheck.h"
#include "mods/mods.h"
#include "mods/modoptions.h"
#include "mods/modvars.h"
#include "game/scriptevents.h"
#include "game/specials.h"
#include "game/gamehook.h"

#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <atomic>
#include "addr/script.h"

namespace {
const unsigned kFirstModId = 0x741;

typedef const char *(__fastcall *Init_t)(void *self, void *edx, void **regs, unsigned n, void *log);
typedef const char *(__fastcall *RegisterBySig_t)(void *self, void *edx, void *fn, const char *name, const char *sig);
typedef int(__cdecl *NativeFn)(int *pSP);
typedef int(__cdecl *Release_t)(int block);

Init_t g_origInit = nullptr;
std::atomic<int> g_logged{0};

int PopInt(int *pSP)
{
	*pSP -= 4;
	return *reinterpret_cast<int *>(*pSP);
}

int PopText(int *pSP)
{
	char *interp = *reinterpret_cast<char **>(addr::kCurInterpreter);
	*pSP -= 4;
	const unsigned short distance = *reinterpret_cast<unsigned short *>(*pSP);
	unsigned *last = reinterpret_cast<unsigned *>(interp + 0x1c);
	*last = distance ? *last - distance : 0;
	*pSP -= 4;
	return *reinterpret_cast<int *>(*pSP);
}

const char *TextOf(int block)
{
	return block ? reinterpret_cast<const char *>(block + 4) : "";
}

void ReleaseText(int block)
{
	reinterpret_cast<Release_t>(addr::kScriptStringRelease)(block);
}

void PushInt(int *pSP, int v)
{
	*reinterpret_cast<int *>(*pSP) = v;
	*pSP += 4;
}

void PushBool(int *pSP, bool v)
{
	*reinterpret_cast<char *>(*pSP) = v ? 1 : 0;
	*pSP += 1;
}

void LogScript(const std::string &text)
{
	const int n = ++g_logged;
	if (n <= 2000) LogI("script", "%s", text.c_str());
	else if (n == 2001) LogW("script", "too many messages; the following ones are no longer written");
}

int __cdecl Mod_Log(int *pSP)
{
	const int text = PopText(pSP);
	LogScript(TextOf(text));
	ReleaseText(text);
	return 0;
}

int __cdecl Mod_LogInt(int *pSP)
{
	const int value = PopInt(pSP);
	const int text = PopText(pSP);
	LogScript(std::string(TextOf(text)) + ": " + std::to_string(value));
	ReleaseText(text);
	return 0;
}

int __cdecl Mod_IsActive(int *pSP)
{
	static std::mutex lock;
	static std::set<std::string> active;
	static std::vector<std::string> builtFrom;
	static bool loaded = false;
	const int text = PopText(pSP);
	std::string id = TextOf(text);
	ReleaseText(text);
	for (char &c : id) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	bool found = false;
	{
		std::lock_guard<std::mutex> guard(lock);
		// Rebuilt when the mounted mods change: map-only mods come and go without a restart (ModsApplyWithoutRestart).
		std::vector<std::string> mounted = ModsMountedProfile().mods;
		if (!loaded || mounted != builtFrom) {
			active.clear();
			for (const std::string &folder : mounted) {
				ModInfo m;
				if (!ModsFind(folder, m)) continue;
				std::string mid = m.id;
				for (char &c : mid) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
				active.insert(mid);
			}
			builtFrom = std::move(mounted);
			loaded = true;
		}
		found = active.count(id) != 0;
	}
	PushBool(pSP, found);
	return 0;
}

int __cdecl Mod_GetOption(int *pSP)
{
	const int text = PopText(pSP);
	const std::string id = TextOf(text);
	ReleaseText(text);
	PushBool(pSP, ModOptionValue(id) != 0);
	return 0;
}

int __cdecl Mod_GetOptionValue(int *pSP)
{
	const int text = PopText(pSP);
	const std::string id = TextOf(text);
	ReleaseText(text);
	PushInt(pSP, ModOptionValue(id));
	return 0;
}

int __cdecl Mod_SetInt(int *pSP)
{
	const int value = PopInt(pSP);
	const int text = PopText(pSP);
	if (!ModVarsSet(TextOf(text), value)) LogScript(std::string("Mod_SetInt: empty name or longer than 64 letters, or more than 1024 variables: ") + TextOf(text));
	ReleaseText(text);
	return 0;
}

int __cdecl Mod_GetInt(int *pSP)
{
	const int text = PopText(pSP);
	const int value = ModVarsGet(TextOf(text));
	ReleaseText(text);
	PushInt(pSP, value);
	return 0;
}

int __cdecl Mod_AddInt(int *pSP)
{
	const int delta = PopInt(pSP);
	const int text = PopText(pSP);
	const std::string key = TextOf(text);
	ReleaseText(text);
	const int value = ModVarsGet(key) + delta;
	if (!ModVarsSet(key, value)) LogScript("Mod_AddInt: empty name or longer than 64 letters, or more than 1024 variables:" + key);
	PushInt(pSP, value);
	return 0;
}

int __cdecl Mod_HasKey(int *pSP)
{
	const int text = PopText(pSP);
	const bool has = ModVarsHas(TextOf(text));
	ReleaseText(text);
	PushBool(pSP, has);
	return 0;
}

int __cdecl Mod_AttackDamage(int *pSP)
{
	PushInt(pSP, ModAttackDamage());
	return 0;
}

int __cdecl Mod_SetAttackDamage(int *pSP)
{
	ModSetAttackDamage(PopInt(pSP));
	return 0;
}

void PopObj(int *pSP, unsigned *handle, int *offset)
{
	*pSP -= 8;
	*handle = *reinterpret_cast<unsigned *>(*pSP);
	*offset = *reinterpret_cast<int *>(*pSP + 4);
}

int __cdecl Mod_HasSpecial(int *pSP)
{
	const int bit = PopInt(pSP);
	unsigned handle;
	int offset;
	PopObj(pSP, &handle, &offset);
	PushBool(pSP, SpecialsHas(SpecialsUnitFromObj(handle, offset), bit));
	return 0;
}

int __cdecl Mod_SetSpecial(int *pSP)
{
	*pSP -= 1;
	const bool on = *reinterpret_cast<char *>(*pSP) != 0;
	const int bit = PopInt(pSP);
	unsigned handle;
	int offset;
	PopObj(pSP, &handle, &offset);
	SpecialsSet(SpecialsUnitFromObj(handle, offset), bit, on);
	return 0;
}

int __cdecl Mod_TrueRace(int *pSP)
{
	unsigned handle;
	int offset;
	PopObj(pSP, &handle, &offset);
	int race = -1;
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle & 0xffff];
	if (base) {
		char *obj = base + offset;
		char *cls = *reinterpret_cast<char **>(obj + 0x3c);
		if (cls) race = *reinterpret_cast<int *>(cls + 0xb48);
	}
	PushInt(pSP, race);
	return 0;
}

int __cdecl Mod_CanExecCmd(int *pSP)
{
	const int text = PopText(pSP);
	unsigned handle;
	int offset;
	PopObj(pSP, &handle, &offset);
	const bool ok = CanExecCommand(handle, TextOf(text));
	ReleaseText(text);
	PushBool(pSP, ok);
	return 0;
}

int __cdecl Mod_OffersCmd(int *pSP)
{
	const int text = PopText(pSP);
	unsigned handle;
	int offset;
	PopObj(pSP, &handle, &offset);
	const bool ok = OffersCommand(handle, TextOf(text));
	ReleaseText(text);
	PushBool(pSP, ok);
	return 0;
}

int __cdecl Mod_PlayerCiv(int *pSP)
{
	PushInt(pSP, CivsPlayerCiv(PopInt(pSP)));
	return 0;
}

int __cdecl Mod_ApiVersion(int *pSP)
{
	PushInt(pSP, kScriptApiVersion);
	return 0;
}

struct Native {
	NativeFn fn;
	const char *name;
	const char *sig;
};
const Native kNatives[] = {
	{Mod_Log, "Mod_Log", "void, str text"},
	{Mod_LogInt, "Mod_LogInt", "void, str text, int value"},
	{Mod_IsActive, "Mod_IsActive", "bool, str id"},
	{Mod_ApiVersion, "Mod_ApiVersion", "int"},
	{Mod_SetInt, "Mod_SetInt", "void, str name, int value"},
	{Mod_GetInt, "Mod_GetInt", "int, str name"},
	{Mod_AddInt, "Mod_AddInt", "int, str name, int delta"},
	{Mod_HasKey, "Mod_HasKey", "bool, str name"},
	{Mod_AttackDamage, "Mod_AttackDamage", "int"},
	{Mod_SetAttackDamage, "Mod_SetAttackDamage", "void, int damage"},
	{Mod_HasSpecial, "Mod_HasSpecial", "bool, Unit unit, int bit"},
	{Mod_SetSpecial, "Mod_SetSpecial", "void, Unit unit, int bit, bool on"},
	{Mod_TrueRace, "Mod_TrueRace", "int, Obj obj"},
	{Mod_GetOption, "Mod_GetOption", "bool, str id"},
	{Mod_GetOptionValue, "Mod_GetOptionValue", "int, str id"},
	{Mod_PlayerCiv, "Mod_PlayerCiv", "int, int player"},
	{Mod_CanExecCmd, "Mod_CanExecCmd", "bool, Obj obj, str command"},
	{Mod_OffersCmd, "Mod_OffersCmd", "bool, Obj obj, str command"},
};

int __cdecl RegisterModNatives(void *engine)
{
	const unsigned first = *reinterpret_cast<unsigned short *>(static_cast<char *>(engine) + 0x4004);
	if (first != kFirstModId) {
		LogI("script", "api: the first own function has id 0x%x (0x%x expected); the game registered another number of functions",
			first, kFirstModId);
	}
	int failed = 0;
	for (const Native &n : kNatives) {
		const char *err = reinterpret_cast<RegisterBySig_t>(addr::kRegisterFunctionBySig)(engine, nullptr, reinterpret_cast<void *>(n.fn),
			n.name, n.sig);
		if (err) {
			LogE("script", "api: could not register %s (%s)", n.name, err);
			failed++;
		}
	}
	CivsRegisterConstants(engine);
	SpecialsRegisterConstants(engine);
	LogI("script", "API %d: %d functions registered, ids 0x%x-0x%x%s", kScriptApiVersion, static_cast<int>(sizeof(kNatives) / sizeof(kNatives[0])) - failed,
		first, *reinterpret_cast<unsigned short *>(static_cast<char *>(engine) + 0x4004) - 1, failed ? " (with errors)" : "");
	return 0;
}

const char *__fastcall HookInit(void *self, void *edx, void **regs, unsigned n, void *log)
{
	std::vector<void *> all(regs, regs + n);
	all.push_back(reinterpret_cast<void *>(&RegisterModNatives));
	return g_origInit(self, edx, all.data(), n + 1, log);
}

}

void InstallScriptApi(bool knownExe)
{
	if (!knownExe) return;
	if (!HookFunction(addr::kScriptEngineInit, &HookInit, &g_origInit)) {
		LogE("script", "api: could not hook CScriptEngine::Init");
		return;
	}
	LogI("script", "api: hooked");
}
