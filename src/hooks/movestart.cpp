// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/movestart.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "addr/world.h"
#include "core/log.h"
#include "hooks/codepatch.h"
#include "hooks/hookcount.h"

// Ported from Classic Modern's MovementFastStart (the code and its guide were sent to the project).
//
// When a move order is accepted (ScriptFn_Unit_Goto, CVXUnit's formation move and the hero's two formation moves), the unit is
// put in state 3 and asks CVXMapObj::IsAnimIdle (0053cfc0) whether its action thread 0x1b is gone; only then does the game
// schedule the march at now + 1. A unit standing in an idle animation (12 to 15) still has that thread, so the march waited for
// the animation to end. At those four calls, a unit with a path, out of any holder, whose idle animation is the one shown and
// that still stands where the action began, stops the animation and goes now.
//
// A unit walking a stride keeps its old route until the stride ends. Before CVXPathRetry::SetGoalPoint (0041a4c0) takes a new
// point, and before CVXPath::Reset (00417790) binds a formation at the three formation orders, CVXMapObj::StopAnimation
// (0053d730) puts the unit at its interpolated place, so the new route starts from there.
//
// Every input is the simulation's own (the unit's state, its threads, the game clock at unit+0x18), so every computer of a
// match takes the same decision for local, remote and computer orders alike. A computer without it would not: MoveStartActive
// marks the rooms (the Steam room's irlvl and the game's version hash).

namespace {

constexpr int kActionThread = 0x1b, kMoving = 3, kFirstIdleAnim = 12, kLastIdleAnim = 15;

typedef void *(__fastcall *FindThread_t)(void *self, void *edx, int id);
typedef void(__fastcall *StopAnimation_t)(void *unit);
typedef int(__fastcall *IsAnimIdle_t)(void *unit);
typedef void(__fastcall *PathReset_t)(void *path, void *edx, void *formation);
typedef int(__fastcall *SetGoalPoint_t)(void *path, void *edx, int force, int x, int y, int range, int minRange, void *target, int mode);

SetGoalPoint_t g_origSetGoal = nullptr;
bool g_active = false;
volatile LONG g_starts = 0, g_turns = 0;

template <class T> T At(const unsigned char *p, unsigned offset) { return *reinterpret_cast<const T *>(p + offset); }

const unsigned char *ActionThread(unsigned char *unit)
{
	return static_cast<const unsigned char *>(reinterpret_cast<FindThread_t>(addr::kFindThread)(unit, nullptr, kActionThread));
}

// A CVXUnit that moves as one (SetPosition and UpdateAction its own), not a ship.
bool PlainUnit(const unsigned char *unit, bool interpolates)
{
	const uintptr_t *vt = At<const uintptr_t *>(unit, 0);
	return vt[0x38 / 4] == addr::kUnitSetPosition && vt[0x54 / 4] == addr::kUnitUpdateAction &&
		(!interpolates || vt[0x40 / 4] == addr::kGetInterpolatedPosition) && reinterpret_cast<uintptr_t>(vt) != addr::kShipVtable;
}

void Counted(volatile LONG &counter, const char *what)
{
	const LONG n = InterlockedIncrement(&counter);
	if (n == 1) LogI("game", "movement: %s (first time this session)", what);
	else if (n % 100 == 0) LogD("game", "movement: %s, %ld times", what, static_cast<long>(n));
}

// 1 when the unit can start its march now; the decision is the game's own except for a unit held by its idle animation.
int Startable(unsigned char *unit, bool &stop)
{
	const unsigned char *thread = ActionThread(unit);
	if (!thread) return 1;
	if (!PlainUnit(unit, false)) return 0;
	const uint32_t *clock = At<const uint32_t *>(unit, 0x18);
	if (!clock) return 0;
	const int anim = At<int>(unit, 0x50);
	const uint32_t now = *clock, due = At<uint32_t>(thread, 4);
	if (At<int>(unit, 0x130) != kMoving || !At<void *>(unit, 0x148) || At<uint16_t>(unit, 0x154) != 0xffff || static_cast<int32_t>(due - now) <= 1) {
		return 0;
	}
	if (anim != -1) {
		const bool idle = anim >= kFirstIdleAnim && anim <= kLastIdleAnim && At<int>(unit, 0x44) == anim && At<int>(unit, 0x24) == At<int>(unit, 0x5c) &&
			At<int>(unit, 0x28) == At<int>(unit, 0x60);
		if (!idle) return 0;
		stop = true;
	}
	return 2;
}

// In place of IsAnimIdle at the four move orders.
int __fastcall StartReady(unsigned char *unit)
{
	bool stop = false;
	int ready = 0;
	__try {
		ready = Startable(unit, stop);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return reinterpret_cast<IsAnimIdle_t>(addr::kIsAnimIdle)(unit);
	}
	if (ready < 2) return ready;
	if (stop) reinterpret_cast<StopAnimation_t>(addr::kStopAnimation)(unit);
	Counted(g_starts, "a unit started its march without waiting for its idle animation");
	return 1;
}

// The unit walking the path, mid-stride, that a new destination should turn from where it is; null for any other.
unsigned char *WalkingUnit(unsigned char *path)
{
	unsigned char *unit = At<unsigned char *>(path, 0x3c);
	if (!unit || At<void *>(unit, 0x148) != path || At<int>(unit, 0x130) != kMoving || At<uint16_t>(unit, 0x154) != 0xffff) return nullptr;
	if (!PlainUnit(unit, true)) return nullptr;
	const int anim = At<int>(unit, 0x50);
	if (anim < 0 || anim >= kFirstIdleAnim || anim != At<uint16_t>(unit, 0x1bc) || anim != At<int>(unit, 0x44)) return nullptr;
	const uint32_t duration = At<uint32_t>(unit, 0x64), began = At<uint32_t>(unit, 0x68);
	const uint32_t *clock = At<const uint32_t *>(unit, 0x18);
	// The game interpolates with plain unsigned comparisons: a stride across the clock's wrap keeps the game's way.
	if (!duration || duration > 60000 || !clock || duration > UINT32_MAX - began || *clock < began || *clock >= began + duration) return nullptr;
	if (At<int>(unit, 0x24) == At<int>(unit, 0x5c) && At<int>(unit, 0x28) == At<int>(unit, 0x60)) return nullptr;
	const unsigned char *timer = ActionThread(unit);
	if (!timer || static_cast<int32_t>(At<uint32_t>(timer, 4) - *clock) <= 1) return nullptr;
	return unit;
}

unsigned char *SafeWalkingUnit(unsigned char *path)
{
	__try {
		return path ? WalkingUnit(path) : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

unsigned char *RedirectedUnit(unsigned char *path, int x, int y)
{
	__try {
		if (At<int>(path, 0x10) == x && At<int>(path, 0x14) == y && !At<void *>(path, 0xc)) return nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
	return SafeWalkingUnit(path);
}

void Turn(unsigned char *unit)
{
	reinterpret_cast<StopAnimation_t>(addr::kStopAnimation)(unit);
	Counted(g_turns, "a walking unit turned from where it was to its new destination");
}

int __fastcall HookSetGoal(unsigned char *path, void *edx, int force, int x, int y, int range, int minRange, void *target, int mode)
{
	if (path && !target && x >= 0 && y >= 0) {
		if (unsigned char *unit = RedirectedUnit(path, x, y)) Turn(unit);
	}
	return g_origSetGoal(path, edx, force, x, y, range, minRange, target, mode);
}

// In place of CVXPath::Reset where a formation order binds the unit's route.
void __fastcall BindFormation(unsigned char *path, void *edx, void *formation)
{
	if (unsigned char *unit = SafeWalkingUnit(path)) Turn(unit);
	reinterpret_cast<PathReset_t>(addr::kPathReset)(path, edx, formation);
}

struct CallSite {
	uintptr_t at;
	uintptr_t original;
	void *detour;
	const char *what;
};

}

bool MoveStartActive() { return g_active; }

void InstallMoveStart(bool knownExe)
{
	if (!knownExe) return;
	const CallSite sites[] = {
		{addr::kUnitGotoIdleCheck, addr::kIsAnimIdle, reinterpret_cast<void *>(&StartReady), "Unit::Goto"},
		{addr::kFormAcceptMoveIdleCheck, addr::kIsAnimIdle, reinterpret_cast<void *>(&StartReady), "Unit::FormAcceptMove"},
		{addr::kHeroMovePointIdleCheck, addr::kIsAnimIdle, reinterpret_cast<void *>(&StartReady), "Hero::FormSetupAndMoveTo(point)"},
		{addr::kHeroMoveObjIdleCheck, addr::kIsAnimIdle, reinterpret_cast<void *>(&StartReady), "Hero::FormSetupAndMoveTo(object)"},
		{addr::kFormAcceptMovePathReset, addr::kPathReset, reinterpret_cast<void *>(&BindFormation), "Unit::FormAcceptMove's route"},
		{addr::kHeroMovePointPathReset, addr::kPathReset, reinterpret_cast<void *>(&BindFormation), "the hero's route to a point"},
		{addr::kHeroMoveObjPathReset, addr::kPathReset, reinterpret_cast<void *>(&BindFormation), "the hero's route to an object"},
	};
	const int count = static_cast<int>(sizeof(sites) / sizeof(sites[0]));
	uint8_t original[count][5], patched[count][5];
	for (int i = 0; i < count; i++) {
		CallBytes(original[i], sites[i].at, sites[i].original);
		CallBytes(patched[i], sites[i].at, reinterpret_cast<uintptr_t>(sites[i].detour));
		if (memcmp(reinterpret_cast<const void *>(sites[i].at), original[i], 5) != 0) {
			LogE("game", "movement: the call in %s (%08x) is not the expected one; units keep the game's start", sites[i].what, static_cast<unsigned>(sites[i].at));
			return;
		}
	}
	void *setGoal = reinterpret_cast<void *>(addr::kPathSetGoalPoint);
	if (HookCreate(setGoal, reinterpret_cast<void *>(&HookSetGoal), reinterpret_cast<void **>(&g_origSetGoal)) != MH_OK) {
		LogE("game", "movement: could not hook CVXPathRetry::SetGoalPoint; units keep the game's start");
		return;
	}
	if (HookEnable(setGoal) != MH_OK) {
		MH_RemoveHook(setGoal);
		LogE("game", "movement: could not enable the SetGoalPoint hook; units keep the game's start");
		return;
	}
	// All or none: half of it would still be a simulation of its own, without the mark that keeps it apart.
	int done = 0;
	while (done < count && PatchCode(sites[done].at, original[done], patched[done], 5)) done++;
	if (done < count) {
		LogE("game", "movement: could not patch the call in %s (%08x); undone", sites[done].what, static_cast<unsigned>(sites[done].at));
		while (done-- > 0) WriteCode(reinterpret_cast<void *>(sites[done].at), original[done], 5);
		MH_DisableHook(setGoal);
		MH_RemoveHook(setGoal);
		return;
	}
	g_active = true;
	LogI("game", "movement: units start at once and turn mid-stride (the rooms are marked as this simulation)");
}
