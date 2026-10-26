// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <vector>
#include <map>
#include <atomic>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "hooks/audio.h"
#include "game/civs.h"
#include "game/controls.h"
#include "net/spectator.h"
#include "core/log.h"
#include "mods/savelib.h"
#include "net/sharemsg.h"
#include "net/steamnet.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/uiport.h"
#include "core/version.h"

namespace {
const unsigned kMagic = 0x76787367;

const size_t kBucketSize = 0xf8;
const size_t kBucketHashes = 0xc8;
const int kBucketHashCount = 8;
const size_t kBucketTick = 0xf0;
const size_t kGameTick = 0x1258;
const size_t kGameSpeed = 0x1244;
const size_t kReplayData = 0x44e4;
const size_t kReplayEnd = 0x44e8;
const size_t kBucketsVec = 0x44ec, kBucketsBegin = 0x44f0, kBucketsEnd = 0x44f4;
const size_t kWindowFrom = 0x24, kWindowTo = 0x28;

typedef int(__cdecl *IsDemo_t)();
typedef void *(__cdecl *OperatorNew_t)(unsigned size);
typedef void *(__fastcall *Ctor1_t)(void *obj, void *edx, int arg);
typedef void(__fastcall *ThisOnly_t)(void *obj);
typedef void(__fastcall *ThisArg_t)(void *obj, void *edx, void *arg);
typedef void(__fastcall *ThisInt_t)(void *obj, void *edx, int arg);
typedef int(__fastcall *StreamOpen_t)(void *stream, void *edx, const char *path);
typedef int(__fastcall *StreamInt_t)(void *stream, void *edx, void *value, const char *name);
typedef void(__fastcall *RunTicks_t)(void *timer, void *edx, unsigned target);
typedef void *(__fastcall *Assign_t)(void *self, void *edx, const char *text, unsigned len);

RunTicks_t g_origRunTicks = nullptr;
char g_ini[MAX_PATH];

void **Vt(void *obj) { return *reinterpret_cast<void ***>(obj); }
uint8_t *Game() { return *reinterpret_cast<uint8_t **>(addr::kGame); }
template <class T> T &At(void *p, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
bool IsDemo() { return reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)() != 0; }

bool GamePaused(uint8_t *game)
{
	__try {
		return reinterpret_cast<int(__fastcall *)(void *)>(Vt(game)[22])(game) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void QuitToMenu()
{
	reinterpret_cast<void(__cdecl *)()>(addr::kRequestQuitToMenu)();
}

size_t BucketCount(uint8_t *game)
{
	uint8_t *b = At<uint8_t *>(game, kBucketsBegin), *e = At<uint8_t *>(game, kBucketsEnd);
	return b ? static_cast<size_t>(e - b) / kBucketSize : 0;
}

uint32_t BucketTickAt(uint8_t *game, size_t i) { return At<uint32_t>(At<uint8_t *>(game, kBucketsBegin) + i * kBucketSize, kBucketTick); }

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\'));
}

std::string Profile()
{
	static std::string cached;
	static DWORD at = 0;
	if (!at || GetTickCount() - at > 5000) {
		cached = SaveLibActiveProfile();
		at = GetTickCount() | 1;
	}
	return cached;
}

std::string ScratchVfs(const std::string &name)
{
	const std::string profile = Profile();
	return "Profiles/" + (profile.empty() ? std::string() : profile + "/") + "Spectator/" + name;
}

std::string DiskOf(const std::string &vfs)
{
	std::string d = GameDir() + "\\" + vfs;
	std::replace(d.begin(), d.end(), '/', '\\');
	return d;
}

void EnsureScratchDir()
{
	const std::string profile = Profile();
	std::string dir = GameDir() + "\\Profiles";
	CreateDirectoryA(dir.c_str(), nullptr);
	if (!profile.empty()) {
		dir += "\\" + profile;
		CreateDirectoryA(dir.c_str(), nullptr);
	}
	dir += "\\Spectator";
	CreateDirectoryA(dir.c_str(), nullptr);
}

bool ReadAll(const std::string &path, std::string &out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(size > 0 ? static_cast<size_t>(size) : 0);
	const size_t got = out.empty() ? 0 : fread(&out[0], 1, out.size(), f);
	fclose(f);
	return got == out.size();
}

bool WriteAll(const std::string &path, const std::string &data)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
	const size_t put = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), f);
	fclose(f);
	return put == data.size();
}

void Put32(std::string &s, uint32_t v) { s.append(reinterpret_cast<const char *>(&v), 4); }
bool Get32(const std::string &s, size_t &pos, uint32_t &v)
{
	if (pos + 4 > s.size()) return false;
	memcpy(&v, s.data() + pos, 4);
	pos += 4;
	return true;
}

bool StreamWrite(const char *path, uint8_t *game, size_t first, size_t last, bool header)
{
	void *stream = nullptr;
	bool ok = false;
	const unsigned oldVersion = *reinterpret_cast<unsigned *>(addr::kPersistVersion);
	__try {
		uint32_t fake[7];
		memcpy(fake, game + kReplayData, sizeof(fake));
		uint8_t *vec = At<uint8_t *>(game, kBucketsBegin);
		fake[3] = reinterpret_cast<uint32_t>(vec ? vec + first * kBucketSize : nullptr);
		fake[4] = reinterpret_cast<uint32_t>(vec ? vec + last * kBucketSize : nullptr);
		fake[5] = fake[4];
		*reinterpret_cast<unsigned *>(addr::kPersistVersion) = *reinterpret_cast<unsigned *>(addr::kPersistVersionSrc);
		stream = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x3c);
		reinterpret_cast<Ctor1_t>(addr::kSaveStreamCtor)(stream, nullptr, 1);
		reinterpret_cast<StreamOpen_t>(Vt(stream)[0x8 / 4])(stream, nullptr, path);
		if (header) {
			unsigned magic = kMagic;
			reinterpret_cast<StreamInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, &magic, "magic");
			reinterpret_cast<StreamInt_t>(Vt(stream)[0x44 / 4])(stream, nullptr, reinterpret_cast<void *>(addr::kNetProtocol), "build");
			int type = 4;
			reinterpret_cast<StreamInt_t>(Vt(stream)[0x4c / 4])(stream, nullptr, &type, "gametype");
			reinterpret_cast<ThisArg_t>(addr::kReplaySettings)(fake, nullptr, stream);
		}
		reinterpret_cast<ThisArg_t>(addr::kReplaySerialize)(fake, nullptr, stream);
		reinterpret_cast<ThisOnly_t>(Vt(stream)[0xc / 4])(stream);
		ok = true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	if (stream) {
		__try {
			reinterpret_cast<ThisInt_t>(Vt(stream)[0])(stream, nullptr, 1);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	*reinterpret_cast<unsigned *>(addr::kPersistVersion) = oldVersion;
	return ok;
}

int StreamAppend(const char *path, uint8_t *game, uint32_t *ticks, int maxTicks)
{
	void *stream = nullptr;
	uint32_t tmp[7];
	int count = -1;
	const unsigned oldVersion = *reinterpret_cast<unsigned *>(addr::kPersistVersion);
	reinterpret_cast<ThisOnly_t>(addr::kReplayCtor)(tmp);
	__try {
		*reinterpret_cast<unsigned *>(addr::kPersistVersion) = *reinterpret_cast<unsigned *>(addr::kPersistVersionSrc);
		stream = reinterpret_cast<OperatorNew_t>(addr::kOperatorNew)(0x40);
		reinterpret_cast<Ctor1_t>(addr::kStreamLoadCtor)(stream, nullptr, 1);
		reinterpret_cast<StreamOpen_t>(Vt(stream)[0x8 / 4])(stream, nullptr, path);
		reinterpret_cast<ThisArg_t>(addr::kReplaySerialize)(tmp, nullptr, stream);
		const int n = tmp[3] ? static_cast<int>((tmp[4] - tmp[3]) / kBucketSize) : 0;
		for (int i = 0; i < n; i++) {
			uint8_t *bucket = reinterpret_cast<uint8_t *>(tmp[3]) + i * kBucketSize;
			if (i < maxTicks) ticks[i] = *reinterpret_cast<uint32_t *>(bucket + kBucketTick);
			if (game) reinterpret_cast<ThisArg_t>(addr::kBucketPush)(game + kBucketsVec, nullptr, bucket);
		}
		count = n;
		reinterpret_cast<ThisOnly_t>(Vt(stream)[0xc / 4])(stream);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		count = -1;
	}
	if (stream) {
		__try {
			reinterpret_cast<ThisInt_t>(Vt(stream)[0])(stream, nullptr, 1);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
	__try {
		reinterpret_cast<ThisOnly_t>(addr::kReplayDtor)(tmp);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	*reinterpret_cast<unsigned *>(addr::kPersistVersion) = oldVersion;
	return count;
}

enum : uint8_t {
	kSpHello = 40,
	kSpOk = 41,
	kSpNo = 42,
	kSpSnapBegin = 43,
	kSpSnapData = 44,
	kSpSnapEnd = 45,
	kSpTicks = 46,
	kSpEnd = 47,
	kSpBye = 48,
	kSpWait = 49,
	kSpResend = 50,
};
static_assert(kSpectatorMessages.Has(kSpHello) && kSpectatorMessages.Has(kSpResend), "spectator messages out of their range");
const DWORD kResendWaitMs = 6000;
// The cover over a catch-up only gives up when the game stops getting closer to live: a long game
// on a slow computer can take minutes and must not be shown fast-forwarding.
const DWORD kCoverStallMs = 30000;
const DWORD kCoverMaxMs = 10 * 60 * 1000;
const size_t kChunk = 48 * 1024;
const size_t kCaptureChunk = 128;
const size_t kTicksMsgMax = 192 * 1024;
const double kStartBufferMs = 2000;

struct Msg {
	uint64_t peer;
	uint8_t type;
	std::string data;
};
std::mutex g_mx;
std::deque<Msg> g_queue;

void Send(uint64_t peer, uint8_t type, const std::string &data = std::string()) { SteamSpectatorSend(peer, type, data); }

struct Rec {
	uint32_t first = 0, count = 0, tick = 0;
	std::string bytes;
};
struct Client {
	uint64_t id = 0;
	int state = 0;
	size_t next = 0;
	uint32_t nextBucket = 0;
	uint32_t sentV = 0;
	DWORD lastSend = 0, since = 0, lastWait = 0;
	bool seen = false;
};
struct Host {
	bool armed = false;
	uint8_t *game = nullptr;
	std::vector<Rec> recs;
	size_t captured = 0;
	std::map<uint64_t, Client> clients;
	uint32_t lastClock = 0;
	DWORD armedAt = 0;
} g_h;
std::atomic<int> g_clientCount{0};

bool BuildFile(uint8_t *game, size_t first, size_t last, bool header, std::string &out)
{
	EnsureScratchDir();
	const std::string vfs = ScratchVfs("host.tmp");
	DeleteFileA(DiskOf(vfs).c_str());
	if (!StreamWrite(vfs.c_str(), game, first, last, header)) return false;
	const bool ok = ReadAll(DiskOf(vfs), out) && !out.empty();
	DeleteFileA(DiskOf(vfs).c_str());
	return ok;
}

void HostSendSnapshot(Client &c, uint8_t *game, uint32_t V, uint32_t clock)
{
	size_t nr = 0, nb = 0;
	while (nr < g_h.recs.size() && g_h.recs[nr].tick <= V) nb += g_h.recs[nr++].count;
	std::string file;
	if (!BuildFile(game, 0, nb, true, file)) {
		LogW("spectator", "could not prepare the header for %llu", c.id);
		Send(c.id, kSpNo, "This game cannot be broadcast.");
		c.state = -1;
		return;
	}
	std::string begin;
	Put32(begin, static_cast<uint32_t>(file.size()));
	Put32(begin, static_cast<uint32_t>(nb));
	Put32(begin, V);
	Put32(begin, clock);
	Send(c.id, kSpSnapBegin, begin);
	for (size_t p = 0; p < file.size(); p += kChunk) Send(c.id, kSpSnapData, file.substr(p, kChunk));
	Send(c.id, kSpSnapEnd);
	c.next = nr;
	c.nextBucket = static_cast<uint32_t>(nb);
	c.sentV = V;
	c.state = 2;
	c.lastSend = GetTickCount();
	LogD("spectator", "header sent to %llu (%u bytes, %u buckets up to tick %u)", c.id, static_cast<unsigned>(file.size()),
		static_cast<unsigned>(nb), V);
}

void HostSendTicks(Client &c, uint32_t V, bool force, bool all = false)
{
	size_t last = c.next, bytes = 0;
	while (last < g_h.recs.size() && (all || g_h.recs[last].tick <= V) && last - c.next < 200) {
		const size_t size = g_h.recs[last].bytes.size();
		if (last > c.next && bytes + size > kTicksMsgMax) break;
		bytes += size;
		last++;
	}
	const DWORD now = GetTickCount();
	if (last == c.next && !force && now - c.lastSend < 250) return;
	if (V < c.sentV) V = c.sentV;
	std::string data;
	Put32(data, V);
	Put32(data, c.nextBucket);
	Put32(data, static_cast<uint32_t>(last - c.next));
	uint8_t *hostGame = Game();
	Put32(data, hostGame ? At<uint32_t>(hostGame, kGameSpeed) : 0);
	Put32(data, hostGame && GamePaused(hostGame) ? 1u : 0u);
	for (size_t i = c.next; i < last; i++) {
		Put32(data, static_cast<uint32_t>(g_h.recs[i].bytes.size()));
		data += g_h.recs[i].bytes;
	}
	Send(c.id, kSpTicks, data);
	if (last > c.next) c.nextBucket = g_h.recs[last - 1].first + g_h.recs[last - 1].count;
	c.next = last;
	c.sentV = V;
	c.lastSend = now;
}
void HostFinish()
{
	for (auto &kv : g_h.clients) {
		Client &c = kv.second;
		if (c.state == 2) {
			do HostSendTicks(c, g_h.lastClock, true, true);
			while (c.next < g_h.recs.size());
			std::string end;
			Put32(end, g_h.lastClock);
			Send(c.id, kSpEnd, end);
		}
	}
	LogI("spectator", "the game ended; %u buckets, %u spectators", static_cast<unsigned>(g_h.captured), static_cast<unsigned>(g_h.clients.size()));
	g_h.armed = false;
	g_h.game = nullptr;
	g_h.recs.clear();
	g_h.captured = 0;
	for (auto &kv : g_h.clients) kv.second.state = 0;
}

void HostHello(uint64_t peer, const std::string &hello, bool open, int delaySec)
{
	if (!open) {
		Send(peer, kSpNo, "The host no longer allows spectators.");
		return;
	}
	const size_t tab = hello.find('\t');
	const std::string mismatch = SteamVersionMismatch(hello.substr(0, tab));
	if (!mismatch.empty()) {
		Send(peer, kSpNo, mismatch);
		return;
	}
	const std::string key = tab == std::string::npos ? "" : hello.substr(tab + 1), own = SteamOwnGameplayKey();
	if (key.empty() || key != own) {
		Send(peer, kSpNo, "This game uses different content. Join it from the game list to download it.");
		LogI("spectator", "%llu has different content (%s, the game's %s); not accepted", peer, key.empty() ? "unknown" : key.c_str(), own.c_str());
		return;
	}
	if (g_h.clients.size() >= 16 && !g_h.clients.count(peer)) {
		Send(peer, kSpNo, "There are too many spectators.");
		return;
	}
	Client &c = g_h.clients[peer];
	c.id = peer;
	c.since = GetTickCount();
	c.state = g_h.armed ? 1 : 0;
	std::string ok = std::to_string(delaySec * 1000) + "\t" + (g_h.armed ? "1" : "0");
	Send(peer, kSpOk, ok);
	LogI("spectator", "%llu watches the game (delay %d s, %s)", peer, delaySec, g_h.armed ? "already started" : "not started yet");
}

void HostResend(uint64_t peer, const std::string &data)
{
	size_t pos = 0;
	uint32_t from = 0;
	const auto it = g_h.clients.find(peer);
	if (it == g_h.clients.end() || it->second.state != 2 || !Get32(data, pos, from)) return;
	Client &c = it->second;
	for (size_t i = 0; i < g_h.recs.size(); i++) {
		if (g_h.recs[i].first != from) continue;
		LogI("spectator", "%llu lost data from bucket %u: sending it again", peer, from);
		c.next = i;
		c.nextBucket = from;
		return;
	}
	LogW("spectator", "%llu asked for bucket %u again, which the host no longer has", peer, from);
}

void HostTick(std::deque<Msg> &msgs)
{
	int delaySec = 0;
	const bool open = SteamSpectatorsOpen(delaySec);
	uint8_t *game = Game();
	for (const Msg &m : msgs) {
		if (m.type == kSpHello) HostHello(m.peer, m.data, open, delaySec);
		else if (m.type == kSpBye) {
			g_h.clients.erase(m.peer);
			LogI("spectator", "%llu stops watching", m.peer);
		}
		else if (m.type == kSpResend) HostResend(m.peer, m.data);
	}
	if (!g_h.clients.empty()) {
		const std::vector<uint64_t> ids = SteamSpectatorIds();
		const DWORD listNow = GetTickCount();
		for (auto it = g_h.clients.begin(); it != g_h.clients.end();) {
			Client &listed = it->second;
			const bool present = std::find(ids.begin(), ids.end(), it->first) != ids.end();
			if (present) listed.seen = true;
			if (!present && (listed.seen || listNow - listed.since > 20000)) it = g_h.clients.erase(it);
			else ++it;
		}
	}
	g_clientCount = static_cast<int>(g_h.clients.size());
	SteamPublishWatchers(g_clientCount);
	if (g_h.armed && (!game || game != g_h.game || IsDemo())) HostFinish();
}

void HostArmIfStarted(bool open, uint8_t *game)
{
	if (g_h.armed || !open || !game || IsDemo() || SteamGameStartedAt() == 0) return;
	g_h.armed = true;
	g_h.game = game;
	g_h.recs.clear();
	g_h.captured = 0;
	g_h.lastClock = 0;
	g_h.armedAt = GetTickCount();
	for (auto &kv : g_h.clients) kv.second.state = 1;
	LogD("spectator", "starting to copy the game for whoever watches");
}

void HostCapture(uint8_t *game)
{
	const size_t n = BucketCount(game);
	if (g_h.captured >= n) return;
	EnsureScratchDir();
	const std::string vfs = ScratchVfs("bucket.tmp");
	const size_t first = g_h.captured, last = std::min(n, first + kCaptureChunk);
	Rec r;
	r.first = static_cast<uint32_t>(first);
	r.count = static_cast<uint32_t>(last - first);
	r.tick = BucketTickAt(game, last - 1);
	DeleteFileA(DiskOf(vfs).c_str());
	const bool ok = StreamWrite(vfs.c_str(), game, first, last, false) && ReadAll(DiskOf(vfs), r.bytes) && !r.bytes.empty();
	DeleteFileA(DiskOf(vfs).c_str());
	if (!ok) {
		static DWORD lastLog = 0;
		if (GetTickCount() - lastLog > 5000) {
			lastLog = GetTickCount();
			LogW("spectator", "could not copy buckets %u to %u", static_cast<unsigned>(first), static_cast<unsigned>(last));
		}
		return;
	}
	g_h.recs.push_back(std::move(r));
	g_h.captured = last;
}
void HostDrive(std::deque<Msg> &msgs)
{
	HostTick(msgs);
	int delaySec = 0;
	const bool open = SteamSpectatorsOpen(delaySec);
	uint8_t *game = Game();
	HostArmIfStarted(open, game);
	if (!g_h.armed || !game) return;
	if (!g_h.clients.empty()) HostCapture(game);
	const uint32_t clock = At<uint32_t>(game, kGameTick);
	g_h.lastClock = clock;
	static DWORD lastDiag = 0;
	if (GetTickCount() - lastDiag > 15000) {
		lastDiag = GetTickCount();
		LogD("spectator", "tick %u s, %u buckets in the game, %u copied, settings %s, %u spectators", clock / 1000,
			static_cast<unsigned>(BucketCount(game)), static_cast<unsigned>(g_h.captured),
			At<uint32_t>(game, kReplayData + 0x18) ? "yes" : "NO", static_cast<unsigned>(g_h.clients.size()));
	}
	const uint32_t delayMs = static_cast<uint32_t>(delaySec) * 1000;
	const uint32_t V = clock > delayMs ? clock - delayMs : 0;
	for (auto &kv : g_h.clients) {
		Client &c = kv.second;
		if (c.state == 1) {
			if (clock == 0) continue;
			if (!At<uint32_t>(game, kReplayData + 0x18)) {
				if (GetTickCount() - g_h.armedAt > 20000) {
					Send(c.id, kSpNo, "This game cannot be broadcast.");
					c.state = -1;
				}
				continue;
			}
			const uint32_t speed = At<uint32_t>(game, kGameSpeed);
			const double pace = speed >= 1 && speed <= 100000 ? std::max(0.5, speed / 1000.0) : 1.0;
			const uint32_t startV = static_cast<uint32_t>(kStartBufferMs * pace);
			if (V < startV) {
				const DWORD waitNow = GetTickCount();
				if (waitNow - c.lastWait >= 1000) {
					c.lastWait = waitNow;
					const double left = (static_cast<double>(delayMs) + startV - clock) / pace / 1000.0;
					Send(c.id, kSpWait, std::to_string((std::max)(1, static_cast<int>(left + 0.999))));
				}
				continue;
			}
			HostSendSnapshot(c, game, V, clock);
		}
		else if (c.state == 2) {
			HostSendTicks(c, V, false);
		}
	}
	for (auto it = g_h.clients.begin(); it != g_h.clients.end();) {
		if (it->second.state == -1) it = g_h.clients.erase(it);
		else ++it;
	}
}

enum Phase { kIdle, kHello, kWaitStart, kRecvSnap, kLoading, kViewing, kEnded, kFailed };
enum Flow { kFlowWaiting, kFlowCatching, kFlowLive };
const char *const kFlowNames[] = {"waiting for data", "catching up", "live"};

struct Viewer {
	Phase phase = kIdle;
	uint64_t host = 0;
	std::string hostName;
	DWORD helloAt = 0, loadAt = 0, lastHostMsg = 0;
	int helloTries = 0;
	uint32_t delayMs = 30000;
	uint32_t hostSpeed = 0;
	bool hostPaused = false;
	int waitFrom = 0;
	std::string snap;
	uint32_t snapTotal = 0, snapBuckets = 0, snapV = 0;
	std::deque<Msg> pending;
	uint32_t limit = 0;
	bool ended = false;
	bool endShown = false;
	uint32_t endTick = 0;
	Flow flow = kFlowWaiting;
	uint32_t catchFrom = 0;
	DWORD catchSince = 0;
	size_t buckets = 0;
	DWORD resendAt = 0, mismatchSince = 0;
	bool covering = false;
	DWORD coverSince = 0, coverMovedAt = 0;
	uint32_t coverTick = 0;
	std::string error;
	DWORD lastRoom = 0;
	uint8_t *game = nullptr;
	bool timeshift = false;
	int contentWait = 0;
};
Viewer g_v;
std::mutex g_viewMx;
std::string g_status;
std::atomic<bool> g_viewing{false}, g_watching{false}, g_covering{false};
std::atomic<int> g_progress{-1};

struct Controls {
	std::atomic<bool> active{false};
	std::atomic<bool> broadcast{false};
	std::atomic<bool> paused{false};
	std::atomic<bool> atLive{false};
	std::atomic<bool> collapsed{false};
	std::atomic<bool> uiHidden{false};
	std::atomic<bool> goLive{false};
	std::atomic<int> speed{1};
} g_ctl;

struct Seeking {
	bool ffActive = false;
	unsigned ffTarget = 0;
	bool reloading = false;
	bool wasBroadcast = false;
	bool ffPaused = false;
	DWORD reloadAt = 0;
	unsigned reloadClock = 0, reloadTarget = 0, origEnd = 0;
	uint8_t *oldGame = nullptr;
	bool hasCam = false;
	int camX = 0, camY = 0;
} g_sk;
std::atomic<int64_t> g_seekReq{-1};
std::atomic<unsigned> g_curMs{0}, g_totalMs{0};
typedef void(__fastcall *CenterOn_t)(void *view, void *edx, int *pos, int x);

bool GetCamera(int &x, int &y)
{
	char *view = *reinterpret_cast<char **>(addr::kVisibleMgr);
	if (!view) return false;
	x = (*reinterpret_cast<int *>(view + 0x618) + *reinterpret_cast<int *>(view + 0x620)) / 2;
	y = (*reinterpret_cast<int *>(view + 0x61c) + *reinterpret_cast<int *>(view + 0x624)) / 2;
	return true;
}

void SetCamera(int x, int y)
{
	void *view = *reinterpret_cast<void **>(addr::kVisibleMgr);
	if (!view) return;
	int pos[2] = {x, y};
	__try {
		reinterpret_cast<CenterOn_t>(addr::kCenterOn)(view, nullptr, pos, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

std::atomic<unsigned> g_effMs{0};
std::atomic<bool> g_busy{false};

typedef void(__fastcall *ThisInt2_t)(void *obj, void *edx, int a, int b);
typedef void(__fastcall *SetFocus_t)(void *root, void *edx, void *window);
const unsigned kUiRootSetFocusSlot = 0x6c / 4;
typedef unsigned(__fastcall *EndMenuShow_t)(void *dlg);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef unsigned(__fastcall *GetFlags_t)(void *win, void *edx, int redraw);
typedef void(__fastcall *ModFlags_t)(void *win, void *edx, unsigned set, unsigned clear, int redraw);
EndMenuShow_t g_origEndShow = nullptr;

void ApplyGameUi(bool hidden)
{
	void *mgr = *reinterpret_cast<void **>(addr::kSelectionManager);
	uint8_t *game = Game();
	if (!mgr || !game) return;
	int *state = nullptr;
	int old = 0;
	__try {
		uint8_t *player = At<uint8_t *>(game, 0x12c8);
		if (player) {
			state = reinterpret_cast<int *>(player + 0x290);
			old = *state;
			*state = 1;
		}
		reinterpret_cast<ThisInt2_t>(addr::kSetInterfaceHidden)(mgr, nullptr, hidden ? 1 : 0, hidden ? 1 : 0);
		void *root = *reinterpret_cast<void **>(addr::kUIRoot);
		void *map = *reinterpret_cast<void **>(addr::kMapView);
		if (root && map) reinterpret_cast<SetFocus_t>((*reinterpret_cast<void ***>(root))[kUiRootSetFocusSlot])(root, nullptr, map);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
	if (state) {
		__try {
			*state = old;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

const unsigned kBufferBase = 1500;
const unsigned kCatchUpBase = 4000;
const unsigned kCatchUpStep = 2000;
const double kCatchUpBudget = 25;
const unsigned kGentleBase = 3500;

double HostPace()
{
	const uint32_t speed = g_v.hostSpeed;
	return speed >= 1 && speed <= 100000 ? std::max(0.5, speed / 1000.0) : 1.0;
}

unsigned Buf() { return static_cast<unsigned>(kBufferBase * HostPace()); }
unsigned CatchUp() { return static_cast<unsigned>(kCatchUpBase * HostPace()); }
unsigned Gentle() { return static_cast<unsigned>(kGentleBase * HostPace()); }

unsigned LiveTotal(unsigned limit, unsigned cur)
{
	const unsigned playable = limit > Buf() ? limit - Buf() : 0;
	return std::max(playable, cur);
}

void SetStatus(const std::string &s)
{
	std::lock_guard<std::mutex> lock(g_viewMx);
	g_status = s;
}

const char *const kHashNames[kBucketHashCount] = {"object positions", "checksums", "sync 3", "sync 4", "sync 5", "commands", "fire and smoke", "sync 8"};

// Which of the game's sync hashes differ, and for how many buckets: a single hash that keeps differing while the
// object positions match is a value the replay computes differently, not a game that went its own way.
struct ReplayCheck {
	unsigned matched = 0, differed = 0;
	unsigned byHash[kBucketHashCount] = {};
	bool reported = false, positionsReported = false;
};
ReplayCheck g_check;

void ReplayCheckSummary()
{
	if (!g_check.matched && !g_check.differed) return;
	std::string by;
	for (int i = 0; i < kBucketHashCount; i++) {
		if (g_check.byHash[i]) by += (by.empty() ? "" : ", ") + std::string(kHashNames[i]) + " " + std::to_string(g_check.byHash[i]);
	}
	LogI("spectator", "the replay matched the game in %u buckets and differed in %u%s%s%s", g_check.matched, g_check.differed, by.empty() ? "" : " (",
		by.c_str(), by.empty() ? "" : ")");
	g_check = ReplayCheck();
}

void ViewerReset()
{
	ReplayCheckSummary();
	if (g_v.phase != kIdle) LogI("spectator", "stopped watching");
	const std::string snap = DiskOf(ScratchVfs("live.dmo"));
	DeleteFileA(snap.c_str());
	g_v = Viewer();
	g_viewing = false;
	g_watching = false;
	g_covering = false;
	AudioSetCoverMute(false);
	g_progress = -1;
	std::lock_guard<std::mutex> lock(g_viewMx);
	g_status.clear();
}

void ViewerFail(const std::string &why)
{
	LogW("spectator", "%s", TrSource(why).c_str());
	g_v.phase = kFailed;
	g_v.error = why;
	UiToast(why, true);
	SetStatus(why);
	SteamLeaveLobby();
}

void RequestReplayLoad(const std::string &vfs)
{
	reinterpret_cast<Assign_t>(addr::kStringAssign)(reinterpret_cast<void *>(addr::kSaveName), nullptr, vfs.c_str(), static_cast<unsigned>(vfs.size()));
	*reinterpret_cast<int *>(addr::kReplayRequest) = 1;
	if (char *frame = *reinterpret_cast<char **>(addr::kMenuFrame)) *reinterpret_cast<int *>(frame + 0x6c) = 1;
}

void ViewerApplyTicks(const std::string &data)
{
	uint8_t *game = Game();
	size_t pos = 0;
	uint32_t V = 0, first = 0, count = 0, speed = 0, flags = 0;
	if (!game || !Get32(data, pos, V) || !Get32(data, pos, first) || !Get32(data, pos, count) || !Get32(data, pos, speed) || !Get32(data, pos, flags)) return;
	if (speed >= 1 && speed <= 100000) g_v.hostSpeed = speed;
	g_v.hostPaused = (flags & 1) != 0;
	if (first != g_v.buckets) {
		const DWORD now = GetTickCount();
		if (!g_v.mismatchSince) g_v.mismatchSince = now;
		if (now - g_v.mismatchSince < kResendWaitMs) {
			if (first > g_v.buckets && now - g_v.resendAt > 1500) {
				g_v.resendAt = now;
				std::string ask;
				Put32(ask, static_cast<uint32_t>(g_v.buckets));
				Send(g_v.host, kSpResend, ask);
				LogW("spectator", "data from bucket %u was lost on the way: asking the host to send it again", static_cast<unsigned>(g_v.buckets));
			}
			return;
		}
		LogW("spectator", "bucket %u arrived and %u was expected for %u s: following the host's numbering", first, static_cast<unsigned>(g_v.buckets),
			kResendWaitMs / 1000);
		g_v.buckets = first;
	}
	g_v.mismatchSince = 0;
	EnsureScratchDir();
	const std::string vfs = ScratchVfs("received-bucket.tmp");
	for (uint32_t i = 0; i < count; i++) {
		uint32_t len = 0;
		if (!Get32(data, pos, len) || pos + len > data.size()) break;
		DeleteFileA(DiskOf(vfs).c_str());
		if (!WriteAll(DiskOf(vfs), data.substr(pos, len))) break;
		pos += len;
		uint32_t tick = 0;
		const int n = StreamAppend(vfs.c_str(), game, &tick, 1);
		if (n < 0) {
			static DWORD lastLog = 0;
			if (GetTickCount() - lastLog > 3000) {
				lastLog = GetTickCount();
				LogW("spectator", "could not read a received bucket");
			}
			continue;
		}
		g_v.buckets += static_cast<size_t>(n);
	}
	DeleteFileA(DiskOf(vfs).c_str());
	g_v.limit = std::max(g_v.limit, V);
}

void ViewerHandle(const Msg &m)
{
	g_v.lastHostMsg = GetTickCount();
	switch (m.type) {
	case kSpOk: {
		const size_t tab = m.data.find('\t');
		g_v.delayMs = static_cast<uint32_t>(atoi(m.data.c_str()));
		const bool started = tab != std::string::npos && m.data.compare(tab + 1, 1, "1") == 0;
		g_v.phase = started ? kRecvSnap : kWaitStart;
		SetStatus(started ? "Receiving the game..." : "Waiting for the game to start");
		break;
	}
	case kSpNo: ViewerFail(m.data.empty() ? "The host does not let you watch." : m.data); break;
	case kSpWait:
		if (g_v.phase == kRecvSnap || g_v.phase == kWaitStart) {
			const int left = (std::max)(1, atoi(m.data.c_str()));
			if (left > g_v.waitFrom) g_v.waitFrom = left;
			SetStatus(TrFmt("The game has just started: you will start watching in %d s", {std::to_string(left)}));
			g_progress = (g_v.waitFrom - left) * 100 / g_v.waitFrom;
		}
		break;
	case kSpSnapBegin: {
		size_t pos = 0;
		g_v.snap.clear();
		if (Get32(m.data, pos, g_v.snapTotal) && Get32(m.data, pos, g_v.snapBuckets) && Get32(m.data, pos, g_v.snapV)) {
			g_v.limit = g_v.snapV;
			g_v.snap.reserve(g_v.snapTotal);
		}
		g_v.phase = kRecvSnap;
		SetStatus("Receiving the game...");
		break;
	}
	case kSpSnapData:
		g_v.snap += m.data;
		if (g_v.snapTotal) g_progress = static_cast<int>((std::min<size_t>)(100, g_v.snap.size() * 100 / g_v.snapTotal));
		break;
	case kSpSnapEnd: {
		if (g_v.snap.size() != g_v.snapTotal) {
			ViewerFail("The game arrived incomplete.");
			break;
		}
		EnsureScratchDir();
		const std::string vfs = ScratchVfs("live.dmo");
		if (!WriteAll(DiskOf(vfs), g_v.snap)) {
			ViewerFail("The received game could not be saved.");
			break;
		}
		g_v.snap.clear();
		g_v.snap.shrink_to_fit();
		g_v.buckets = g_v.snapBuckets;
		g_progress = -1;
		g_v.phase = kLoading;
		g_v.loadAt = GetTickCount();
		SetStatus("Loading the game...");
		LogI("spectator", "loading the broadcast (%u buckets up to tick %u)", g_v.snapBuckets, g_v.snapV);
		RequestReplayLoad(vfs);
		break;
	}
	case kSpTicks:
		if (g_v.phase == kViewing && !g_sk.reloading) ViewerApplyTicks(m.data);
		else g_v.pending.push_back(m);
		break;
	case kSpEnd:
		if (g_v.phase == kViewing && !g_sk.reloading) {
			size_t pos = 0;
			uint32_t t = 0;
			Get32(m.data, pos, t);
			g_v.ended = true;
			g_v.endTick = std::max(t, g_v.limit);
			if (Game()) At<uint32_t>(Game(), kReplayEnd) = g_v.endTick;
			LogI("spectator", "the game ended (tick %u)", g_v.endTick);
		}
		else g_v.pending.push_back(m);
		break;
	default: break;
	}
}

void ViewerAdopt(uint8_t *game)
{
	g_v.game = game;
	g_v.phase = kViewing;
	g_v.flow = kFlowWaiting;
	g_v.covering = true;
	g_v.coverSince = g_v.coverMovedAt = GetTickCount();
	g_covering = true;
	// The game runs at full speed under the cover to reach the live game; its sound would play all of that.
	AudioSetCoverMute(true);
	const size_t n = BucketCount(game);
	if (n != g_v.buckets) LogD("spectator", "the replay has %u buckets and %u were expected", static_cast<unsigned>(n), static_cast<unsigned>(g_v.buckets));
	g_v.buckets = n;
	At<uint32_t>(game, kReplayEnd) = 0x7fffffff;
	g_viewing = true;
	LogI("spectator", "watching the broadcast (%u buckets, visible up to tick %u)", static_cast<unsigned>(n), g_v.limit);
	while (!g_v.pending.empty()) {
		const Msg m = g_v.pending.front();
		g_v.pending.pop_front();
		ViewerHandle(m);
	}
}

void ViewerReattach(uint8_t *game)
{
	g_v.game = game;
	g_v.buckets = BucketCount(game);
	g_v.flow = kFlowWaiting;
	At<uint32_t>(game, kReplayEnd) = g_v.ended ? g_v.endTick : 0x7fffffff;
	while (!g_v.pending.empty()) {
		const Msg m = g_v.pending.front();
		g_v.pending.pop_front();
		ViewerHandle(m);
	}
}

std::string HelloText()
{
	return std::string(ModVersionText()) + "\t" + SteamOwnGameplayKey();
}

bool ViewerContentReady(const OnlineRoom &room)
{
	const RoomContent content = SteamRoomContent(room.info, SteamOwnGameplayKey());
	if (content == RoomContent::kSame) {
		g_v.contentWait = 0;
		return true;
	}
	if (content == RoomContent::kChecking) {
		if (g_v.contentWait != 1) {
			g_v.contentWait = 1;
			SetStatus("Checking your content...");
			LogI("spectator", "waiting until the content of this computer is checked");
		}
		return false;
	}
	if (room.contentSync) {
		if (g_v.contentWait != 2) {
			g_v.contentWait = 2;
			LogI("spectator", "the content for this game is being prepared; the broadcast waits for the restart");
		}
		return false;
	}
	if (g_v.contentWait != 3) {
		g_v.contentWait = 3;
		LogW("spectator", "the content of this computer differs from the game's; leaving the spectator room");
		SteamLeaveForContent();
	}
	return false;
}

void ViewerTick(std::deque<Msg> &msgs)
{
	const DWORD now = GetTickCount();
	uint8_t *game = Game();
	for (const Msg &m : msgs) {
		if (g_v.phase == kIdle || g_v.phase == kFailed || m.peer != g_v.host) continue;
		ViewerHandle(m);
	}
	if (now - g_v.lastRoom > 300) {
		g_v.lastRoom = now;
		const OnlineRoom room = SteamCurrentRoom();
		const bool inSpec = room.id && room.info.spectate && (!room.owner || g_v.phase != kIdle);
		if (g_v.phase == kIdle || g_v.phase == kFailed) {
			if (inSpec && g_v.phase == kIdle && ViewerContentReady(room)) {
				g_v = Viewer();
				for (const OnlineMember &m : room.members) {
					if (m.owner) {
						g_v.host = m.id;
						g_v.hostName = m.name;
					}
				}
				if (g_v.host) {
					g_v.phase = kHello;
					g_v.helloAt = 0;
					g_watching = true;
					SteamResetStaleSession(g_v.host);
					SetStatus("Connecting to the host...");
				}
			}
			else if (!inSpec && g_v.phase == kFailed) {
				ViewerReset();
			}
			else if (!inSpec && g_v.contentWait) {
				g_v.contentWait = 0;
				SetStatus("");
			}
		}
		else if (!inSpec && !game) {
			ViewerReset();
		}
		else if (inSpec && g_v.phase == kViewing && !g_v.ended) {
			bool hostThere = false;
			for (const OnlineMember &m : room.members) hostThere = hostThere || m.id == g_v.host;
			if (!hostThere) {
				g_v.ended = true;
				g_v.endTick = g_v.limit;
				if (game) At<uint32_t>(game, kReplayEnd) = g_v.endTick;
				UiNotice("The game has ended", "The host has left the game, so the broadcast is over. You can look back over what you saw with the bar, or leave with Esc.");
				LogI("spectator", "the host left the spectator room");
			}
		}
	}
	switch (g_v.phase) {
	case kHello:
		if (now - g_v.helloAt > 3000) {
			if (g_v.helloTries >= 6) {
				ViewerFail("The host is not answering.");
				break;
			}
			if (g_v.helloTries > 0) SteamResetStaleSession(g_v.host);
			g_v.helloTries++;
			g_v.helloAt = now;
			Send(g_v.host, kSpHello, HelloText());
		}
		break;
	case kWaitStart:
		if (static_cast<int>(now - g_v.lastHostMsg) > 8000 && static_cast<int>(now - g_v.helloAt) > 8000) {
			SteamResetStaleSession(g_v.host);
			g_v.helloAt = now;
			Send(g_v.host, kSpHello, HelloText());
		}
		break;
	case kRecvSnap:
		if (static_cast<int>(now - g_v.lastHostMsg) > 8000 && static_cast<int>(now - g_v.helloAt) > 8000) {
			if (g_v.helloTries >= 8) {
				ViewerFail(Tr("The host stopped responding."));
				break;
			}
			SteamResetStaleSession(g_v.host);
			g_v.helloTries++;
			g_v.helloAt = now;
			Send(g_v.host, kSpHello, HelloText());
		}
		break;
	case kLoading:
		if (game && IsDemo()) ViewerAdopt(game);
		else if (static_cast<int>(now - g_v.loadAt) > 40000) ViewerFail("The game could not load the received game.");
		break;
	case kViewing:
		if (g_sk.reloading) break;
		if (!game || !IsDemo()) {
			if (!g_v.ended) Send(g_v.host, kSpBye);
			SteamLeaveLobby();
			ViewerReset();
			break;
		}
		break;
	default: break;
	}
}

bool StartReload(uint8_t *game, unsigned target, bool broadcast)
{
	EnsureScratchDir();
	const std::string vfs = ScratchVfs("rewind.dmo");
	DeleteFileA(DiskOf(vfs).c_str());
	if (!StreamWrite(vfs.c_str(), game, 0, BucketCount(game), true)) {
		LogW("spectator", "could not save the replay to go back");
		return false;
	}
	g_sk.reloading = true;
	g_sk.wasBroadcast = broadcast;
	g_sk.reloadAt = GetTickCount();
	g_sk.reloadClock = At<unsigned>(game, kGameTick);
	g_sk.origEnd = At<unsigned>(game, kReplayEnd);
	g_sk.reloadTarget = target;
	g_sk.oldGame = game;
	g_sk.hasCam = GetCamera(g_sk.camX, g_sk.camY);
	g_curMs = 0;
	g_sk.ffActive = false;
	LogW("spectator", "going back, from tick %u to %u", g_sk.reloadClock, target);
	RequestReplayLoad(vfs);
	return true;
}

void SeekPoll(uint8_t *game, bool broadcast)
{
	unsigned &cur = At<unsigned>(game, kGameTick);
	if (g_sk.reloading) {
		const bool fresh = *reinterpret_cast<int *>(addr::kReplayRequest) == 0 && (game != g_sk.oldGame || cur < g_sk.reloadClock);
		if (fresh) {
			g_sk.reloading = false;
			broadcast = g_sk.wasBroadcast;
			if (broadcast) ViewerReattach(game);
			else if (g_sk.origEnd) At<unsigned>(game, kReplayEnd) = g_sk.origEnd;
			g_sk.ffActive = true;
			g_sk.ffTarget = g_sk.reloadTarget;
			g_sk.ffPaused = g_ctl.paused;
			if (g_ctl.uiHidden) ApplyGameUi(true);
			if (g_sk.hasCam) SetCamera(g_sk.camX, g_sk.camY);
			LogI("spectator", "replay loaded again; advancing to tick %u", g_sk.ffTarget);
		}
		else {
			if (GetTickCount() - g_sk.reloadAt > 30000) {
				g_sk.reloading = false;
				LogW("spectator", "the replay did not load again");
			}
			const int64_t again = g_seekReq.exchange(-1);
			if (again >= 0) g_sk.reloadTarget = static_cast<unsigned>(std::min<int64_t>(again, 0x7ffffffe));
			g_effMs = g_sk.reloadTarget;
			g_busy = true;
			return;
		}
	}
	const unsigned limit = broadcast ? (g_v.ended ? g_v.endTick : g_v.limit) : At<unsigned>(game, kReplayEnd);
	g_curMs = cur;
	g_totalMs = broadcast && !g_v.ended ? LiveTotal(limit, cur) : std::max(limit, cur);
	const int64_t req = g_seekReq.exchange(-1);
	if (req >= 0) {
		unsigned t = static_cast<unsigned>(std::min<int64_t>(req, 0x7ffffffe));
		if (limit && t > limit) t = limit;
		if (broadcast) g_v.timeshift = !(t + Gentle() >= limit);
		if (t >= cur) {
			g_sk.ffActive = true;
			g_sk.ffTarget = t;
			g_sk.ffPaused = g_ctl.paused;
		}
		else {
			StartReload(game, t, broadcast);
		}
	}
	g_busy = g_sk.reloading || g_sk.ffActive;
	g_effMs = g_sk.reloading ? g_sk.reloadTarget : g_sk.ffActive ? g_sk.ffTarget : cur;
}

bool SeekStep(void *timer, void *edx, unsigned target, uint8_t *game)
{
	SeekPoll(game, g_v.phase == kViewing && game == g_v.game);
	if (g_sk.reloading) return true;
	if (!g_sk.ffActive) return false;
	if (g_ctl.paused != g_sk.ffPaused) {
		g_sk.ffActive = false;
		g_busy = false;
		LogW("spectator", "jump cancelled at tick %u", At<unsigned>(game, kGameTick));
		return false;
	}
	const bool broadcast = g_v.phase == kViewing && game == g_v.game;
	unsigned &cur = At<unsigned>(game, kGameTick);
	const unsigned limit = broadcast ? (g_v.ended ? g_v.endTick : g_v.limit) : At<unsigned>(game, kReplayEnd);
	unsigned goal = g_sk.ffTarget;
	if (goal > limit && limit) goal = limit;
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	while (cur < goal) {
		g_origRunTicks(timer, edx, std::min(goal, cur + kCatchUpStep));
		QueryPerformanceCounter(&b);
		if ((b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart > 80) break;
	}
	const int back = static_cast<int>(cur - target);
	At<int>(timer, kWindowFrom) += back;
	At<int>(timer, kWindowTo) += back;
	if (cur >= goal) {
		g_sk.ffActive = false;
		if (broadcast) g_v.flow = kFlowWaiting;
		if (g_sk.hasCam) {
			SetCamera(g_sk.camX, g_sk.camY);
			g_sk.hasCam = false;
		}
	}
	return true;
}

void EnableReplayMenu(void *dlg)
{
	__try {
		for (int id = 0x1001; id <= 0x100b; id++) {
			void *ctrl = reinterpret_cast<GetChild_t>(Vt(dlg)[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCWindow));
			if (!ctrl) continue;
			const unsigned flags = reinterpret_cast<GetFlags_t>(Vt(ctrl)[0xc / 4])(ctrl, nullptr, 0);
			LogD("spectator", "replay menu: button 0x%x with bits 0x%x%s", id, flags, id == 0x1003 && (flags & 0x200) ? " (it activates)" : "");
			if (id == 0x1003 && (flags & 0x200)) reinterpret_cast<ModFlags_t>(addr::kModifyFlags)(ctrl, nullptr, 0, 0x200, 1);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogW("spectator", "replay menu: could not read its buttons");
	}
}

ThisArg_t g_origReplaySerialize = nullptr;

void __fastcall HookReplaySerialize(void *self, void *edx, void *stream)
{
	uint8_t *game = Game();
	const bool writing = stream && reinterpret_cast<int *>(stream)[4] == 0;
	const bool guard = writing && game && self == game + kReplayData && IsDemo();
	const uint32_t keep = guard ? At<uint32_t>(self, 4) : 0;
	g_origReplaySerialize(self, edx, stream);
	if (guard) At<uint32_t>(self, 4) = keep;
}

unsigned __fastcall HookEndMenuShow(void *dlg)
{
	unsigned r = 0;
	{
		CivRaceAsBase local(CivsLocalPlayer());
		r = g_origEndShow(dlg);
	}
	if (IsDemo()) EnableReplayMenu(dlg);
	return r;
}

void ViewerPump(uint8_t *game)
{
	static DWORD last = 0;
	const DWORD now = GetTickCount();
	if (now - last < 40) return;
	last = now;
	if (g_v.phase != kViewing || game != g_v.game || g_sk.reloading) return;
	std::deque<Msg> mine;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		for (auto it = g_queue.begin(); it != g_queue.end();) {
			if (it->peer == g_v.host && (it->type == kSpTicks || it->type == kSpEnd)) {
				mine.push_back(*it);
				it = g_queue.erase(it);
			}
			else {
				++it;
			}
		}
	}
	for (const Msg &m : mine) ViewerHandle(m);
}

void __fastcall HookRunTicks(void *timer, void *edx, unsigned target)
{
	uint8_t *game = Game();
	if (g_v.phase == kLoading && game && IsDemo()) ViewerAdopt(game);
	if (game && IsDemo()) ViewerPump(game);
	const bool demo = game && IsDemo();
	g_ctl.active = demo;
	g_ctl.broadcast = demo && g_v.phase == kViewing && game == g_v.game;
	if (demo && SeekStep(timer, edx, target, game)) return;
	if (g_v.phase != kViewing || !game || game != g_v.game || !IsDemo()) {
		if (demo && (g_ctl.paused || g_ctl.speed > 1)) {
			unsigned &now = At<unsigned>(game, kGameTick);
			const unsigned before = now, delta = target > before ? target - before : 0;
			g_origRunTicks(timer, edx, g_ctl.paused ? before : before + delta * static_cast<unsigned>(g_ctl.speed));
			const int back = static_cast<int>(now - target);
			At<int>(timer, kWindowFrom) += back;
			At<int>(timer, kWindowTo) += back;
			return;
		}
		g_origRunTicks(timer, edx, target);
		return;
	}
	unsigned &cur = At<unsigned>(game, kGameTick);
	if (g_v.hostSpeed >= 1 && g_v.hostSpeed <= 100000) At<uint32_t>(game, kGameSpeed) = g_v.hostSpeed;
	if (g_v.ended && cur >= g_v.endTick) {
		if (!g_v.endShown) {
			g_v.endShown = true;
			UiNotice("The game has ended", "The players have left the game or it has finished. You can look back over it with the bar, or leave with Esc.");
		}
		g_origRunTicks(timer, edx, target);
		return;
	}
	const unsigned limit = g_v.ended ? g_v.endTick : g_v.limit;
	const unsigned delta = target > cur ? target - cur : 0;
	unsigned mult = static_cast<unsigned>(std::max(1, g_ctl.speed.load()));
	if (g_ctl.goLive.exchange(false)) {
		g_ctl.paused = false;
		g_ctl.speed = 1;
		g_v.timeshift = false;
		if (limit > cur + Buf()) {
			g_v.flow = kFlowCatching;
			g_v.catchFrom = cur;
			g_v.catchSince = GetTickCount();
			LogI("spectator", "to live from tick %u to %u", cur, limit);
		}
	}
	if (g_v.timeshift && limit <= cur + Gentle()) g_v.timeshift = false;
	if (g_ctl.broadcast && !g_v.timeshift && !g_v.ended && mult > 1 && limit <= cur + Gentle()) {
		g_ctl.speed = 1;
		mult = 1;
	}
	if (g_ctl.paused) {
		g_v.timeshift = true;
		g_ctl.atLive = false;
		const int back = static_cast<int>(cur - target);
		At<int>(timer, kWindowFrom) += back;
		At<int>(timer, kWindowTo) += back;
		return;
	}

	if ((!g_v.timeshift && !g_v.ended && limit > cur + CatchUp()) || (g_v.flow == kFlowCatching && limit > cur + Buf())) {
		if (g_v.flow != kFlowCatching) {
			g_v.catchFrom = cur;
			g_v.catchSince = GetTickCount();
			g_v.flow = kFlowCatching;
			LogD("spectator", "catching up from tick %u to %u", cur, limit);
		}
		LARGE_INTEGER f, a, b;
		QueryPerformanceFrequency(&f);
		QueryPerformanceCounter(&a);
		const unsigned goal = limit - std::min(limit, Buf());
		while (cur < goal) {
			g_origRunTicks(timer, edx, std::min(goal, cur + kCatchUpStep));
			QueryPerformanceCounter(&b);
			if ((b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart > kCatchUpBudget) break;
		}
		if (g_v.covering && goal > g_v.catchFrom) g_progress = static_cast<int>((std::min<unsigned>(goal, cur) - g_v.catchFrom) * 100ULL / (goal - g_v.catchFrom));
		if (cur >= goal) {
			const DWORD ms = GetTickCount() - g_v.catchSince;
			LogI("spectator", "caught up; %u s of game in %.1f s", (cur - g_v.catchFrom) / 1000, ms / 1000.0);
			g_v.flow = kFlowLive;
		}
	}
	else {
		unsigned want = cur + delta * mult;
		if (g_v.flow == kFlowWaiting) {
			if (limit >= cur + Buf() || g_v.ended) g_v.flow = kFlowLive;
			else want = cur;
		}
		if (g_v.flow == kFlowLive) {
			if (want > limit) {
				want = limit;
				if (!g_v.ended) g_v.flow = kFlowWaiting;
			}
		}
		if (want < cur) want = cur;
		g_origRunTicks(timer, edx, want);
	}
	if (g_v.covering) {
		const DWORD now = GetTickCount();
		if (cur > g_v.coverTick) {
			g_v.coverTick = cur;
			g_v.coverMovedAt = now;
		}
		const bool stalled = now - g_v.coverMovedAt > kCoverStallMs || now - g_v.coverSince > kCoverMaxMs;
		if (stalled && g_v.flow != kFlowLive && !g_v.ended) LogW("spectator", "the catch-up stopped advancing; showing the game as it is");
		if (g_v.flow == kFlowLive || g_v.ended || stalled) {
			g_v.covering = false;
			g_covering = false;
			AudioSetCoverMute(false);
			g_progress = -1;
			SetStatus("");
		}
		else if (g_v.flow == kFlowCatching) {
			SetStatus("Catching up with the live game...");
		}
		else {
			g_progress = -1;
			SetStatus("Preparing the live view...");
		}
	}
	g_ctl.atLive = g_v.flow == kFlowLive && mult == 1 && !g_v.ended && !g_v.timeshift && limit <= cur + Gentle() + Buf();
	g_curMs = cur;
	g_totalMs = g_v.ended ? std::max(limit, cur) : LiveTotal(limit, cur);
	const int shift = static_cast<int>(cur - target);
	if (shift) {
		At<int>(timer, kWindowFrom) += shift;
		At<int>(timer, kWindowTo) += shift;
	}
}

}

void SpectatorOnSteamMessage(uint64_t peer, uint8_t type, const std::string &data)
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_queue.push_back({peer, type, data});
	if (g_queue.size() > 5000) g_queue.pop_front();
}

static bool g_pauseShown = false;

static void SyncPauseVisual()
{
	uint8_t *game = Game();
	const bool want = game && IsDemo() && g_ctl.active && (g_ctl.paused || SpectatorHostPaused());
	void *menu = *reinterpret_cast<void **>(addr::kPausedMenu);
	if (want && !menu) {
		reinterpret_cast<void(__fastcall *)(void *)>(Vt(game)[0x60 / 4])(game);
		g_pauseShown = true;
	}
	else if (!want && g_pauseShown) {
		g_pauseShown = false;
		if (menu && game && !GamePaused(game)) reinterpret_cast<void(__fastcall *)(void *)>(Vt(game)[0x5c / 4])(game);
	}
}

void SpectatorGameThreadTick()
{
	static bool inside = false;
	if (inside) return;
	inside = true;
	struct Leave {
		~Leave() { inside = false; }
	} leave;
	std::deque<Msg> msgs;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		msgs.swap(g_queue);
	}
	if (Game() && IsDemo() && g_ctl.active) SeekPoll(Game(), (g_v.phase == kViewing && Game() == g_v.game) || (g_sk.reloading && g_sk.wasBroadcast));
	if (!Game() || !IsDemo()) {
		g_ctl.uiHidden = false;
		g_ctl.collapsed = false;
		g_ctl.active = false;
		g_ctl.broadcast = false;
		g_ctl.paused = false;
		g_ctl.atLive = false;
		g_ctl.goLive = false;
		g_ctl.speed = 1;
		g_pauseShown = false;
		if (!g_sk.reloading) {
			g_sk.ffActive = false;
			g_busy = false;
			g_seekReq = -1;
		}
	}
	HostDrive(msgs);
	ViewerTick(msgs);
	SyncPauseVisual();
}

bool SpectatorControls(SpectatorState &out)
{
	if (!g_ctl.active) return false;
	out.paused = g_ctl.paused;
	out.speed = g_ctl.speed;
	out.broadcast = g_ctl.broadcast;
	out.atLive = g_ctl.atLive;
	out.ended = g_ctl.broadcast && g_v.ended && g_curMs + 500 >= g_v.endTick;
	out.collapsed = g_ctl.collapsed;
	out.uiHidden = g_ctl.uiHidden;
	out.cur = g_curMs;
	out.total = g_totalMs;
	out.target = g_effMs;
	out.busy = g_busy;
	return true;
}

void SpectatorBarPos(double &centerX, double &top, int mode)
{
	static double cx = -1, ty = -2;
	if (cx < 0) {
		char text[32] = {};
		GetPrivateProfileStringA("Spectator", "BarX", "0.5", text, sizeof(text), g_ini);
		cx = std::max(0.0, std::min(1.0, atof(text)));
		GetPrivateProfileStringA("Spectator", "BarY", "-1", text, sizeof(text), g_ini);
		ty = atof(text);
	}
	if (mode == 0) {
		centerX = cx;
		top = ty;
		return;
	}
	cx = std::max(0.0, std::min(1.0, centerX));
	ty = top < 0 ? -1.0 : std::min(1.0, top);
	if (mode == 2) {
		char text[32];
		snprintf(text, sizeof(text), "%.4f", cx);
		WritePrivateProfileStringA("Spectator", "BarX", text, g_ini);
		snprintf(text, sizeof(text), "%.4f", ty);
		WritePrivateProfileStringA("Spectator", "BarY", text, g_ini);
	}
}

void SpectatorSeekFraction(double fraction)
{
	if (!g_ctl.active) return;
	const unsigned total = g_totalMs;
	g_seekReq = static_cast<int64_t>(std::max(0.0, std::min(1.0, fraction)) * total);
}

bool AtLiveEdge()
{
	return g_ctl.broadcast && g_v.phase == kViewing && !g_v.ended && !g_v.timeshift && g_v.flow != kFlowCatching &&
		g_v.limit <= g_curMs + Gentle();
}

bool SpeedUp()
{
	if (AtLiveEdge()) {
		UiToast("You are at the live edge: you can only speed up when you are behind");
		return false;
	}
	const int s = g_ctl.speed;
	g_ctl.speed = s >= 8 ? 1 : s * 2;
	g_ctl.paused = false;
	return true;
}

void SpectatorControl(int id)
{
	if (!g_ctl.active) return;
	if (id == 0) {
		g_ctl.paused = !g_ctl.paused;
	}
	else if (id == 1) {
		if (g_ctl.speed >= 8) {
			g_ctl.speed = 1;
			g_ctl.paused = false;
		}
		else {
			SpeedUp();
		}
	}
	else if (id == 2 || id == 3) {
		int64_t base = g_seekReq.load();
		if (base < 0) base = g_effMs;
		g_seekReq = id == 2 ? std::max<int64_t>(0, base - 10000) : base + 10000;
	}
	else if (id == 4 && g_ctl.broadcast) {
		if (g_v.ended && g_curMs + 500 >= g_v.endTick) QuitToMenu();
		else g_ctl.goLive = true;
	}
	else if (id == 5) {
		g_ctl.uiHidden = !g_ctl.uiHidden;
		ApplyGameUi(g_ctl.uiHidden);
	}
	else if (id == 6) {
		g_ctl.collapsed = !g_ctl.collapsed;
	}

}

bool SpectatorHandleKey(unsigned msg, unsigned wp, long)
{
	if (!g_ctl.active || (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN)) return false;
	if (ControlsModKeyPressed(kModKeySpecPause, wp)) { SpectatorControl(0); return true; }
	if (ControlsModKeyPressed(kModKeySpecFaster, wp)) { if (g_ctl.speed < 8) SpeedUp(); return true; }
	if (ControlsModKeyPressed(kModKeySpecSlower, wp)) { g_ctl.speed = std::max(1, g_ctl.speed / 2); return true; }
	if (ControlsModKeyPressed(kModKeySpecBack, wp)) { SpectatorControl(2); return true; }
	if (ControlsModKeyPressed(kModKeySpecForward, wp)) { SpectatorControl(3); return true; }
	if (g_ctl.broadcast && ControlsModKeyPressed(kModKeySpecLive, wp)) { SpectatorControl(4); return true; }
	if (ControlsModKeyPressed(kModKeySpecUi, wp)) { SpectatorControl(5); return true; }
	if (ControlsModKeyPressed(kModKeySpecBar, wp)) { SpectatorControl(6); return true; }
	return false;
}

bool SpectatorWatching() { return g_watching; }
int SpectatorProgress() { return g_progress; }
int SpectatorLagMs()
{
	if (!g_ctl.active || !g_ctl.broadcast || g_v.phase != kViewing || g_v.ended) return -1;
	const unsigned host = g_v.limit, cur = g_curMs;
	return static_cast<int>((host > cur ? host - cur : 0) / HostPace());
}

bool SpectatorHostPaused()
{
	return g_ctl.active && g_ctl.broadcast && g_v.phase == kViewing && !g_v.ended && g_v.hostPaused && g_v.flow == kFlowWaiting;
}

int SpectatorDelaySec()
{
	if (!g_ctl.active || !g_ctl.broadcast || g_v.phase != kViewing) return 0;
	return static_cast<int>(g_v.delayMs / 1000);
}

bool SpectatorWantsWake() { return g_watching && (!g_viewing || g_v.flow == kFlowWaiting); }
bool SpectatorViewing() { return g_viewing; }
bool SpectatorCovering() { return g_covering; }
int SpectatorCount() { return g_clientCount; }

std::string SpectatorStatusText()
{
	std::lock_guard<std::mutex> lock(g_viewMx);
	return g_status;
}

typedef void(__fastcall *PlaybackTick_t)(void *pump);
PlaybackTick_t g_origPlayback = nullptr;

bool DifferingHashes(const uint32_t *recorded, const uint32_t *now, unsigned &mask)
{
	__try {
		for (int i = 0; i < kBucketHashCount; i++) {
			if (recorded[i] != now[i]) mask |= 1u << i;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

typedef int(__fastcall *CommandSynced_t)(void *registry, void *edx, void *command);

void DescribeBucket(const uint8_t *bucket, char *out, size_t cap)
{
	out[0] = 0;
	size_t used = 0;
	__try {
		void *registry = *reinterpret_cast<void **>(addr::kCmdRegistry);
		auto synced = reinterpret_cast<CommandSynced_t>((*reinterpret_cast<void ***>(registry))[0x30 / 4]);
		int shown = 0, total = 0;
		for (int queue = 0; queue < 16; queue++) {
			const int *list = reinterpret_cast<const int *>(bucket + 4 + queue * 12);
			const int *head = reinterpret_cast<const int *>(list[0]);
			for (const int *node = reinterpret_cast<const int *>(head[0]); node != head && total < 400; node = reinterpret_cast<const int *>(node[0]), total++) {
				void *command = reinterpret_cast<void *>(node[2]);
				if (shown >= 12) continue;
				const int *definition = reinterpret_cast<const int *>(reinterpret_cast<const int *>(command)[1]);
				const int written = _snprintf_s(out + used, cap - used, _TRUNCATE, "%s[player %d id %u param %d%s]", shown ? " " : "", queue,
					static_cast<unsigned>(definition[2]), reinterpret_cast<const int *>(command)[2], synced(registry, nullptr, command) ? "" : " not synced");
				if (written < 0) break;
				used += static_cast<size_t>(written);
				shown++;
			}
		}
		if (total > shown) _snprintf_s(out + used, cap - used, _TRUNCATE, " ... %d commands in total", total);
		else if (!shown) _snprintf_s(out, cap, _TRUNCATE, "no commands");
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		_snprintf_s(out, cap, _TRUNCATE, "could not read the commands");
	}
}

void __fastcall HookPlaybackTick(void *pump)
{
	uint8_t *game = Game();
	const unsigned before = game ? At<unsigned>(game, kReplayData) : 0;
	g_origPlayback(pump);
	if (!game || g_v.phase != kViewing || game != g_v.game) return;
	const unsigned after = At<unsigned>(game, kReplayData);
	if (after != before + 1) return;
	uint8_t *buckets = At<uint8_t *>(game, kBucketsBegin);
	if (!buckets || before >= BucketCount(game)) return;
	const uint32_t *recorded = reinterpret_cast<const uint32_t *>(buckets + before * kBucketSize + kBucketHashes);
	const uint32_t *now = reinterpret_cast<const uint32_t *>(addr::kSyncHashes);
	unsigned mask = 0;
	if (!DifferingHashes(recorded, now, mask)) return;
	std::string different;
	for (int i = 0; i < kBucketHashCount; i++) {
		if (!(mask & (1u << i))) continue;
		different += (different.empty() ? "" : ", ") + std::string(kHashNames[i]);
		g_check.byHash[i]++;
	}
	if (different.empty()) {
		g_check.matched++;
		return;
	}
	g_check.differed++;
	if ((mask & 1u) && !g_check.positionsReported) {
		g_check.positionsReported = true;
		if (g_check.reported) LogW("spectator", "object positions differ from tick %u (bucket %u)", BucketTickAt(game, before), before);
	}
	if (!g_check.reported) {
		g_check.reported = true;
		LogW("spectator", "the replay no longer matches the game at tick %u (bucket %u, %u buckets matched before): differs in %s", BucketTickAt(game, before), before,
			g_check.matched, different.c_str());
		for (unsigned i = before >= 3 ? before - 3 : 0; i <= before; i++) {
			char text[900];
			DescribeBucket(buckets + i * kBucketSize, text, sizeof(text));
			LogI("spectator", "bucket %u (tick %u): %s", i, BucketTickAt(game, i), text);
		}
	}
}

void InstallSpectator(bool knownExe)
{
	if (!knownExe) return;
	HookCreate(reinterpret_cast<void *>(addr::kPlaybackRecordedTick), reinterpret_cast<void *>(&HookPlaybackTick), reinterpret_cast<void **>(&g_origPlayback));
	if (g_origPlayback) HookEnable(reinterpret_cast<void *>(addr::kPlaybackRecordedTick));
	GetModuleFileNameA(nullptr, g_ini, MAX_PATH);
	if (char *slash = strrchr(g_ini, '\\')) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - g_ini), "imperivm-mod.ini");
	const bool ok = HookCreate(reinterpret_cast<void *>(addr::kRunTicksUpTo), reinterpret_cast<void *>(&HookRunTicks),
						reinterpret_cast<void **>(&g_origRunTicks)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kRunTicksUpTo)) == MH_OK;
	const bool show = HookCreate(reinterpret_cast<void *>(addr::kEndMenuOnShow), reinterpret_cast<void *>(&HookEndMenuShow),
						  reinterpret_cast<void **>(&g_origEndShow)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kEndMenuOnShow)) == MH_OK;
	const bool keepEnd = HookCreate(reinterpret_cast<void *>(addr::kReplaySerialize), reinterpret_cast<void *>(&HookReplaySerialize),
							 reinterpret_cast<void **>(&g_origReplaySerialize)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kReplaySerialize)) == MH_OK;
	LogAt(ok ? kLogInfo : kLogError, "spectator", "%s (replay menu: %s, the save respects the end: %s)", ok ? "ready" : "NOT available", show ? "yes" : "no",
		keepEnd ? "yes" : "no");
}
