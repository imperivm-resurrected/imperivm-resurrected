// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/orderdelay.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "addr/net.h"
#include "addr/script.h"
#include "addr/world.h"
#include "core/log.h"
#include "hooks/hookcount.h"

// A player's order goes to CVXCmdPump's local list (AddLocalCommand 004f6e30). Every command turn, CVXCmdPump::Tick (004f79d0)
// first runs the turn's lists and only then moves the local list into them (ExecuteQueued 004f6e80): the order waits for the
// next turn and runs on the one after. In multiplayer that turn is the time the order needs to reach the others, but out of it
// the game keeps the same wait: the turn is 150 ms (CVXGameSingle::Update 0051ea30 and CVXGameAdv::Update 004ccc80 schedule
// it with 0x96), so a click took 150 to 300 ms to start.
// Out of multiplayer an order now runs in the same frame: after the game's update, a pending order goes into the turn's lists
// and the pump runs them at once. The replay is safe: Tick records each run with the game clock (game+0x1258, the bucket's
// +0xf0) and playback runs a bucket when the simulation reaches that very clock (RunTicksUpTo 00528b40), not on a turn.

namespace {

typedef int(__fastcall *Update_t)(void *game);
typedef void(__fastcall *Pump_t)(void *pump);
typedef int(__cdecl *IsDemo_t)();

Update_t g_origSingle = nullptr;
Update_t g_origAdventure = nullptr;
Pump_t g_origTick = nullptr;
// Set while the game of a single player (skirmish, scenario, adventure) runs its update: CVXGameMultiplayer::Update (00550890)
// drives the pump through the network transport and never passes here.
int g_solo = 0;

struct SoloScope {
	SoloScope() { g_solo++; }
	~SoloScope() { g_solo--; }
};

bool Replaying()
{
	__try {
		return reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return true;
	}
}

// CVXGame+0x1054 is the pump; its local lists, one per player (16), are std::lists of 0xc bytes from +0xf4, size at +8.
const unsigned kGamePump = 0x1054, kPumpLocal = 0xf4, kListSize = 0xc, kListCount = 8;
// CVXGameSingle::Update only runs the pump while game+0x22c is 0.
const unsigned kGameSingleHold = 0x22c;

bool OrderWaiting(const char *pump)
{
	__try {
		for (int i = 0; i < 16; i++) {
			if (*reinterpret_cast<const unsigned *>(pump + kPumpLocal + i * kListSize + kListCount) != 0) return true;
		}
		return false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool Held(const char *game)
{
	__try {
		return *reinterpret_cast<const int *>(game + kGameSingleHold) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return true;
	}
}

unsigned g_rightAway = 0;

// DIAGNOSTIC (diag/order-latency, never for main): times an order of the local player from the click to the first move of the
// first selected unit, in real ms and in game clock.
typedef void(__fastcall *Queue_t)(void *game, void *edx, void *cmd);
Queue_t g_origQueue = nullptr;
const unsigned kGameClock = 0x1258, kLocalPlayer = 0x12c8, kCmdPlayer = 4, kPlayerSelection = 0x27c, kObjX = 0x24, kObjY = 0x28;

struct Probe {
	bool active = false, executed = false;
	unsigned handle = 0;
	int x = 0, y = 0;
	LARGE_INTEGER click = {}, run = {};
	unsigned clockClick = 0, clockRun = 0;
	uintptr_t vtable = 0;
	unsigned frames = 0;
} g_probe;
LARGE_INTEGER g_freq = {};

double Ms(const LARGE_INTEGER &a, const LARGE_INTEGER &b) { return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart); }

bool UnitAt(unsigned handle, int &x, int &y)
{
	__try {
		const char *obj = reinterpret_cast<char **>(addr::kHandleTable)[handle & 0xffff];
		if (!obj) return false;
		x = *reinterpret_cast<const int *>(obj + kObjX);
		y = *reinterpret_cast<const int *>(obj + kObjY);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool FirstSelected(const char *game, unsigned &handle, unsigned &clock, uintptr_t &vtable, const void *cmd)
{
	__try {
		const char *local = *reinterpret_cast<char *const *>(game + kLocalPlayer);
		if (!local || *reinterpret_cast<char *const *>(static_cast<const char *>(cmd) + kCmdPlayer) != local) return false;
		vtable = *reinterpret_cast<const uintptr_t *>(cmd);
		const char *head = *reinterpret_cast<char *const *>(local + kPlayerSelection);
		const char *node = head ? *reinterpret_cast<char *const *>(head) : nullptr;
		if (!node || node == head) return false;
		handle = *reinterpret_cast<const unsigned short *>(node + 8);
		clock = *reinterpret_cast<const unsigned *>(game + kGameClock);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

unsigned ClockOf(const void *game)
{
	__try {
		return *reinterpret_cast<const unsigned *>(static_cast<const char *>(game) + kGameClock);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

void __fastcall HookQueue(void *game, void *edx, void *cmd)
{
	Probe p;
	if (game && cmd && FirstSelected(static_cast<const char *>(game), p.handle, p.clockClick, p.vtable, cmd) && UnitAt(p.handle, p.x, p.y)) {
		p.active = true;
		QueryPerformanceCounter(&p.click);
		g_probe = p;
	}
	g_origQueue(game, edx, cmd);
}

void ProbeRan(void *game)
{
	if (!g_probe.active || g_probe.executed) return;
	g_probe.executed = true;
	QueryPerformanceCounter(&g_probe.run);
	g_probe.clockRun = ClockOf(game);
}

void ProbeFrame(void *game)
{
	if (!g_probe.active || !g_probe.executed) return;
	g_probe.frames++;
	int x = 0, y = 0;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const bool moved = UnitAt(g_probe.handle, x, y) && (x != g_probe.x || y != g_probe.y);
	if (!moved && Ms(g_probe.click, now) < 5000) return;
	LogI("game", "orderprobe: command vtable %08x, unit %u: click->run %.1f ms (clock +%u), run->%s %.1f ms (clock +%u, %u frames)",
		static_cast<unsigned>(g_probe.vtable), g_probe.handle, Ms(g_probe.click, g_probe.run), g_probe.clockRun - g_probe.clockClick,
		moved ? "first move" : "NO MOVE within 5 s", Ms(g_probe.run, now), ClockOf(game) - g_probe.clockRun, g_probe.frames);
	g_probe.active = false;
}

void RunWaitingOrders(void *game)
{
	char *pump = static_cast<char *>(game) + kGamePump;
	if (!OrderWaiting(pump) || Replaying()) {
		ProbeFrame(game);
		return;
	}
	reinterpret_cast<Pump_t>(addr::kCmdPumpExecuteQueued)(pump);
	g_origTick(pump);
	ProbeRan(game);
	if (g_rightAway++ == 0) LogI("game", "orders: out of multiplayer, orders run in the frame they are given");
}

int __fastcall HookSingle(void *game)
{
	int result;
	{
		SoloScope scope;
		result = g_origSingle(game);
	}
	if (result == 0 && game && !Held(static_cast<const char *>(game))) RunWaitingOrders(game);
	return result;
}

int __fastcall HookAdventure(void *game)
{
	int result;
	{
		SoloScope scope;
		result = g_origAdventure(game);
	}
	if (result == 0 && game) RunWaitingOrders(game);
	return result;
}

void __fastcall HookTick(void *pump)
{
	// An order given between this frame's update and the turn (by a script, say) still runs on this turn.
	if (g_solo > 0 && pump && !Replaying()) reinterpret_cast<Pump_t>(addr::kCmdPumpExecuteQueued)(pump);
	g_origTick(pump);
	if (g_solo > 0 && pump) ProbeRan(static_cast<char *>(pump) - kGamePump);
}

bool Hook(uintptr_t target, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(target), detour, orig) == MH_OK) return true;
	LogE("game", "orders: could not hook %s (%08x)", what, static_cast<unsigned>(target));
	return false;
}

}

void InstallOrderDelay(bool knownExe)
{
	if (!knownExe) return;
	const uintptr_t targets[] = {addr::kCmdPumpTick, addr::kGameSingleUpdate, addr::kGameAdvUpdate};
	bool created[3] = {};
	created[0] = Hook(addr::kCmdPumpTick, reinterpret_cast<void *>(&HookTick), reinterpret_cast<void **>(&g_origTick), "CVXCmdPump::Tick");
	created[1] = created[0] && Hook(addr::kGameSingleUpdate, reinterpret_cast<void *>(&HookSingle), reinterpret_cast<void **>(&g_origSingle), "CVXGameSingle::Update");
	created[2] = created[1] && Hook(addr::kGameAdvUpdate, reinterpret_cast<void *>(&HookAdventure), reinterpret_cast<void **>(&g_origAdventure), "CVXGameAdv::Update");
	if (!created[2]) {
		for (int i = 0; i < 3; i++) {
			if (created[i]) MH_RemoveHook(reinterpret_cast<void *>(targets[i]));
		}
		LogE("game", "orders: orders keep the game's own wait");
		return;
	}
	bool ok = true;
	for (uintptr_t t : targets) ok = HookEnable(reinterpret_cast<void *>(t)) == MH_OK && ok;
	QueryPerformanceFrequency(&g_freq);
	if (Hook(addr::kQueueCommand, reinterpret_cast<void *>(&HookQueue), reinterpret_cast<void **>(&g_origQueue), "CVXGame::QueueCommand") &&
		HookEnable(reinterpret_cast<void *>(addr::kQueueCommand)) == MH_OK)
		LogI("game", "orderprobe: timing the local player's orders (diagnostic build)");
	LogAt(ok ? kLogInfo : kLogError, "game", "orders: faster orders out of multiplayer %s", ok ? "ready" : "NOT available (a hook could not be enabled)");
}
