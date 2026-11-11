// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/orderdelay.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "addr/net.h"
#include "addr/world.h"
#include "core/log.h"
#include "hooks/hookcount.h"

// A player's order goes to CVXCmdPump's local list (AddLocalCommand 004f6e30). Every command turn, CVXCmdPump::Tick (004f79d0)
// first runs the turn's lists and only then moves the local list into them (ExecuteQueued 004f6e80): the order waits for the
// next turn and runs on the one after. In multiplayer that turn is the time the order needs to reach the others, but out of it
// the game keeps the same wait: the turn is 150 ms (CVXGameSingle::Update 0051ea30 and CVXGameAdv::Update 004ccc80 schedule
// it with 0x96), so a click took 150 to 300 ms to start. Out of multiplayer the local list now goes into the turn before it
// runs: 0 to 150 ms. The replay records the turn's lists after this (Tick copies them before running them), so a replay still
// plays every order on the turn it ran.

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
bool g_logged = false;

struct SoloScope {
	SoloScope() { g_solo++; }
	~SoloScope() { g_solo--; }
};

int __fastcall HookSingle(void *game)
{
	SoloScope scope;
	return g_origSingle(game);
}

int __fastcall HookAdventure(void *game)
{
	SoloScope scope;
	return g_origAdventure(game);
}

bool Replaying()
{
	__try {
		return reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return true;
	}
}

void __fastcall HookTick(void *pump)
{
	if (g_solo > 0 && pump && !Replaying()) {
		reinterpret_cast<Pump_t>(addr::kCmdPumpExecuteQueued)(pump);
		if (!g_logged) {
			g_logged = true;
			LogI("game", "orders: out of multiplayer, orders run on the next command turn (150 ms at most, was up to 300)");
		}
	}
	g_origTick(pump);
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
	LogAt(ok ? kLogInfo : kLogError, "game", "orders: faster orders out of multiplayer %s", ok ? "ready" : "NOT available (a hook could not be enabled)");
}
