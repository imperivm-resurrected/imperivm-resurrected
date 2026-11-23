// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/movestart.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addr/world.h"
#include "core/log.h"
#include "hooks/codepatch.h"

// Ported from Classic Modern's MovementFastStart (the code and its guide were sent to the project).
//
// When a move order is accepted (ScriptFn_Unit_Goto, CVXUnit's formation move and the hero's two formation moves), the unit is
// put in state 3 and asks CVXMapObj::IsAnimIdle (0053cfc0) whether its action thread 0x1b is gone; only then does the game
// schedule the march at now + 1. A unit standing in an idle animation (12 to 15) still has that thread, so the march waited for
// the animation to end. At those four calls, a unit with a path, out of any holder, whose idle animation is the one shown and
// that still stands where the action began, stops the animation and goes now.
//
// The port also turned a unit walking a stride from its interpolated place when CVXPathRetry::SetGoalPoint (0041a4c0) or a
// formation's CVXPath::Reset (00417790) gave it a new route. That turn decided on fields no fingerprint covers (the route's goal,
// the walk variant +0x1bc, the stride's +0x64/+0x68) and split a ranked 1v1 on 2026-11-23: at tick 1733 a capture order reached
// the same army on both computers, one turned soldier 1697 mid-stride (StopAnimation, SetPosition, StartAnimThread) and the
// other let the stride end. A walking unit keeps the game's own rule again: its old route until the stride ends.
//
// What is left reads the unit's state, its threads and the game clock at unit+0x18, the same inputs the game's own move code
// branches on. A computer without it would still decide otherwise: MoveStartActive marks the rooms (movemark.h).

namespace {

constexpr int kActionThread = 0x1b, kMoving = 3, kFirstIdleAnim = 12, kLastIdleAnim = 15;

typedef void *(__fastcall *FindThread_t)(void *self, void *edx, int id);
typedef void(__fastcall *StopAnimation_t)(void *unit);
typedef int(__fastcall *IsAnimIdle_t)(void *unit);

bool g_active = false;
volatile LONG g_starts = 0;

template <class T> T At(const unsigned char *p, unsigned offset) { return *reinterpret_cast<const T *>(p + offset); }

const unsigned char *ActionThread(unsigned char *unit)
{
	return static_cast<const unsigned char *>(reinterpret_cast<FindThread_t>(addr::kFindThread)(unit, nullptr, kActionThread));
}

// A CVXUnit that moves as one (SetPosition and UpdateAction its own), not a ship.
bool PlainUnit(const unsigned char *unit)
{
	const uintptr_t *vt = At<const uintptr_t *>(unit, 0);
	return vt[0x38 / 4] == addr::kUnitSetPosition && vt[0x54 / 4] == addr::kUnitUpdateAction && reinterpret_cast<uintptr_t>(vt) != addr::kShipVtable;
}

// 1 when the unit can start its march now; the decision is the game's own except for a unit held by its idle animation.
int Startable(unsigned char *unit, bool &stop)
{
	const unsigned char *thread = ActionThread(unit);
	if (!thread) return 1;
	if (!PlainUnit(unit)) return 0;
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
	const LONG n = InterlockedIncrement(&g_starts);
	if (n == 1) LogI("game", "movement: a unit started its march without waiting for its idle animation (first time this session)");
	else if (n % 100 == 0) LogD("game", "movement: a unit started its march without waiting for its idle animation, %ld times", static_cast<long>(n));
	return 1;
}

struct CallSite {
	uintptr_t at;
	const char *what;
};

}

bool MoveStartActive() { return g_active; }

void InstallMoveStart(bool knownExe)
{
	if (!knownExe) return;
	const CallSite sites[] = {
		{addr::kUnitGotoIdleCheck, "Unit::Goto"},
		{addr::kFormAcceptMoveIdleCheck, "Unit::FormAcceptMove"},
		{addr::kHeroMovePointIdleCheck, "Hero::FormSetupAndMoveTo(point)"},
		{addr::kHeroMoveObjIdleCheck, "Hero::FormSetupAndMoveTo(object)"},
	};
	const int count = static_cast<int>(sizeof(sites) / sizeof(sites[0]));
	uint8_t original[count][5], patched[count][5];
	for (int i = 0; i < count; i++) {
		CallBytes(original[i], sites[i].at, addr::kIsAnimIdle);
		CallBytes(patched[i], sites[i].at, reinterpret_cast<uintptr_t>(&StartReady));
		if (memcmp(reinterpret_cast<const void *>(sites[i].at), original[i], 5) != 0) {
			LogE("game", "movement: the call in %s (%08x) is not the expected one; units keep the game's start", sites[i].what, static_cast<unsigned>(sites[i].at));
			return;
		}
	}
	// All or none: half of it would still be a simulation of its own, without the mark that keeps it apart.
	int done = 0;
	while (done < count && PatchCode(sites[done].at, original[done], patched[done], 5)) done++;
	if (done < count) {
		LogE("game", "movement: could not patch the call in %s (%08x); undone", sites[done].what, static_cast<unsigned>(sites[done].at));
		while (done-- > 0) WriteCode(reinterpret_cast<void *>(sites[done].at), original[done], 5);
		return;
	}
	g_active = true;
	LogI("game", "movement: units start at once (the rooms are marked as this simulation)");
}
