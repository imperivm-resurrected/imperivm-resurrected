// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "game/civs.h"
#include "game/gamelayout.h"
#include "game/gamehook.h"
#include "core/log.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "addr/state.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
const int kClassRaceOffset = 0xb48;

typedef int(__cdecl *Native_t)(int *pSP);
typedef int(__fastcall *IsDerived_t)(void *self, void *edx, void *other);
typedef int(__fastcall *IsKindOfName_t)(void *self, void *edx, const char *name);
typedef int(__fastcall *ClassCount_t)(void *army, void *edx, const char *name);
typedef void *(__fastcall *FindClass_t)(void *registry, void *edx, const char *name);

Native_t g_origObjRace = nullptr;
IsDerived_t g_origIsDerived = nullptr;
IsKindOfName_t g_origIsKindOf = nullptr;
ClassCount_t g_origClassCount = nullptr;

std::mutex g_lock;
std::map<std::string, std::string, std::less<>> g_cloneToBase;
std::map<std::string, std::vector<std::string>, std::less<>> g_baseToClones;
volatile bool g_haveClasses = false;
volatile bool g_active = false;

// CVXScriptClass: its id, a std::string at +4.
std::string ClassId(const char *cls)
{
	return layout::StringValue(cls + 4, 256);
}

void *FindClass(const char *name)
{
	void *registry = *reinterpret_cast<void **>(addr::kRegistry);
	return registry ? reinterpret_cast<FindClass_t>(addr::kFindClassByName)(registry, nullptr, name) : nullptr;
}

void *BaseClassOf(void *cls)
{
	if (!g_haveClasses || !cls) return nullptr;
	if (*reinterpret_cast<int *>(static_cast<char *>(cls) + kClassRaceOffset) < 10) return nullptr;
	const std::string id = ClassId(static_cast<char *>(cls));
	if (id.empty()) return nullptr;
	std::string base;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		auto it = g_cloneToBase.find(id);
		if (it == g_cloneToBase.end()) return nullptr;
		base = it->second;
	}
	void *b = FindClass(base.c_str());
	return b != cls ? b : nullptr;
}

char *ObjFromHandle(unsigned handle, int offset)
{
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle & 0xffff];
	return base ? base + offset : nullptr;
}

bool IsAiPlayer(const char *player)
{
	return player && Field<int>(player, layout::kPlayerAi) != 0;
}

const Civ *AliasCivOfRace(int race)
{
	if (race < 10) return nullptr;
	const Civ *c = CivByIndex(race);
	return c && c->AiAlias() ? c : nullptr;
}

const Civ *AliasCivOfObject(const char *obj)
{
	if (!obj) return nullptr;
	const char *cls = *reinterpret_cast<const char *const *>(obj + 0x3c);
	if (!cls) return nullptr;
	const Civ *c = AliasCivOfRace(*reinterpret_cast<const int *>(cls + kClassRaceOffset));
	return c && IsAiPlayer(*reinterpret_cast<const char *const *>(obj + 0x70)) ? c : nullptr;
}

const Civ *AliasCivOfSettlement(const char *set)
{
	return set ? AliasCivOfObject(ObjFromHandle(*reinterpret_cast<const unsigned short *>(set + 0x8c), 0)) : nullptr;
}

const Civ *AliasCivOfPlayerId(int id)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || id < 1 || id > layout::kPlayerCount) return nullptr;
	const char *player = layout::PlayerAt(game, id - 1);
	return IsAiPlayer(player) ? AliasCivOfRace(Field<int>(player, layout::kPlayerRace)) : nullptr;
}

int __cdecl HookObjRace(int *pSP)
{
	const int top = *pSP;
	const unsigned handle = *reinterpret_cast<unsigned *>(top - 8);
	const int offset = *reinterpret_cast<int *>(top - 4);
	const int r = g_origObjRace(pSP);
	int *result = reinterpret_cast<int *>(*pSP - 4);
	if (g_active && *result >= 10) {
		if (const Civ *c = AliasCivOfObject(ObjFromHandle(handle, offset))) {
			static int logged = 0;
			if (logged++ < 3) LogD("civilizations", "AI: Obj::race of civilization %d -> the base (%d)", *result, c->base);
			*result = c->base;
		}
	}
	return r;
}

enum Kind { kObject, kSettlement, kPlayerId };

struct Site {
	uintptr_t addr;
	int strOff;
	int objOff;
	Kind kind;
	const char *what;
	Native_t orig;
};

Site g_sites[] = {
	{addr::kScriptFnObjExecCmd, 0x19, 0x21, kObject, "Obj::ExecCmd", nullptr},
	{addr::kScriptFnObjCmdCount, 8, 16, kObject, "Obj::CmdCount", nullptr},
	{addr::kScriptFnBuildingResearch, 8, 16, kObject, "Building::Research", nullptr},
	{addr::kScriptFnIsResearched, 8, 16, kSettlement, "IsResearched", nullptr},
	{addr::kScriptFnIsResearchedPlayer, 8, 12, kPlayerId, "IsResearched(player)", nullptr},
	{addr::kScriptFnIsResearching, 8, 16, kSettlement, "IsResearching", nullptr},
	{addr::kScriptFnIsResearchingPlayer, 8, 12, kPlayerId, "IsResearching(player)", nullptr},
	{addr::kScriptFnSettlementCanAfford, 8, 16, kSettlement, "Settlement::CanAfford", nullptr},
	{addr::kScriptFnSettlementCanAffordN, 12, 20, kSettlement, "Settlement::CanAfford(n)", nullptr},
	{addr::kScriptFnSettlementMaxAffordCount, 8, 16, kSettlement, "Settlement::MaxAffordCount", nullptr},
	{addr::kScriptFnSettlementCanResearch, 8, 16, kSettlement, "Settlement::CanResearch", nullptr},
	{addr::kScriptFnSettlementResearch, 8, 16, kSettlement, "Settlement::Research", nullptr},
	{addr::kScriptFnSettlementReserveFor, 12, 20, kSettlement, "Settlement::ReserveFor", nullptr},
	{addr::kScriptFnSettlementCheckTechBudget, 12, 20, kSettlement, "Settlement::CheckTechBudget", nullptr},
	{addr::kScriptFnSettlementFindResearchLab, 8, 16, kSettlement, "Settlement::FindResearchLab", nullptr},
	{addr::kScriptFnSettlementTSResearch, 8, 16, kSettlement, "Settlement::TSResearch", nullptr},
};
const int kSiteCount = sizeof(g_sites) / sizeof(g_sites[0]);

void TranslateArg(int *pSP, const Site &s)
{
	if (!g_active) return;
	const int top = *pSP;
	const Civ *civ = nullptr;
	if (s.kind == kPlayerId) {
		civ = AliasCivOfPlayerId(*reinterpret_cast<int *>(top - s.objOff));
	}
	else {
		char *obj = ObjFromHandle(*reinterpret_cast<unsigned *>(top - s.objOff), *reinterpret_cast<int *>(top - s.objOff + 4));
		civ = s.kind == kObject ? AliasCivOfObject(obj) : AliasCivOfSettlement(obj);
	}
	if (!civ || civ->aliasOrders.empty()) return;
	int *slot = reinterpret_cast<int *>(top - s.strOff);
	const int block = *slot;
	if (!block) return;
	auto it = civ->aliasOrders.find(reinterpret_cast<const char *>(block + 4));
	if (it == civ->aliasOrders.end()) return;
	const int fresh = reinterpret_cast<int(__cdecl *)(const char *)>(addr::kAllocScriptString)(it->second.c_str());
	if (!fresh) return;
	static int logged = 0;
	if (logged++ < 12) LogD("civilizations", "AI: %s of %s: \"%s\" -> \"%s\"", s.what, civ->id.c_str(), it->first.c_str(), it->second.c_str());
	*slot = fresh;
	reinterpret_cast<int(__cdecl *)(int)>(addr::kScriptStringRelease)(block);
}

template <int N>
int __cdecl WrapTranslate(int *pSP)
{
	TranslateArg(pSP, g_sites[N]);
	return g_sites[N].orig(pSP);
}

const Native_t kWrappers[] = {&WrapTranslate<0>, &WrapTranslate<1>, &WrapTranslate<2>, &WrapTranslate<3>, &WrapTranslate<4>, &WrapTranslate<5>,
	&WrapTranslate<6>, &WrapTranslate<7>, &WrapTranslate<8>, &WrapTranslate<9>, &WrapTranslate<10>, &WrapTranslate<11>, &WrapTranslate<12>,
	&WrapTranslate<13>, &WrapTranslate<14>, &WrapTranslate<15>};
static_assert(sizeof(kWrappers) / sizeof(kWrappers[0]) == sizeof(g_sites) / sizeof(g_sites[0]), "one hook per site");

int __fastcall HookIsDerivedFrom(void *self, void *edx, void *other)
{
	const int r = g_origIsDerived(self, edx, other);
	if (r || !g_haveClasses || !self || !other) return r;
	void *base = BaseClassOf(self);
	return base ? g_origIsDerived(base, edx, other) : 0;
}

int __fastcall HookIsKindOfName(void *self, void *edx, const char *name)
{
	const int r = g_origIsKindOf(self, edx, name);
	if (r || !g_haveClasses || !self || !name) return r;
	void *base = BaseClassOf(self);
	return base ? g_origIsKindOf(base, edx, name) : 0;
}

int __fastcall HookClassCount(void *army, void *edx, const char *name)
{
	int r = g_origClassCount(army, edx, name);
	if (!g_haveClasses || !name) return r;
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_baseToClones.find(name);
	if (it != g_baseToClones.end()) {
		for (const std::string &clone : it->second) r += g_origClassCount(army, edx, clone.c_str());
	}
	return r;
}

}

void CivAiRebuild()
{
	std::map<std::string, std::string, std::less<>> toBase;
	std::map<std::string, std::vector<std::string>, std::less<>> toClones;
	bool active = false;
	for (const Civ &c : CivsList()) {
		if (!c.AiAlias()) continue;
		active = true;
		for (const auto &kv : c.aliasClasses) {
			toBase[kv.second] = kv.first;
			toClones[kv.first].push_back(kv.second);
		}
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_cloneToBase.swap(toBase);
		g_baseToClones.swap(toClones);
		g_haveClasses = !g_cloneToBase.empty();
		g_active = active;
	}
	if (active) LogD("civilizations", "AI with aliases to the base: %d classes", static_cast<int>(g_cloneToBase.size()));
}

void InstallCivAi(bool knownExe)
{
	if (!knownExe) return;
	struct Hook {
		uintptr_t addr;
		void *detour;
		void **orig;
		const char *what;
	};
	std::vector<Hook> hooks = {
		{addr::kObjRace, reinterpret_cast<void *>(&HookObjRace), reinterpret_cast<void **>(&g_origObjRace), "Obj::race"},
		{addr::kIsDerivedFrom, reinterpret_cast<void *>(&HookIsDerivedFrom), reinterpret_cast<void **>(&g_origIsDerived), "IsDerivedFrom"},
		{addr::kIsKindOfName, reinterpret_cast<void *>(&HookIsKindOfName), reinterpret_cast<void **>(&g_origIsKindOf), "IsKindOfName"},
		{addr::kGetClassCount, reinterpret_cast<void *>(&HookClassCount), reinterpret_cast<void **>(&g_origClassCount), "CVXArmy::GetClassCount"},
	};
	for (int i = 0; i < kSiteCount; i++) {
		hooks.push_back({g_sites[i].addr, reinterpret_cast<void *>(kWrappers[i]), reinterpret_cast<void **>(&g_sites[i].orig), g_sites[i].what});
	}
	for (const Hook &h : hooks) {
		if (!HookFunction(h.addr, h.detour, h.orig)) {
			LogE("civilizations", "AI: could not hook %s", h.what);
		}
	}
	LogI("civilizations", "AI: hooked");
}
