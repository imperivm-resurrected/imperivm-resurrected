// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/movestart.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "addr/net.h"
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
// A unit walking a stride and given a new order turns at once: CVXMapObj::StopAnimation (0053d730) puts it at its place on the
// stride at the game clock (GetInterpolatedPosition, unit+0x18 is the simulation's clock, never the screen's), and the route is
// recast from there. 1.0.3 did it from a MinHook on CVXPathRetry::SetGoalPoint (every caller, temporary paths and formations'
// own included) and only when the point differed from the route's stored goal. A ranked 1v1 split on 2026-11-23 at tick 1733:
// replaying both fingerprint chains of that tick (slots and threads are rotate-xor chains, so each record can be undone) shows
// that the other computer took everything else identically and let soldier 1697 end its stride at 600377 instead of turning it
// at 600325. The inputs of that decision were in no fingerprint, so which one differed cannot be read from one computer's dump.
//
// Now the turn is taken only inside the lockstep move natives (the call to SetGoalPoint in Unit::Goto, Unit::GotoEnter and the
// latent Goto toward an object or point, and the call to CVXPath::Reset of the three formation moves), reads only the unit
// (state, holder, its own route, the stride's animation and times, its action thread and the clock) and never the route's goal:
// a turned unit has its route recast whatever the goal, so no stored goal decides anything; a Goto a script repeats on a walking
// unit turns it each time. Each turn (the unit, the clock, where it stopped) is folded into the sync state's "extra" counter, which
// every computer compares on every tick: a computer that decides otherwise is reported with "extra" among the counters that differ.
//
// A computer without it would still decide otherwise: MoveStartActive marks the rooms (movemark.h).

namespace {

constexpr int kActionThread = 0x1b, kMoving = 3, kFirstIdleAnim = 12, kLastIdleAnim = 15;
// A stride closer than this to its end is left to end: the new route takes over within it anyway.
constexpr uint32_t kMinRemaining = 100;
// A flag of CVXPathRetry+0x20 that SetGoalPoint takes as "the goal changed" and clears as it recasts.
constexpr uint32_t kRecastFlag = 0x2;
// The sync state's counter 6 ("extra", the game's fire smoke checksum).
constexpr uintptr_t kExtraCounter = addr::kSyncState + 0x20;

typedef void *(__fastcall *FindThread_t)(void *self, void *edx, int id);
typedef void(__fastcall *StopAnimation_t)(void *unit);
typedef int(__fastcall *IsAnimIdle_t)(void *unit);
typedef void(__fastcall *PathReset_t)(void *path, void *edx, void *formation);
typedef int(__fastcall *SetGoalPoint_t)(void *path, void *edx, int force, int x, int y, int range, int minRange, void *target, int mode);

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

void Fold(uint32_t value)
{
	uint32_t &h = *reinterpret_cast<uint32_t *>(kExtraCounter);
	h = (h << 1 | h >> 31) ^ value;
}

// What the turn decides on, all of it the unit's own simulation state at the clock of the order.
struct Stride {
	uint16_t handle;
	int anim, shown, walk;
	uint32_t now, due, began, duration;
	int x, y, fromX, fromY;
};

// Whether the unit walking this route is mid-stride and should turn now; false for any other route or unit.
bool ShouldTurn(unsigned char *path, unsigned char *&unit)
{
	unit = At<unsigned char *>(path, 0x3c);
	if (!unit || At<void *>(unit, 0x148) != path || At<int>(unit, 0x130) != kMoving || At<uint16_t>(unit, 0x154) != 0xffff) return false;
	if (!PlainUnit(unit, true)) return false;
	const uint32_t *clock = At<const uint32_t *>(unit, 0x18);
	const unsigned char *timer = ActionThread(unit);
	if (!clock || !timer) return false;
	Stride s;
	s.handle = At<uint16_t>(unit, 8);
	s.anim = At<int>(unit, 0x50);
	s.shown = At<int>(unit, 0x44);
	s.walk = At<uint16_t>(unit, 0x1bc);
	s.now = *clock;
	s.due = At<uint32_t>(timer, 4);
	s.began = At<uint32_t>(unit, 0x68);
	s.duration = At<uint32_t>(unit, 0x64);
	s.x = At<int>(unit, 0x24);
	s.y = At<int>(unit, 0x28);
	s.fromX = At<int>(unit, 0x5c);
	s.fromY = At<int>(unit, 0x60);
	// The walk animation, shown (past any transition), with the game clock inside its stride and the stride's end far enough. The game
	// interpolates with plain unsigned comparisons: a stride across the clock's wrap keeps the game's way.
	const bool walking = s.anim >= 0 && s.anim < kFirstIdleAnim && s.anim == s.walk && s.anim == s.shown;
	const bool inStride = s.duration && s.duration <= 60000 && s.duration <= UINT32_MAX - s.began && s.now >= s.began && s.now < s.began + s.duration;
	return walking && inStride && (s.x != s.fromX || s.y != s.fromY) && static_cast<int32_t>(s.due - s.now) >= static_cast<int32_t>(kMinRemaining);
}

// The turn itself, not its inputs: an input that differs without changing the simulation must not end a match.
void FoldTurn(const unsigned char *unit)
{
	Fold(At<uint16_t>(unit, 8));
	Fold(*At<const uint32_t *>(unit, 0x18));
	Fold(At<uint32_t>(unit, 0x24));
	Fold(At<uint32_t>(unit, 0x28));
}

// The unit turned, or null.
unsigned char *TurnWalking(unsigned char *path)
{
	unsigned char *unit = nullptr;
	bool turn = false;
	__try {
		turn = path && ShouldTurn(path, unit);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
	if (!turn) return nullptr;
	reinterpret_cast<StopAnimation_t>(addr::kStopAnimation)(unit);
	FoldTurn(unit);
	Counted(g_turns, "a walking unit turned from where it was to its new order");
	return unit;
}

// In place of CVXPathRetry::SetGoalPoint at the three Goto natives: a turned unit's route is recast from where it stopped.
int __fastcall OrderSetGoal(unsigned char *path, void *edx, int force, int x, int y, int range, int minRange, void *target, int mode)
{
	if (TurnWalking(path)) *reinterpret_cast<uint32_t *>(path + 0x20) |= kRecastFlag;
	return reinterpret_cast<SetGoalPoint_t>(addr::kPathSetGoalPoint)(path, edx, force, x, y, range, minRange, target, mode);
}

// In place of CVXPath::Reset where a formation order binds the unit's route (Reset drops the route; the formation recasts it).
void __fastcall OrderFormation(unsigned char *path, void *edx, void *formation)
{
	TurnWalking(path);
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
	void *start = reinterpret_cast<void *>(&StartReady), *setGoal = reinterpret_cast<void *>(&OrderSetGoal), *bind = reinterpret_cast<void *>(&OrderFormation);
	const CallSite sites[] = {
		{addr::kUnitGotoIdleCheck, addr::kIsAnimIdle, start, "Unit::Goto"},
		{addr::kFormAcceptMoveIdleCheck, addr::kIsAnimIdle, start, "Unit::FormAcceptMove"},
		{addr::kHeroMovePointIdleCheck, addr::kIsAnimIdle, start, "Hero::FormSetupAndMoveTo(point)"},
		{addr::kHeroMoveObjIdleCheck, addr::kIsAnimIdle, start, "Hero::FormSetupAndMoveTo(object)"},
		{addr::kUnitGotoSetGoalPoint, addr::kPathSetGoalPoint, setGoal, "Unit::Goto's route"},
		{addr::kUnitGotoEnterSetGoalPoint, addr::kPathSetGoalPoint, setGoal, "Unit::GotoEnter's route"},
		{addr::kGotoLatentSetGoalPoint, addr::kPathSetGoalPoint, setGoal, "the latent Goto's route"},
		{addr::kFormAcceptMovePathReset, addr::kPathReset, bind, "Unit::FormAcceptMove's route"},
		{addr::kHeroMovePointPathReset, addr::kPathReset, bind, "the hero's route to a point"},
		{addr::kHeroMoveObjPathReset, addr::kPathReset, bind, "the hero's route to an object"},
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
	// All or none: half of it would still be a simulation of its own, without the mark that keeps it apart.
	int done = 0;
	while (done < count && PatchCode(sites[done].at, original[done], patched[done], 5)) done++;
	if (done < count) {
		LogE("game", "movement: could not patch the call in %s (%08x); undone", sites[done].what, static_cast<unsigned>(sites[done].at));
		while (done-- > 0) WriteCode(reinterpret_cast<void *>(sites[done].at), original[done], 5);
		return;
	}
	g_active = true;
	LogI("game", "movement: units start at once and turn mid-stride when re-ordered (the rooms are marked as this simulation)");
}
