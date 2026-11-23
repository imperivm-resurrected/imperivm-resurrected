// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <map>
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "core/log.h"
#include "net/leagueclient.h"
#include "net/netdrop.h"
#include "net/nettune.h"
#include "game/cheats.h"
#include "net/steamnet.h"
#include "addr/engine.h"
#include "addr/net.h"
#include "addr/world.h"

#include "net/rejoin_internal.h"
#include "core/uiport.h"

using namespace rejoin_impl;

namespace rejoin_impl {
void FillRing(char *t, int myIndex)
{
	const char *p = g_blobs.wireRaw.data() + sizeof(WireState);
	const unsigned now = Now();
	for (int i = 0; i < 8; i++) {
		const uint32_t size = g_blobs.wire.blob[i];
		if (!size) continue;
		if (i != myIndex) {
			char *rec = Record(t, i, g_blobs.wire.tick);
			if (!*reinterpret_cast<char **>(rec + 0x14)) {
				char *buf = static_cast<char *>(reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(size));
				memcpy(buf, p, size);
				*reinterpret_cast<uint32_t *>(rec + 0x10) = size;
				*reinterpret_cast<char **>(rec + 0x14) = buf;
				*reinterpret_cast<unsigned *>(rec + 0x1c) = now;
			}
		}
		p += size;
	}
}

void SendOwnTickData(char *t)
{
	const int me = *reinterpret_cast<int *>(t + 0x4c);
	char *rec = Record(t, me, g_blobs.wire.tick);
	char *gate = *reinterpret_cast<char **>(t + 0x50);
	const uint32_t size = *reinterpret_cast<uint32_t *>(rec + 0x10);
	char *buf = *reinterpret_cast<char **>(rec + 0x14);
	if (!gate || !buf || !size) {
		LogD("rejoin", "no own data of tick %d to send", g_blobs.wire.tick);
		return;
	}
	int desc[8] = {};
	desc[5] = reinterpret_cast<int>(buf);
	desc[6] = static_cast<int>(size);
	reinterpret_cast<SendPacket_t>(addr::kSendPacket)(gate, nullptr, static_cast<uint8_t>(*reinterpret_cast<uint8_t *>(gate + 0x108) | g_session.restoreBits),
		static_cast<unsigned>(g_blobs.wire.tick), desc);
}
}

namespace rejoin_impl {
namespace {
void *__fastcall AdvanceTickInner(void *tp)
{
	char *t = static_cast<char *>(tp);
	int traceTick;
	if (Tracing(traceTick) && traceTick != g_trace.lastTick) {
		g_trace.lastTick = traceTick;
		TracePush({traceTick, 'T', 0, 0, 0, 0, 0, 0, ThreadsHash()});
	}
	if (g_session.freezeTick >= 0 && *reinterpret_cast<int *>(t + 0x54) == g_session.freezeTick) {
		if (!g_session.frozen) LogD("rejoin", "game stopped at tick %d", g_session.freezeTick);
		g_session.frozen = true;
		*reinterpret_cast<int *>(t + 0x20) = 0;
		return nullptr;
	}
	if (g_session.firstAdvance) {
		g_session.firstAdvance = false;
		*reinterpret_cast<int *>(t + 0x3f8) = 0;
		*reinterpret_cast<int *>(t + 0x3fc) = 0;
		if (char *game = static_cast<char *>(Game())) {
			char *outQueue = game + 0x1054 + 0xf4;
			reinterpret_cast<ThisOnly_t>(addr::kCmdQueueSetDtor)(outQueue);
			reinterpret_cast<ThisOnly_t>(addr::kCmdQueueSetCtor)(outQueue);
		}
		g_session.filterHost = true;
		void *r = g_orig.advance(tp);
		g_session.filterHost = false;
		if (g_session.restoreBits) {
			char *gate = *reinterpret_cast<char **>(t + 0x50);
			if (gate) {
				*reinterpret_cast<uint8_t *>(gate + 0x108) |= g_session.restoreBits;
				const int me = *reinterpret_cast<int *>(t + 0x4c);
				const int tick = *reinterpret_cast<int *>(t + 0x54);
				char *rec = Record(t, me, tick);
				if (!*reinterpret_cast<char **>(rec + 0x14)) {
					reinterpret_cast<ThisOnly_t>(addr::kBuildLocalTick)(t);
					int desc[8] = {};
					desc[5] = reinterpret_cast<int>(*reinterpret_cast<char **>(rec + 0x14));
					desc[6] = static_cast<int>(*reinterpret_cast<uint32_t *>(rec + 0x10));
					if (desc[5]) {
						reinterpret_cast<SendPacket_t>(addr::kSendPacket)(gate, nullptr, *reinterpret_cast<uint8_t *>(gate + 0x108), static_cast<unsigned>(tick), desc);
					}
					LogD("rejoin", "tick %d data prepared and sent (%d bytes)", tick, desc[6]);
				}
			}
			g_session.restoreBits = 0;
		}
		LogD("rejoin", "first step done (tick %d)", *reinterpret_cast<int *>(t + 0x54));
		return r;
	}
	return g_orig.advance(tp);
}
}
}

namespace rejoin_impl {
void *__fastcall HookAdvanceTick(void *tp)
{
	void *r = AdvanceTickInner(tp);
	NetTuneAfterAdvance(tp);
	LeagueSyncAfterAdvance(tp);
	return r;
}

void __fastcall HookAddCommand(void *pump, void *edx, int *cmd)
{
	if (g_session.filterHost && cmd) {
		char *player = reinterpret_cast<char *>(cmd[1]);
		if (player && *reinterpret_cast<int *>(player + 8) == g_blobs.wire.hostIndex) {
			return;
		}
	}
	g_orig.addCommand(pump, edx, cmd);
}
}

namespace rejoin_impl {
namespace {
void LogAiTrace(int from)
{
	const size_t first = (g_trace.ringNext + kRingSize - g_trace.ringCount) % kRingSize;
	std::map<uint16_t, const TraceRec *> last;
	unsigned lines = 0;
	for (size_t k = 0; k < g_trace.ringCount; k++) {
		const TraceRec &r = g_trace.ring[(first + k) % kRingSize];
		if (r.tick < from || (r.kind != 'U' && r.kind != 'O')) continue;
		if (r.kind == 'U') last[r.h] = &r;
		const bool show = r.kind == 'O' || (r.id >> 16) != 0 || r.vt != 0 || r.from != 0;
		if (!show || ++lines > 200) continue;
		if (r.kind == 'O') {
			LogD("rejoin", "AI %u tick %d t=%u: new command type %d priority %d (squad %u, target %u) from %08x", r.h, r.tick, r.t,
				static_cast<int>(r.id), static_cast<int>(r.vt), r.due & 0xffff, r.due >> 16, r.from);
		}
		else {
			LogD("rejoin", "AI %u tick %d t=%u: thinks with %u commands, %u active, busy %u, launches %u, state %08x", r.h, r.tick, r.t, r.id & 0xffff,
				r.id >> 16, r.from ? 1 : 0, r.vt, r.due);
		}
	}
	for (const auto &kv : last) {
		const TraceRec &r = *kv.second;
		LogD("rejoin", "AI %u: last time it thought tick %d t=%u, %u commands, state %08x", r.h, r.tick, r.t, r.id & 0xffff, r.due);
	}
}
}
}

namespace rejoin_impl {
void __stdcall HookDesync(unsigned tick, unsigned hash, unsigned compared)
{
	// Before the game's own report: it shows a box that holds the game thread until the player closes it.
	DesyncReport(tick, hash, compared);
	LeagueMatchDesync();
	if (WriteMoves(static_cast<int>(tick) - kMovesTicksWritten)) {
		LogI("match", "object moves of the last %d ticks in %s (send the problem report of every player)", kMovesTicksWritten, kMovesFile);
	}
	if (GetConfig().rejoinTrace) {
		if (g_trace.active && WriteTrace(static_cast<int>(tick) - kTraceTicksWritten)) {
			LogI("rejoin", "trace of the last %d ticks in %s", kTraceTicksWritten, kTraceFile);
		}
		WriteAiHistory();
	}
	if (g_trace.active) {
		LogD("rejoin", "what the AI did since tick %d:", static_cast<int>(tick) - kTraceTicksWritten);
		LogAiTrace(static_cast<int>(tick) - kTraceTicksWritten);
	}
	g_orig.desync(tick, hash, compared);
}

void __fastcall HookMsgExecute(void *cmd)
{
	const char *s = static_cast<const char *>(cmd) + 0xc;
	const unsigned size = *reinterpret_cast<const unsigned *>(s + 0x14);
	const unsigned cap = *reinterpret_cast<const unsigned *>(s + 0x18);
	const char *text = cap < 0x10 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	if (text && size > 4 && size < 32 && memcmp(text, kMagicPrefix, 4) == 0) {
		const int slot = atoi(std::string(text + 4, size - 4).c_str());
		// Every computer runs this on the same tick, whatever its role in the return, and so does a spectator of the broadcast: the AI of a
		// ranked 2v2's player goes here.
		NetDropAiRelease(slot);
		if (reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) return;
		char *t = Transport();
		if (!t) return;
		if (g_session.role == kNoRole && g_session.ignoreFreezeUntil && static_cast<int>(GetTickCount() - g_session.ignoreFreezeUntil) < 0) {
			g_session.ignoreFreezeUntil = 0;
			LogW("rejoin", "stop order for slot %d from an already cancelled return; not stopping", slot);
			return;
		}
		g_session.freezeTick = *reinterpret_cast<int *>(t + 0x54);
		g_session.frozen = false;
		RaiseQuitTimeouts();
		if (g_session.role == kNoRole) {
			g_session.role = kOther;
			g_session.slot = slot;
			g_session.deadline = GetTickCount() + 180000;
			UiToast("A player is rejoining the game: it pauses for a few seconds while they join.");
		}
		LogD("rejoin", "stop order received (slot %d); stopping at tick %d", slot, g_session.freezeTick);
		return;
	}
	if (text && size > 7 && CheatsHandleCommand(cmd, text, size)) return;
	if (text && LeagueVoteCommand(cmd, text, size)) return;
	g_orig.msgExec(cmd);
}


void __fastcall HookSetPosition(void *self, void *edx, uint32_t *pt, int a, int b)
{
	if (self && *reinterpret_cast<uint16_t *>(static_cast<char *>(self) + 8) < kFirstLocalHandle) NoteLocalTimerTouch("moved a shared object", _ReturnAddress());
	const uint32_t before = PositionsHash();
	g_orig.setPosition(self, edx, pt, a, b);
	if (!KeepLocalOutOfPositionsHash(self, before)) MovePush('M', self, before, _ReturnAddress());
	int tick;
	if (!g_trace.active || !Tracing(tick)) return;
	char *mgr = ThreadManager(0);
	TracePush({tick, 'M', *reinterpret_cast<uint16_t *>(static_cast<char *>(self) + 8), mgr ? *reinterpret_cast<uint32_t *>(mgr) : 0,
		*reinterpret_cast<uint32_t *>(static_cast<char *>(self) + 0x24), *reinterpret_cast<uint32_t *>(static_cast<char *>(self) + 0x28), 0,
		static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress())), *reinterpret_cast<uint32_t *>(addr::kSyncState + 8)});
}

void __fastcall HookRemoveFromMap(void *self)
{
	if (self && *reinterpret_cast<uint16_t *>(static_cast<char *>(self) + 8) < kFirstLocalHandle) NoteLocalTimerTouch("took a shared object off the map", _ReturnAddress());
	const uint32_t before = PositionsHash();
	g_orig.removeFromMap(self);
	if (!KeepLocalOutOfPositionsHash(self, before)) MovePush('R', self, before, _ReturnAddress());
}

}

namespace rejoin_impl {
namespace {
unsigned g_tileLineSteps = 0;
unsigned g_tileLineCuts = 0;
thread_local unsigned g_smoothDepth = 0;
thread_local unsigned g_smoothCalls = 0;
const unsigned kSmoothBudget = 400000;
const DWORD kSmoothAbort = 0xE0494D31;
}
}

namespace rejoin_impl {
void __cdecl HookSmoothPath(int path, void *out, int flag)
{
	if (++g_smoothDepth == 1) g_smoothCalls = 0;
	__try {
		g_orig.smoothPath(path, out, flag);
	}
	__except (GetExceptionCode() == kSmoothAbort ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		static int cuts = 0;
		if (cuts++ < 5) LogW("game", "paths: smoothing of a path stopped after %u tile lines (the game would have hung)", kSmoothBudget);
	}
	g_smoothDepth--;
}

void __cdecl HookTraceTileLine(int x1, int y1, int x2, int y2, void *visitor)
{
	if (g_smoothDepth && ++g_smoothCalls > kSmoothBudget) RaiseException(kSmoothAbort, EXCEPTION_NONCONTINUABLE, 0, nullptr);
	g_tileLineSteps = 0;
	g_orig.traceTileLine(x1, y1, x2, y2, visitor);
}

int __fastcall HookTileLineVisit(void *self, void *edx, int x, int y)
{
	if (++g_tileLineSteps > 20000) {
		if (g_tileLineSteps == 20001 && g_tileLineCuts++ < 5) LogD("game", "paths: tile line of more than 20000 steps cut (the game would have hung)");
		*reinterpret_cast<int *>(static_cast<char *>(self) + 8) = y;
		*reinterpret_cast<int *>(static_cast<char *>(self) + 4) = x;
		*reinterpret_cast<int *>(self) = 0;
		return 0;
	}
	return g_orig.tileLineVisit(self, edx, x, y);
}

bool HookAt(uintptr_t addr, void *detour, void **orig)
{
	return HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK && HookEnable(reinterpret_cast<void *>(addr)) == MH_OK;
}
}
