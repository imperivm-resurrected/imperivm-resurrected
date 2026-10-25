// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include "MinHook.h"
#include "core/guard.h"
#include "net/steamnet.h"
#include "addr/net.h"

#include "net/rejoin_internal.h"

using namespace rejoin_impl;

namespace rejoin_impl {
char *ThreadManager(int m)
{
	char *game = static_cast<char *>(Game());
	return game ? *reinterpret_cast<char **>(game + kGameThreadManagers[m]) : nullptr;
}

}

namespace rejoin_impl {
namespace {
const int kNativeTicksWritten = 6;
}
}

namespace rejoin_impl {
TraceState g_trace;
}

namespace rejoin_impl {
namespace {
struct MoveRec {
	int tick;
	uint16_t handle;
	char kind;
	uint32_t vt, x, y, from, hash;
};
const size_t kMovesRing = 1 << 17;
std::vector<MoveRec> g_moves;
size_t g_movesNext = 0, g_movesCount = 0;
int g_movesLastTick = -1;
}

uint32_t PositionsHash() { return *reinterpret_cast<uint32_t *>(addr::kSyncState + 8); }

bool KeepLocalOutOfPositionsHash(void *obj, uint32_t before)
{
	const char *o = static_cast<const char *>(obj);
	const uint16_t handle = *reinterpret_cast<const uint16_t *>(o + 8);
	if (handle < kFirstLocalHandle || PositionsHash() == before) return false;
	*reinterpret_cast<uint32_t *>(addr::kSyncState + 8) = before;
	static bool logged = false;
	if (!logged) {
		logged = true;
		LogI("match", "positions fingerprint: a local object (handle %u, class vtable %08x) moved and was left out, as the game does with its own effects", handle,
			*reinterpret_cast<const uint32_t *>(o));
	}
	return true;
}

uint32_t *ThreadsChecksum(void *owner)
{
	char *mgr = owner ? *reinterpret_cast<char **>(static_cast<char *>(owner) + 0x18) : nullptr;
	return mgr ? *reinterpret_cast<uint32_t **>(mgr + 0x28) : nullptr;
}

bool KeepLocalOutOfThreadsHash(void *owner, uint32_t *checksum, uint32_t before)
{
	if (!checksum || *checksum == before) return false;
	const char *o = static_cast<const char *>(owner);
	const uint16_t handle = *reinterpret_cast<const uint16_t *>(o + 8);
	if (handle < kFirstLocalHandle) return false;
	*checksum = before;
	static bool logged = false;
	if (!logged) {
		logged = true;
		LogI("match", "threads fingerprint: a local object (handle %u, class vtable %08x) used the shared timers and was left out, as the game does with its own effects",
			handle, *reinterpret_cast<const uint32_t *>(o));
	}
	return true;
}

void RecordMove(int tick, uint16_t handle, char kind, uint32_t vt, uint32_t x, uint32_t y, void *from, uint32_t after)
{
	if (g_moves.size() != kMovesRing) g_moves.assign(kMovesRing, MoveRec{});
	if (tick < g_movesLastTick) g_movesNext = g_movesCount = 0;
	g_movesLastTick = tick;
	g_moves[g_movesNext] = {tick, handle, kind, vt, x, y, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(from)), after};
	g_movesNext = (g_movesNext + 1) % kMovesRing;
	if (g_movesCount < kMovesRing) g_movesCount++;
}

void TimerPush(char kind, void *owner, uint32_t id, uint32_t due, uint32_t before, void *from)
{
	uint32_t *checksum = ThreadsChecksum(owner);
	if (!checksum || *checksum == before || !InOnlineGame()) return;
	char *t = Transport();
	const char *o = static_cast<const char *>(owner);
	RecordMove(t ? *reinterpret_cast<int *>(t + 0x54) : -1, *reinterpret_cast<const uint16_t *>(o + 8), kind, *reinterpret_cast<const uint32_t *>(o), id, due, from,
		*checksum);
}

void MovePush(char kind, void *obj, uint32_t before, void *from)
{
	const uint32_t after = PositionsHash();
	if (after == before || !InOnlineGame()) return;
	char *t = Transport();
	const char *o = static_cast<const char *>(obj);
	RecordMove(t ? *reinterpret_cast<int *>(t + 0x54) : -1, *reinterpret_cast<const uint16_t *>(o + 8), kind, *reinterpret_cast<const uint32_t *>(o),
		*reinterpret_cast<const uint32_t *>(o + 0x24), *reinterpret_cast<const uint32_t *>(o + 0x28), from, after);
}

bool WriteMoves(int from)
{
	if (!g_movesCount) return false;
	EnsureLogDir();
	FILE *f = _fsopen(DiskPath(kMovesFile).c_str(), "wb", _SH_DENYWR);
	if (!f) return false;
	fprintf(f, "# objects that changed the positions (slots) or timers (threads) fingerprint since tick %d\n", from);
	fprintf(f, "# tick kind handle class-vtable x y caller fingerprint-after; kinds: M moved, R removed (x y = position); timers S start, C reschedule, P stop, D delete, T time (x = timer id, y = time)\n");
	const size_t first = (g_movesNext + kMovesRing - g_movesCount) % kMovesRing;
	for (size_t k = 0; k < g_movesCount; k++) {
		const MoveRec &r = g_moves[(first + k) % kMovesRing];
		if (r.tick < from) continue;
		fprintf(f, "%d %c %u %08x %u %u %08x %08x\n", r.tick, r.kind, r.handle, r.vt, r.x, r.y, r.from, r.hash);
	}
	fclose(f);
	return true;
}

uint32_t ThreadsHash() { return *reinterpret_cast<uint32_t *>(addr::kSyncState + 0xc); }

void TraceStart(int)
{
	PatchNatives();
	if (g_trace.ring.size() != kRingSize) g_trace.ring.assign(kRingSize, TraceRec{});
	g_trace.ringNext = g_trace.ringCount = 0;
	g_trace.active = true;
	g_trace.lastTick = -1;
}

void TracePush(const TraceRec &r)
{
	g_trace.ring[g_trace.ringNext] = r;
	g_trace.ringNext = (g_trace.ringNext + 1) % kRingSize;
	if (g_trace.ringCount < kRingSize) g_trace.ringCount++;
}

bool Tracing(int &tick)
{
	if (!g_trace.active) return false;
	if (!Game()) {
		g_trace.active = false;
		return false;
	}
	char *t = Transport();
	tick = t ? *reinterpret_cast<int *>(t + 0x54) : -1;
	return true;
}

bool WriteTrace(int from)
{
	EnsureLogDir();
	FILE *f = _fsopen(DiskPath(kTraceFile).c_str(), "wb", _SH_DENYWR);
	if (!f) return false;
	const size_t first = (g_trace.ringNext + kRingSize - g_trace.ringCount) % kRingSize;
	const int lastTick = g_trace.ringCount ? g_trace.ring[(g_trace.ringNext + kRingSize - 1) % kRingSize].tick : 0;
	for (size_t k = 0; k < g_trace.ringCount; k++) {
		const TraceRec &r = g_trace.ring[(first + k) % kRingSize];
		if (r.tick < from) continue;
		switch (r.kind) {
		case 'N':
			if (r.tick < lastTick - kNativeTicksWritten) break;
			fprintf(f, "%d N t=%u ai=%u script=%u %s -> %d (stack %d)\n", r.tick, r.t, r.id >> 16, r.id & 0xffff, NativeName(r.h).c_str(),
				static_cast<int>(r.due), static_cast<int>(r.vt));
			break;
		case 'T':
			fprintf(f, "== tick %d, threads %08x\n", r.tick, r.hash);
			break;
		case 'G':
			fprintf(f, "-- continue (tick %d, threads %08x)\n", r.tick, r.hash);
			break;
		case 'A':
			fprintf(f, "%d A t=%u [%d,%d]=%d from=%08x seed=%08x\n", r.tick, r.t, static_cast<int>(r.due), static_cast<int>(r.vt),
				static_cast<int>(r.id), r.from, r.hash);
			break;
		case 'U':
			fprintf(f, "%d U t=%u ai=%u commands=%u active=%u busy=%u launches=%u state=%08x\n", r.tick, r.t, r.h, r.id & 0xffff, r.id >> 16,
				r.from ? 1 : 0, r.vt, r.due);
			break;
		case 'O':
			fprintf(f, "%d O t=%u ai=%u type=%d prio=%d a=%u b=%u from=%08x\n", r.tick, r.t, r.h, static_cast<int>(r.id), static_cast<int>(r.vt),
				r.due & 0xffff, r.due >> 16, r.from);
			break;
		default:
			fprintf(f, "%d %c t=%u h=%u vt=%08x id=%u due=%u from=%08x hash=%08x\n", r.tick, r.kind, r.t, r.h, r.vt, r.id, r.due, r.from,
				r.hash);
		}
	}
	WriteAiState(f);
	fclose(f);
	return true;
}

}

namespace rejoin_impl {
namespace {
void TraceThreadEvent(char kind, void *owner, uint32_t id, uint32_t due, void *from)
{
	int tick;
	if (!owner || !Tracing(tick)) return;
	char *mgr = *reinterpret_cast<char **>(static_cast<char *>(owner) + 0x18);
	if (!mgr || mgr != ThreadManager(0)) return;
	TracePush({tick, kind, *reinterpret_cast<uint16_t *>(static_cast<char *>(owner) + 8), *reinterpret_cast<uint32_t *>(mgr),
		*static_cast<uint32_t *>(owner), id, due, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(from)), ThreadsHash()});
}
}
}

namespace rejoin_impl {
void *__fastcall HookStartThread(void *owner, void *edx, uint32_t id, uint32_t due)
{
	TraceThreadEvent('S', owner, id, due, _ReturnAddress());
	uint32_t *checksum = ThreadsChecksum(owner);
	const uint32_t before = checksum ? *checksum : 0;
	void *r = g_orig.startThread(owner, edx, id, due);
	if (!KeepLocalOutOfThreadsHash(owner, checksum, before)) TimerPush('S', owner, id, due, before, _ReturnAddress());
	return r;
}


void *__fastcall HookRescheduleThread(void *owner, void *edx, uint32_t *thread, uint32_t due)
{
	TraceThreadEvent('R', owner, thread ? thread[4] : 0, due, _ReturnAddress());
	uint32_t *checksum = ThreadsChecksum(owner);
	const uint32_t before = checksum ? *checksum : 0;
	void *r = g_orig.reschedule(owner, edx, thread, due);
	if (!KeepLocalOutOfThreadsHash(owner, checksum, before)) TimerPush('C', owner, thread ? thread[4] : 0, due, before, _ReturnAddress());
	return r;
}

void *__fastcall HookStopThread(void *owner, void *edx, uint32_t id)
{
	TraceThreadEvent('P', owner, id, 0xffffffff, _ReturnAddress());
	uint32_t *checksum = ThreadsChecksum(owner);
	const uint32_t before = checksum ? *checksum : 0;
	void *r = g_orig.stopThread(owner, edx, id);
	if (!KeepLocalOutOfThreadsHash(owner, checksum, before)) TimerPush('P', owner, id, 0xffffffff, before, _ReturnAddress());
	return r;
}

void __fastcall HookDeleteThread(void *owner, void *edx, uint32_t id)
{
	uint32_t *checksum = ThreadsChecksum(owner);
	const uint32_t before = checksum ? *checksum : 0;
	g_orig.deleteThread(owner, edx, id);
	if (!KeepLocalOutOfThreadsHash(owner, checksum, before)) TimerPush('D', owner, id, 0xffffffff, before, _ReturnAddress());
}

int __fastcall HookSetThreadTime(void *owner, void *edx, uint32_t id, uint32_t due)
{
	uint32_t *checksum = ThreadsChecksum(owner);
	const uint32_t before = checksum ? *checksum : 0;
	const int r = g_orig.setThreadTime(owner, edx, id, due);
	if (!KeepLocalOutOfThreadsHash(owner, checksum, before)) TimerPush('T', owner, id, due, before, _ReturnAddress());
	return r;
}


int __fastcall HookRandomRange(void *rng, void *edx, int lo, int hi)
{
	const uint32_t seed = *reinterpret_cast<uint32_t *>(static_cast<char *>(rng) + 4);
	const int r = g_orig.randomRange(rng, edx, lo, hi);
	int tick;
	char *game = static_cast<char *>(Game());
	if (game && rng == game + 0x12a0 && Tracing(tick)) {
		char *mgr = ThreadManager(0);
		TracePush({tick, 'A', 0, mgr ? *reinterpret_cast<uint32_t *>(mgr) : 0, static_cast<uint32_t>(hi), static_cast<uint32_t>(r),
			static_cast<uint32_t>(lo), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress())), seed});
	}
	return r;
}
}
