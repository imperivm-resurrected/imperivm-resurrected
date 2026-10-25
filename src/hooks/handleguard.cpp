// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "hooks/handleguard.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "addr/script.h"
#include "addr/world.h"
#include "addr/state.h"

namespace {
typedef char *(__fastcall *IsKindOf_t)(char *object, void *unused, const char *className);
typedef int(__cdecl *Native_t)(int *sp);
typedef char *(__fastcall *GetFirstUnit_t)(char *squad);
typedef char *(__cdecl *GetSquad_t)(char *unit);

const unsigned kObjFlags = 0x2c;
const uint32_t kIsHero = 0x1000000;

struct Range {
	uintptr_t begin = 0, end = 0;
	bool Has(const void *p) const { return reinterpret_cast<uintptr_t>(p) >= begin && reinterpret_cast<uintptr_t>(p) < end; }
};
Range g_text, g_rdata;

Native_t g_origEvalAttach = nullptr, g_origAdvHeroSkills = nullptr, g_origHasFreedom = nullptr;
void *g_squadizeCont = nullptr, *g_squadizeNext = nullptr;
void *g_commandDefCont = nullptr, *g_commandDefNone = nullptr;
void *g_mageMemberCont = nullptr, *g_mageMemberNext = nullptr;
void *g_memberCont = nullptr, *g_memberNext = nullptr;

enum Site { kSiteEvalAttach, kSiteHeroSkills, kSiteAddSquadUnits, kSiteFirstUnit, kSiteSquadize, kSiteThreadLook, kSiteCommandDef,
	kSiteHasFreedom, kSiteCollect, kSiteSetPosition, kSiteCount };
const char *const kSiteNames[kSiteCount] = {"Squad.EvalAttach with a leader that is not a unit or has no squad",
	"Hero.TSAdvHeroSkills on an object that is not a hero", "an army member that is not a unit while adding squads to a target",
	"a squad whose first member is not a unit", "a unit that cannot join a squad while grouping units",
	"a squad member that is not a unit while a squad looks around", "the command parameters of an object whose command queue is empty",
	"Unit.HasFreedom on an object that is not a unit", "a squad member that is not a unit in GetPlayerUnits",
	"moving a unit out of a building that is no longer there"};
volatile LONG g_rejected[kSiteCount] = {};

void Rejected(Site site)
{
	const LONG n = InterlockedIncrement(&g_rejected[site]);
	if (n <= 3 || n == 100 || n == 10000) LogW("game", "objects: skipped %s (%ld so far); the original game would read an object that is no longer there", kSiteNames[site], n);
}

void ReadSections()
{
	const char *image = reinterpret_cast<const char *>(GetModuleHandleA(nullptr));
	const IMAGE_NT_HEADERS *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(image + reinterpret_cast<const IMAGE_DOS_HEADER *>(image)->e_lfanew);
	const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
		Range r;
		r.begin = reinterpret_cast<uintptr_t>(image) + s->VirtualAddress;
		r.end = r.begin + s->Misc.VirtualSize;
		if (strncmp(reinterpret_cast<const char *>(s->Name), ".text", 8) == 0) g_text = r;
		else if (strncmp(reinterpret_cast<const char *>(s->Name), ".rdata", 8) == 0) g_rdata = r;
	}
}

char *KindOf(char *obj, uintptr_t classNameSlot)
{
	if (!obj || (reinterpret_cast<uintptr_t>(obj) & 3)) return nullptr;
	__try {
		char *bases = *reinterpret_cast<char **>(obj + 4);
		if (!g_rdata.Has(bases)) return nullptr;
		const int offset = *reinterpret_cast<int *>(bases + 4);
		if (offset < 0 || offset > 0x10000) return nullptr;
		char *base = obj + offset + 4;
		void **vtable = *reinterpret_cast<void ***>(base);
		if (!g_rdata.Has(vtable) || !g_text.Has(vtable[1])) return nullptr;
		return reinterpret_cast<IsKindOf_t>(vtable[1])(base, nullptr, *reinterpret_cast<const char **>(classNameSlot));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

bool IsUnit(char *obj)
{
	return KindOf(obj, addr::kUnitClassName) != nullptr;
}

char *ScriptObj(int top, int at)
{
	const unsigned handle = *reinterpret_cast<unsigned *>(top + at) & 0xffff;
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle];
	if (!base) return nullptr;
	return base + *reinterpret_cast<int *>(top + at + 4);
}

int __cdecl HookEvalAttach(int *sp)
{
	const int top = *sp;
	char *leader = ScriptObj(top, -0xc);
	if (!leader || !IsUnit(leader) || !reinterpret_cast<GetSquad_t>(addr::kGetSquad)(leader)) {
		Rejected(kSiteEvalAttach);
		*reinterpret_cast<int *>(top - 0x10) = 0;
		*sp = top - 0xc;
		return 0;
	}
	return g_origEvalAttach(sp);
}

int __cdecl HookAdvHeroSkills(int *sp)
{
	const int top = *sp;
	char *hero = ScriptObj(top, -0x18);
	if (hero && (!IsUnit(hero) || !(*reinterpret_cast<uint32_t *>(hero + kObjFlags) & kIsHero))) {
		Rejected(kSiteHeroSkills);
		*sp = top - 0x18;
		return 0;
	}
	return g_origAdvHeroSkills(sp);
}

int __cdecl HookHasFreedom(int *sp)
{
	const int top = *sp;
	char *unit = ScriptObj(top, -8);
	if (unit && !IsUnit(unit)) {
		Rejected(kSiteHasFreedom);
		*reinterpret_cast<char *>(top - 8) = 0;
		*sp = top - 7;
		return 0;
	}
	return g_origHasFreedom(sp);
}

typedef void(__fastcall *SetPosition_t)(char *unit, void *edx, void *pos, int a, int b);
SetPosition_t g_origSetPosition = nullptr;
const unsigned kUnitHandle = 0x08;
const unsigned kUnitHolder = 0x154;
const uint16_t kNoHolder = 0xffff;

char *Handle(uint16_t handle)
{
	return reinterpret_cast<char **>(addr::kHandleTable)[handle];
}

void ForgetMissingHolder(char *unit, void *from)
{
	uint16_t &field = *reinterpret_cast<uint16_t *>(unit + kUnitHolder);
	if (field == kNoHolder || Handle(field)) return;
	Rejected(kSiteSetPosition);
	LogW("game", "objects: unit %u (class vtable %08x) pointed to building %u, which does not exist; it is now outside (from %08x)",
		static_cast<unsigned>(*reinterpret_cast<uint16_t *>(unit + kUnitHandle)), *reinterpret_cast<uint32_t *>(unit), static_cast<unsigned>(field),
		static_cast<unsigned>(reinterpret_cast<uintptr_t>(from)));
	field = kNoHolder;
}

void __fastcall HookSetPosition(char *unit, void *edx, void *pos, int a, int b)
{
	if (unit) ForgetMissingHolder(unit, _ReturnAddress());
	g_origSetPosition(unit, edx, pos, a, b);
}

int __fastcall DeadUnlessUnit(char *obj, Site site)
{
	if (!IsUnit(obj)) {
		Rejected(site);
		return 1;
	}
	void **vtable = *reinterpret_cast<void ***>(obj);
	return reinterpret_cast<int(__thiscall *)(char *)>(vtable[0x50 / 4])(obj);
}

int __fastcall AddSquadUnitsIsDead(char *obj)
{
	return DeadUnlessUnit(obj, kSiteAddSquadUnits);
}

int __fastcall ThreadLookIsDead(char *obj)
{
	return DeadUnlessUnit(obj, kSiteThreadLook);
}

char *__fastcall FirstUnitChecked(char *squad)
{
	char *unit = reinterpret_cast<GetFirstUnit_t>(addr::kGetFirstUnit)(squad);
	if (unit && !IsUnit(unit)) {
		Rejected(kSiteFirstUnit);
		return nullptr;
	}
	return unit;
}

void __cdecl NoSquad()
{
	Rejected(kSiteSquadize);
}

void __cdecl EmptyCommandQueue()
{
	Rejected(kSiteCommandDef);
}

int __fastcall CollectMemberIsUnit(char *obj)
{
	if (IsUnit(obj)) return 1;
	Rejected(kSiteCollect);
	return 0;
}

__declspec(naked) void HookSquadize()
{
	__asm {
		test eax, eax
		jz no_squad
		jmp dword ptr [g_squadizeCont]
	no_squad:
		call NoSquad
		add esp, 8
		jmp dword ptr [g_squadizeNext]
	}
}

__declspec(naked) void HookCommandDef()
{
	__asm {
		cmp dword ptr [eax+98h], 0
		je empty
		jmp dword ptr [g_commandDefCont]
	empty:
		call EmptyCommandQueue
		jmp dword ptr [g_commandDefNone]
	}
}

__declspec(naked) void HookMageMember()
{
	__asm {
		push eax
		push ecx
		push edx
		mov ecx, eax
		call CollectMemberIsUnit
		test eax, eax
		pop edx
		pop ecx
		pop eax
		jz skip
		jmp dword ptr [g_mageMemberCont]
	skip:
		jmp dword ptr [g_mageMemberNext]
	}
}

__declspec(naked) void HookMember()
{
	__asm {
		push eax
		push ecx
		push edx
		mov ecx, eax
		call CollectMemberIsUnit
		test eax, eax
		pop edx
		pop ecx
		pop eax
		jz skip
		jmp dword ptr [g_memberCont]
	skip:
		jmp dword ptr [g_memberNext]
	}
}

typedef void(__cdecl *OnUnitRemoved_t)(char *unit);
typedef void(__fastcall *OnDeathFinish_t)(char *unit);
typedef int(__fastcall *RemoveUnitHandle_t)(char *squad, void *edx, unsigned handle);
OnUnitRemoved_t g_origOnUnitRemoved = nullptr;
OnDeathFinish_t g_origOnDeathFinish = nullptr;
const unsigned kObjHandle = 0x08;
const unsigned kObjPlayer = 0x70;
const unsigned kUnitSquad = 0x174;
const unsigned kUnitFlags = 0x194;
const uint32_t kTemplate = 0x8000000;
const uint32_t kNoSelect = 0x800000;
volatile LONG g_leftInSquad = 0;

void LeaveSquad(char *unit, const char *why)
{
	uint16_t &squadId = *reinterpret_cast<uint16_t *>(unit + kUnitSquad);
	if (!squadId) return;
	char *squad = reinterpret_cast<GetSquad_t>(addr::kGetSquad)(unit);
	if (!squad) return;
	const unsigned handle = *reinterpret_cast<uint16_t *>(unit + kObjHandle);
	squadId = 0;
	reinterpret_cast<RemoveUnitHandle_t>(addr::kSquadRemoveUnitHandle)(squad, nullptr, handle);
	const LONG n = InterlockedIncrement(&g_leftInSquad);
	if (n <= 5 || n == 100 || n == 10000) LogD("game", "objects: unit %u leaves its squad (%s); the original game would keep it there after it is gone (%ld so far)", handle, why, n);
}

void __cdecl HookOnUnitRemoved(char *unit)
{
	if (unit && *reinterpret_cast<int *>(addr::kAiInitted) && *reinterpret_cast<uint16_t *>(unit + kUnitSquad)) {
		const uint32_t flags = *reinterpret_cast<uint32_t *>(unit + kObjFlags);
		const uint32_t unitFlags = *reinterpret_cast<uint32_t *>(unit + kUnitFlags);
		if (unitFlags & kNoSelect) LeaveSquad(unit, "it cannot be selected");
		else if (flags & kTemplate) LeaveSquad(unit, "it is a template");
		else if (!*reinterpret_cast<void **>(unit + kObjPlayer)) LeaveSquad(unit, "it has no player");
	}
	g_origOnUnitRemoved(unit);
}

void __fastcall HookOnDeathFinish(char *unit)
{
	if (unit && *reinterpret_cast<int *>(addr::kMutatingUnit)) LeaveSquad(unit, "it dies while another unit changes type");
	g_origOnDeathFinish(unit);
}

bool PatchBytes(uintptr_t site, const uint8_t *expected, size_t n, const uint8_t *patch)
{
	uint8_t *at = reinterpret_cast<uint8_t *>(site);
	if (memcmp(at, expected, n) != 0) return false;
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, patch, n);
	VirtualProtect(at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	return true;
}

bool CallInsteadOfIsDead(uintptr_t site, void *fn)
{
	const uint8_t original[] = {0x8b, 0x16, 0x8b, 0xce, 0xff, 0x52, 0x50};
	uint8_t patch[7] = {0x8b, 0xce, 0xe8};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(fn) - (site + 7));
	memcpy(patch + 3, &rel, 4);
	return PatchBytes(site, original, sizeof(original), patch);
}

bool RedirectCall(uintptr_t site, uintptr_t expectedTarget, void *fn)
{
	uint8_t original[5] = {0xe8};
	const int32_t oldRel = static_cast<int32_t>(expectedTarget - (site + 5));
	memcpy(original + 1, &oldRel, 4);
	uint8_t patch[5] = {0xe8};
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(fn) - (site + 5));
	memcpy(patch + 1, &rel, 4);
	return PatchBytes(site, original, sizeof(original), patch);
}

bool Hook(uintptr_t at, void *detour, void **orig)
{
	return HookCreate(reinterpret_cast<void *>(at), detour, orig) == MH_OK && HookEnable(reinterpret_cast<void *>(at)) == MH_OK;
}
}

void InstallHandleGuard(bool knownExe)
{
	if (!knownExe) return;
	ReadSections();
	if (!g_text.begin || !g_rdata.begin) {
		LogE("game", "objects: could not read the sections of the game executable; the checks of objects that are no longer there are off");
		return;
	}
	g_squadizeNext = reinterpret_cast<void *>(addr::kSquadizeNextUnit);
	g_commandDefNone = reinterpret_cast<void *>(addr::kScriptCommandDefNone);
	g_mageMemberNext = reinterpret_cast<void *>(addr::kCollectMageNextMember);
	g_memberNext = reinterpret_cast<void *>(addr::kCollectNextMember);
	struct Entry {
		const char *what;
		bool ok;
	} done[] = {
		{"Squad.EvalAttach", Hook(addr::kScriptFnSquadEvalAttach, reinterpret_cast<void *>(&HookEvalAttach), reinterpret_cast<void **>(&g_origEvalAttach))},
		{"Hero.TSAdvHeroSkills", Hook(addr::kScriptFnHeroTSAdvHeroSkills, reinterpret_cast<void *>(&HookAdvHeroSkills), reinterpret_cast<void **>(&g_origAdvHeroSkills))},
		{"Unit.HasFreedom", Hook(addr::kScriptFnUnitHasFreedom, reinterpret_cast<void *>(&HookHasFreedom), reinterpret_cast<void **>(&g_origHasFreedom))},
		{"AddSquadUnits", CallInsteadOfIsDead(addr::kAddSquadUnitsIsDead, reinterpret_cast<void *>(&AddSquadUnitsIsDead))},
		{"TVXSquad::ThreadLook", CallInsteadOfIsDead(addr::kThreadLookIsDead, reinterpret_cast<void *>(&ThreadLookIsDead))},
		{"Unit::BestProtPos", RedirectCall(addr::kBestProtPosFirstUnit, addr::kGetFirstUnit, reinterpret_cast<void *>(&FirstUnitChecked)) &&
			RedirectCall(addr::kBestProtPosFirstUnitAgain, addr::kGetFirstUnit, reinterpret_cast<void *>(&FirstUnitChecked))},
		{"CollectPlayerUnits (heroes)", RedirectCall(addr::kCollectHeroFirstUnit, addr::kGetFirstUnit, reinterpret_cast<void *>(&FirstUnitChecked))},
		{"CollectPlayerUnits (mages)", RedirectCall(addr::kCollectMageFirstUnit, addr::kGetFirstUnit, reinterpret_cast<void *>(&FirstUnitChecked)) &&
			Hook(addr::kCollectMageMember, reinterpret_cast<void *>(&HookMageMember), &g_mageMemberCont)},
		{"CollectPlayerUnits", Hook(addr::kCollectMember, reinterpret_cast<void *>(&HookMember), &g_memberCont)},
		{"SquadizeObjList", Hook(addr::kSquadizeSquadRead, reinterpret_cast<void *>(&HookSquadize), &g_squadizeCont)},
		{"GetCurrentScriptCommandDef", Hook(addr::kScriptCommandDefQueueRead, reinterpret_cast<void *>(&HookCommandDef), &g_commandDefCont)},
		{"AIHook_OnUnitRemoved", Hook(addr::kOnUnitRemoved, reinterpret_cast<void *>(&HookOnUnitRemoved), reinterpret_cast<void **>(&g_origOnUnitRemoved))},
		{"CVXUnit::OnDeathFinish", Hook(addr::kUnitOnDeathFinish, reinterpret_cast<void *>(&HookOnDeathFinish), reinterpret_cast<void **>(&g_origOnDeathFinish))},
		{"CVXUnit::SetPosition", Hook(addr::kUnitSetPosition, reinterpret_cast<void *>(&HookSetPosition), reinterpret_cast<void **>(&g_origSetPosition))},
	};
	int ok = 0;
	for (const Entry &e : done) {
		if (e.ok) ok++;
		else LogE("game", "objects: could not protect %s; a crash of the original game with an object that is no longer there stays possible there", e.what);
	}
	LogD("game", "objects: %d of %d places skip objects that are no longer there instead of reading them", ok, static_cast<int>(sizeof(done) / sizeof(done[0])));
}
