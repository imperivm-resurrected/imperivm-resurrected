// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <set>
#include "MinHook.h"
#include "core/log.h"
#include "core/syncdiag.h"
#include "mods/modoptions.h"
#include "net/nettune.h"
#include "net/steamnet.h"
#include "addr/net.h"

#include "net/rejoin_internal.h"

using namespace rejoin_impl;

namespace rejoin_impl {
namespace {
const char kDesyncFile[] = "logs/imperivm/rejoin/desync.txt";
const size_t kSnapshotsKept = 64;

typedef uint32_t(__fastcall *GetHash_t)(void *state);
GetHash_t g_origGetHash = nullptr;

std::mutex g_syncMx;
std::vector<SyncSnapshot> g_ring;
size_t g_ringNext = 0;
// This computer's own desync (the tick and the two fingerprints the game compared), and who it already answered.
struct Seen {
	bool active = false;
	int32_t tick = 0;
	uint32_t hash = 0, compared = 0;
	std::set<uint64_t> answered;
	// desync.txt was started in this game: what comes later is added to it.
	bool fileStarted = false;
};
Seen g_seen;
std::mutex g_fileMx;
unsigned g_localTouches = 0;

// CVXSyncState::GetHash, called once from AdvanceTick after it read the others' data and before the tick's commands run: what it hashes
// is what goes to the others with the next tick and what the desync check compares. The tick read now is the one a desync reports.
uint32_t __fastcall HookGetHash(void *state)
{
	SyncSnapshot s;
	const uint32_t *counters = reinterpret_cast<const uint32_t *>(static_cast<char *>(state) + 8);
	for (int i = 0; i < 8; i++) s.counters[i] = counters[i];
	if (char *t = Transport()) {
		s.tick = *reinterpret_cast<int *>(t + 0x54);
		s.turn = *reinterpret_cast<uint32_t *>(t + 0x70);
		s.delay = *reinterpret_cast<uint32_t *>(t + 0x74);
		const char *gate = *reinterpret_cast<char **>(t + 0x50);
		const int me = *reinterpret_cast<int *>(t + 0x4c);
		s.mask = (gate ? *reinterpret_cast<const uint8_t *>(gate + 0x108) : 0u) | (me >= 0 && me < 8 ? 1u << me : 0u);
	}
	if (const char *game = static_cast<const char *>(Game())) {
		s.speed = *reinterpret_cast<const uint32_t *>(game + 0x1244);
		s.clock = *reinterpret_cast<const uint32_t *>(game + 0x1260);
	}
	{
		std::lock_guard<std::mutex> lock(g_syncMx);
		if (g_ring.size() < kSnapshotsKept) g_ring.push_back(s);
		else g_ring[g_ringNext] = s;
		g_ringNext = (g_ringNext + 1) % kSnapshotsKept;
	}
	return g_origGetHash(state);
}

std::vector<SyncSnapshot> Snapshots()
{
	std::lock_guard<std::mutex> lock(g_syncMx);
	if (g_ring.size() < kSnapshotsKept) return g_ring;
	std::vector<SyncSnapshot> out(g_ring.begin() + static_cast<std::ptrdiff_t>(g_ringNext), g_ring.end());
	out.insert(out.end(), g_ring.begin(), g_ring.begin() + static_cast<std::ptrdiff_t>(g_ringNext));
	return out;
}

std::string Counters(const SyncSnapshot &s)
{
	std::string out;
	for (int i = 0; i < 8; i++) {
		char b[40];
		sprintf_s(b, "%s%s %08x", i ? " " : "", SyncCounterName(i), s.counters[i]);
		out += b;
	}
	char b[120];
	sprintf_s(b, "; turn %u ms, delay %u ms, players %02x, speed %u, clock %u", s.turn, s.delay, s.mask, s.speed, s.clock);
	return out + b;
}

void WriteDesyncFile(const std::string &text, bool fresh)
{
	std::lock_guard<std::mutex> lock(g_fileMx);
	EnsureLogDir();
	FILE *f = _fsopen(DiskPath(kDesyncFile).c_str(), fresh ? "wb" : "ab", _SH_DENYWR);
	if (!f) return;
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

std::string PeerName(uint64_t peer)
{
	for (const OnlineMember &m : SteamCurrentRoom().members) {
		if (m.id == peer) return m.name + " (" + std::to_string(peer) + ")";
	}
	return std::to_string(peer);
}

std::string SnapshotLines(const std::vector<SyncSnapshot> &list)
{
	std::string out = "# tick slots threads pathfinder exploration scriptstate netcmds extra ai turn delay players speed clock\n";
	for (const SyncSnapshot &s : list) out += SyncSnapshotLine(s) + "\n";
	return out;
}
}

bool InstallDesyncSnapshots()
{
	return HookAt(addr::kSyncGetHash, reinterpret_cast<void *>(&HookGetHash), reinterpret_cast<void **>(&g_origGetHash));
}

void DesyncSnapshotsReset()
{
	std::lock_guard<std::mutex> lock(g_syncMx);
	g_ring.clear();
	g_ringNext = 0;
	g_seen = Seen();
	g_localTouches = 0;
}

void DesyncReport(unsigned tick, unsigned hash, unsigned compared)
{
	const std::vector<SyncSnapshot> mine = Snapshots();
	const SyncSnapshot *at = SyncFind(mine, static_cast<int32_t>(tick));
	LogE("match", "DESYNC at tick %u: my fingerprint %08x, the other's %08x. Fingerprints then: %s", tick, hash, compared,
		at ? Counters(*at).c_str() : "(not kept)");
	if (at && at->Hash() != hash) LogW("match", "desync: the counters kept for tick %u make %08x, not %08x", tick, at->Hash(), hash);
	const std::string tune = NetTuneDescribe(), signature = NetSimSignature();
	LogI("match", "desync: network %s; simulation %s; rules %x", tune.c_str(), signature.c_str(), ModRulesActive());
	std::string text = "[DESYNC]\ntick " + std::to_string(tick) + "\n";
	char line[96];
	sprintf_s(line, "my fingerprint %08x, the other's %08x\n", hash, compared);
	text += line;
	text += "network: " + tune + "\nsimulation: " + signature + "\n";
	sprintf_s(line, "rules: %x\n", ModRulesActive());
	text += line;
	text += "\n[MINE]\n" + SnapshotLines(mine);
	text += "\n[TRANSPORT] the game's last lines of its network\n";
	for (const std::string &l : NetTuneRecentLines()) text += l + "\n";
	bool fresh;
	{
		std::lock_guard<std::mutex> lock(g_syncMx);
		fresh = !g_seen.fileStarted;
		g_seen.fileStarted = true;
		g_seen.active = true;
		g_seen.tick = static_cast<int32_t>(tick);
		g_seen.hash = hash;
		g_seen.compared = compared;
	}
	WriteDesyncFile(fresh ? text : "\n" + text, fresh);
	const std::string message = SyncEncode(false, static_cast<int32_t>(tick), mine);
	const std::vector<uint64_t> others = OtherMembers();
	for (uint64_t m : others) SteamRejoinSend(m, kRjDesync, message);
	LogI("match", "desync: my fingerprints went to the %u other players; theirs go to this log and to %s as they come", static_cast<unsigned>(others.size()),
		kDesyncFile);
}

void DesyncOnPeerMessage(uint64_t peer, const std::string &data)
{
	bool reply = false;
	int32_t peerTick = 0;
	std::vector<SyncSnapshot> theirs;
	if (!SyncDecode(data, reply, peerTick, theirs)) {
		LogW("match", "desync: unreadable fingerprints from %llu", static_cast<unsigned long long>(peer));
		return;
	}
	const std::vector<SyncSnapshot> mine = Snapshots();
	Seen seen;
	bool answer = false, fresh = false;
	{
		std::lock_guard<std::mutex> lock(g_syncMx);
		answer = !reply && !mine.empty() && g_seen.answered.insert(peer).second;
		fresh = !g_seen.fileStarted;
		g_seen.fileStarted = true;
		seen = g_seen;
	}
	// Whoever sees it first asks; the others answer once, whether or not their own game saw it yet.
	if (answer) SteamRejoinSend(peer, kRjDesync, SyncEncode(true, seen.active ? seen.tick : peerTick, mine));
	const std::string name = PeerName(peer);
	const int32_t tick = seen.active ? seen.tick : peerTick;
	const SyncSnapshot *their = SyncFind(theirs, tick);
	const SyncSnapshot *own = SyncFind(mine, tick);
	if (seen.active && !their) their = SyncFindHash(theirs, seen.compared);
	std::string differs = their && own ? SyncSnapshotDiff(*own, *their) : std::string();
	LogE("match", "DESYNC: %s's fingerprints at tick %d: %s; they differ from mine in: %s", name.c_str(), tick, their ? Counters(*their).c_str() : "(not sent)",
		their && own ? (differs.empty() ? "nothing" : differs.c_str()) : "(not comparable)");
	int32_t first = 0;
	std::string firstWhat;
	if (SyncFirstDifference(mine, theirs, first, firstWhat)) LogE("match", "DESYNC: first difference with %s at tick %d: %s", name.c_str(), first, firstWhat.c_str());
	std::string text = "\n[PEER " + name + "]" + (reply ? " (answer)" : "") + "\ntheir desync tick " + std::to_string(peerTick) + "\n";
	text += "differs from mine at tick " + std::to_string(tick) + ": " + (their && own ? (differs.empty() ? "nothing" : differs) : "(not comparable)") + "\n";
	if (!firstWhat.empty()) text += "first difference at tick " + std::to_string(first) + ": " + firstWhat + "\n";
	text += SnapshotLines(theirs);
	// Before this computer's own desync (it may never come, or come later) the file still has to say whose snapshots these are.
	if (fresh) text = "[MINE] (this game had not seen the desync yet)\n" + SnapshotLines(mine) + text;
	WriteDesyncFile(text, fresh);
}

bool LocalTimerRunning(uint16_t &handle, uint32_t &vtable)
{
	char *mgr = ThreadManager(0);
	if (!mgr) return false;
	__try {
		const char *thread = *reinterpret_cast<char **>(mgr + 0x1c);
		const char *owner = thread ? *reinterpret_cast<char *const *>(thread + 0x14) : nullptr;
		if (!owner) return false;
		handle = *reinterpret_cast<const uint16_t *>(owner + 8);
		vtable = *reinterpret_cast<const uint32_t *>(owner);
		return handle >= kFirstLocalHandle;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void NoteLocalTimerTouch(const char *what, void *from)
{
	uint16_t handle = 0;
	uint32_t vtable = 0;
	if (!LocalTimerRunning(handle, vtable) || !InOnlineGame()) return;
	static std::set<std::pair<uint32_t, uintptr_t>> seen;
	if (!seen.insert({vtable, reinterpret_cast<uintptr_t>(from)}).second) return;
	if (++g_localTouches > 20) return;
	char *t = Transport();
	LogW("match", "desync risk: a shared timer of a local object (handle %u, class vtable %08x) %s at tick %d (caller %p): a local object exists on "
		"this computer only", handle, vtable, what, t ? *reinterpret_cast<int *>(t + 0x54) : -1, from);
}
}

void RejoinNetworkStarted()
{
	DesyncSnapshotsReset();
}
