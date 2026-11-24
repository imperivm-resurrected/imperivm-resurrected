// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <cstring>
#include <mutex>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "hooks/tweaks.h"
#include "core/log.h"
#include "core/syncdiag.h"
#include "mods/mods.h"
#include "net/nettune.h"
#include "net/netdrop.h"
#include "net/rejoin.h"
#include "net/steamnet.h"
#include "addr/net.h"
#include "addr/state.h"
#include "addr/world.h"

namespace {
const unsigned kMinTickMs = 120;
const unsigned kMaxTickMs = 500;
const unsigned kTickPercent = 70;
// The rest of [Network] comes from each computer's Config.ini as well. LambdaPercent decides with TickPercent how fast the turn grows, so
// how far each turn moves the game; the delay's two only move the real-time window and BoundPercent is never read. All of them get the
// values the game installs, on every computer.
const int kLambdaPercent = 50;
const int kDelayPercent = 20;
const int kLambdaDelayPercent = 50;
const int kBoundPercent = 80;

const size_t kOffTick = 0x54;
const size_t kOffSuggested = 0x70;
const size_t kOffDelay = 0x74;
const size_t kOffMinTick = 0x3f0;
const size_t kOffMaxTick = 0x3f4;

typedef void *(__fastcall *Ctor_t)(void *self, void *edx, unsigned long a, int b, unsigned c, unsigned d);
Ctor_t g_origCtor = nullptr;
bool g_patched = false;
// The transport's constructor is hooked: every online game gets the values above, whether or not the delay's patch went in.
bool g_forced = false;

// Config.ini [system] as the game read it at startup: the game changes none of them later (AIEnabled only from a saved game's AI).
struct ConfigSim {
	bool read = false;
	int newPf = -1, aiEnabled = -1, invalidTerrains = -1;
};
std::mutex g_configMx;
ConfigSim g_configSim;

void NoteConfigLoaded()
{
	ConfigSim c;
	c.read = true;
	c.newPf = *reinterpret_cast<const int *>(addr::kConfigNewPf);
	c.aiEnabled = *reinterpret_cast<const int *>(addr::kAiEnabled);
	c.invalidTerrains = *reinterpret_cast<const int *>(addr::kConfigInvalidTerrains);
	{
		std::lock_guard<std::mutex> lock(g_configMx);
		g_configSim = c;
	}
	LogI("net", "Config.ini: NewPF %d, AIEnabled %d, InvalidTerrains %d (they decide the simulation: rooms compare them)", c.newPf, c.aiEnabled,
		c.invalidTerrains);
}

unsigned g_badLines = 0;

template <typename T>
T &At(void *base, size_t off)
{
	return *reinterpret_cast<T *>(static_cast<char *>(base) + off);
}

static_assert(addr::kDelayMultiplier == 0x0081a2fc && addr::kDelayResume == 0x00407caa, "DelayFixCave hard-codes these addresses in its assembly");

__declspec(naked) void DelayFixCave()
{
	__asm {
		sub esi, ebx
		jns nonneg
		xor esi, esi
	nonneg:
		imul esi, dword ptr ds:[0x0081A2FC]
		push 0x00407CAA
		ret
	}
}

bool PatchDelaySite()
{
	static const unsigned char expect[9] = {0x2B, 0xF3, 0x0F, 0xAF, 0x35, 0xFC, 0xA2, 0x81, 0x00};
	unsigned char *site = reinterpret_cast<unsigned char *>(addr::kDelaySite);
	if (memcmp(site, expect, sizeof(expect)) != 0) return false;
	DWORD old = 0;
	if (!VirtualProtect(site, sizeof(expect), PAGE_EXECUTE_READWRITE, &old)) return false;
	const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&DelayFixCave) - (addr::kDelaySite + 5));
	site[0] = 0xE9;
	memcpy(site + 1, &rel, sizeof(rel));
	memset(site + 5, 0x90, 4);
	VirtualProtect(site, sizeof(expect), old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(expect));
	return true;
}

void *__fastcall HookCtor(void *self, void *edx, unsigned long a, int b, unsigned c, unsigned d)
{
	void *r = g_origCtor(self, edx, a, b, c, d);
	if (self) {
		int &tickPercent = *reinterpret_cast<int *>(addr::kTickPercentAddr);
		int &lambda = *reinterpret_cast<int *>(addr::kNetLambdaPercent);
		int &delay = *reinterpret_cast<int *>(addr::kDelayMultiplier);
		int &lambdaDelay = *reinterpret_cast<int *>(addr::kNetLambdaDelayPercent);
		int &bound = *reinterpret_cast<int *>(addr::kNetBoundPercent);
		const unsigned oldMin = At<unsigned>(self, kOffMinTick), oldMax = At<unsigned>(self, kOffMaxTick);
		const int oldPercent = tickPercent, oldLambda = lambda, oldDelay = delay, oldLambdaDelay = lambdaDelay, oldBound = bound;
		At<unsigned>(self, kOffMinTick) = kMinTickMs;
		At<unsigned>(self, kOffMaxTick) = kMaxTickMs;
		tickPercent = static_cast<int>(kTickPercent);
		lambda = kLambdaPercent;
		delay = kDelayPercent;
		lambdaDelay = kLambdaDelayPercent;
		bound = kBoundPercent;
		LogI("net", "game network: network turns set (minimum %u ms, maximum %u ms, margin %u %%, smoothing %d %%, delay %d %%/%d %%, bound %d %%; "
			"Config.ini had %u, %u, %d, %d, %d/%d and %d)%s", kMinTickMs, kMaxTickMs, kTickPercent, kLambdaPercent, kDelayPercent, kLambdaDelayPercent,
			kBoundPercent, oldMin, oldMax, oldPercent, oldLambda, oldDelay, oldLambdaDelay, oldBound, g_patched ? "" : "; the delay's fix is not in");
		LogI("net", "game network: AIEnabled %d at the start (Config.ini said %d)", *reinterpret_cast<const int *>(addr::kAiEnabled), g_configSim.aiEnabled);
	}
	g_badLines = 0;
	NetDropNetworkStarted();
	RejoinNetworkStarted();
	return r;
}

struct PeerArrival {
	unsigned samples = 0, late = 0, maxDelay = 0;
	double sum = 0;
};
std::mutex g_arrivalMx;
std::map<unsigned, PeerArrival> g_arrivals;
unsigned g_resends = 0;
// The same for the performance panel, taken by NetTuneTakeSecond: the log's ones are taken every 30 s.
unsigned g_secResends = 0;
int g_secLateMax = -1;

typedef void(__cdecl *TransportLog_t)(const char *fmt, ...);
TransportLog_t g_origLog = nullptr;

const size_t kRecentLines = 600;
std::vector<std::string> g_recent;
size_t g_recentNext = 0;

int CurrentTick()
{
	const char *t = *reinterpret_cast<const char *const *>(addr::kTransport);
	return t ? *reinterpret_cast<const int *>(t + kOffTick) : -1;
}

// The game's own warnings that a tick ran out of turn or a player's commands could not be read: either is a desync coming, and with the
// game's log off they went nowhere.
void NoteGameWarning(const std::string &line)
{
	if (line.find("WRONG TIME") == std::string::npos && line.find("BAD COMMAND") == std::string::npos) return;
	const unsigned n = ++g_badLines;
	if (n <= 20) LogE("match", "game network: \"%s\" at tick %d", line.c_str(), CurrentTick());
	else if (n == 21) LogE("match", "game network: more warnings of the game in this game, not logged");
}

void NoteTransportLine(const char *text)
{
	{
		std::string line = text;
		for (char &ch : line) {
			if (ch == '\n' || ch == '\r') ch = ' ';
		}
		while (!line.empty() && line.back() == ' ') line.pop_back();
		NoteGameWarning(line);
		std::lock_guard<std::mutex> lock(g_arrivalMx);
		if (g_recent.size() < kRecentLines) g_recent.push_back(line);
		else g_recent[g_recentNext] = line;
		g_recentNext = (g_recentNext + 1) % kRecentLines;
	}
	unsigned tick = 0, a = 0, b = 0, c = 0, d = 0, delay = 0;
	if (strstr(text, "PEER DELAY") && sscanf_s(text, "%*d - PEER DELAY - tick %u - %u.%u.%u.%u - realtime delay %u", &tick, &a, &b, &c, &d, &delay) == 6) {
		if (delay > 100000) return;
		std::lock_guard<std::mutex> lock(g_arrivalMx);
		PeerArrival &p = g_arrivals[(c << 8) | d];
		p.samples++;
		p.sum += delay;
		p.maxDelay = std::max(p.maxDelay, delay);
		if (delay > 250) p.late++;
		g_secLateMax = std::max(g_secLateMax, static_cast<int>(delay));
	}
	else if (strstr(text, "NACK SEND")) {
		std::lock_guard<std::mutex> lock(g_arrivalMx);
		g_resends++;
		g_secResends++;
	}
}

void __cdecl HookLog(const char *fmt, ...)
{
	char text[512];
	va_list args;
	va_start(args, fmt);
	const int n = _vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, args);
	va_end(args);
	if (n < 0 && text[0] == 0) return;
	NoteTransportLine(text);
	g_origLog("%s", text);
}

void LogArrivals()
{
	std::map<unsigned, PeerArrival> arrivals;
	unsigned resends = 0;
	{
		std::lock_guard<std::mutex> lock(g_arrivalMx);
		arrivals.swap(g_arrivals);
		resends = g_resends;
		g_resends = 0;
	}
	if (arrivals.empty() && !resends) return;
	std::map<unsigned, std::string> names;
	for (const OnlineMember &m : SteamCurrentRoom().members) names[static_cast<unsigned>(m.number)] = m.name;
	std::string text;
	for (const auto &entry : arrivals) {
		const PeerArrival &p = entry.second;
		const auto name = names.find(entry.first);
		char line[160];
		sprintf_s(line, "%s%s: mean %.0f ms, max %u ms, %u of %u turns over 250 ms", text.empty() ? "" : "; ",
			name != names.end() ? name->second.c_str() : ("127.77.0." + std::to_string(entry.first)).c_str(), p.sum / p.samples, p.maxDelay, p.late, p.samples);
		text += line;
	}
	const char *gameBase = *reinterpret_cast<const char **>(addr::kGame);
	const unsigned speed = gameBase ? *reinterpret_cast<const unsigned *>(gameBase + 0x1244) : 0;
	LogI("net", "game network: when each player's data arrived after it was due: %s; game speed %u; resend requests: %u", text.c_str(), speed, resends);
}

LARGE_INTEGER g_freq = {};
LARGE_INTEGER g_last = {};
unsigned g_lastTick = 0;
bool g_haveLast = false;

struct Window {
	std::vector<unsigned> intervals;
	double suggestedSum = 0, delaySum = 0;
	unsigned suggestedMax = 0, slow = 0;
	DWORD since = 0;
};
Window g_win;

std::mutex g_secMx;
unsigned g_secCount = 0, g_secSum = 0, g_secMax = 0, g_secSizeSum = 0, g_secDelaySum = 0, g_secWait = 0;

void FlushWindow()
{
	if (g_win.intervals.empty()) {
		g_win = Window();
		return;
	}
	LogArrivals();
	std::vector<unsigned> v = g_win.intervals;
	std::sort(v.begin(), v.end());
	double sum = 0;
	for (unsigned x : v) sum += x;
	const size_t n = v.size();
	LogI("net", "game network: %u turns in %u s; mean interval %.0f ms (median %u, p90 %u, max %u); mean turn size "
		"%.0f ms (max %u), mean added delay %.0f ms; turns slower than expected: %u",
		static_cast<unsigned>(n), (GetTickCount() - g_win.since) / 1000, sum / n, v[n / 2], v[std::min(n - 1, n * 9 / 10)], v[n - 1],
		g_win.suggestedSum / n, g_win.suggestedMax, g_win.delaySum / n, g_win.slow);
	g_win = Window();
}

}

void InstallNetTune(bool knownExe)
{
	if (!knownExe) return;
	g_patched = PatchDelaySite();
	const bool hooked = HookCreate(reinterpret_cast<void *>(addr::kTransportCtor), reinterpret_cast<void *>(&HookCtor),
							reinterpret_cast<void **>(&g_origCtor)) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr::kTransportCtor)) == MH_OK;
	QueryPerformanceFrequency(&g_freq);
	HookCreate(reinterpret_cast<void *>(addr::kTransportLog), reinterpret_cast<void *>(&HookLog), reinterpret_cast<void **>(&g_origLog));
	if (g_origLog) HookEnable(reinterpret_cast<void *>(addr::kTransportLog));
	LogAt(g_patched && hooked ? kLogInfo : kLogError, "net", "game network: turn adjustment %s", g_patched && hooked ? "ready" : "NOT available");
	if (!hooked) g_patched = false;
	g_forced = hooked;
	TweaksOnConfigLoaded(&NoteConfigLoaded);
}

std::string NetSimSignature()
{
	SimInputs in;
	in.turnsForced = g_forced;
	in.minTick = kMinTickMs;
	in.maxTick = kMaxTickMs;
	in.tickPercent = kTickPercent;
	in.lambda = kLambdaPercent;
	{
		std::lock_guard<std::mutex> lock(g_configMx);
		in.newPf = g_configSim.newPf;
		in.aiEnabled = g_configSim.aiEnabled;
		in.invalidTerrains = g_configSim.invalidTerrains;
	}
	in.crashFixSites = TweaksCrashFixSites();
	in.rejoinHooks = RejoinHooksReady();
	in.dropHooks = NetDropHooksReady();
	return SimSignatureText(in);
}

std::string NetRulesLevel()
{
	return RulesLevelWithSim(ModsRulesLevel(), NetSimSignature());
}

std::string NetTuneDescribe()
{
	const char *t = *reinterpret_cast<const char *const *>(addr::kTransport);
	char text[256];
	sprintf_s(text, "MinTickSize %d, MaxTickSize %d, TickPercent %d, LambdaPercent %d, DelayPercent %d, LambdaDelayPercent %d, BoundPercent %d (%s)",
		t ? At<int>(const_cast<char *>(t), kOffMinTick) : -1, t ? At<int>(const_cast<char *>(t), kOffMaxTick) : -1,
		*reinterpret_cast<const int *>(addr::kTickPercentAddr), *reinterpret_cast<const int *>(addr::kNetLambdaPercent),
		*reinterpret_cast<const int *>(addr::kDelayMultiplier), *reinterpret_cast<const int *>(addr::kNetLambdaDelayPercent),
		*reinterpret_cast<const int *>(addr::kNetBoundPercent), g_forced ? "set by the mod" : "Config.ini's");
	return text;
}

std::vector<std::string> NetTuneRecentLines()
{
	std::lock_guard<std::mutex> lock(g_arrivalMx);
	if (g_recent.size() < kRecentLines) return g_recent;
	std::vector<std::string> out(g_recent.begin() + static_cast<std::ptrdiff_t>(g_recentNext), g_recent.end());
	out.insert(out.end(), g_recent.begin(), g_recent.begin() + static_cast<std::ptrdiff_t>(g_recentNext));
	return out;
}

NetTurnSecond NetTuneTakeSecond()
{
	std::lock_guard<std::mutex> lock(g_secMx);
	NetTurnSecond out;
	out.count = static_cast<int>(g_secCount);
	if (g_secCount) {
		out.avgMs = static_cast<int>((g_secSum + g_secCount / 2) / g_secCount);
		out.maxMs = static_cast<int>(g_secMax);
		out.sizeAvgMs = static_cast<int>((g_secSizeSum + g_secCount / 2) / g_secCount);
		out.delayAvgMs = static_cast<int>((g_secDelaySum + g_secCount / 2) / g_secCount);
		out.waitMs = static_cast<int>(g_secWait);
	}
	g_secCount = g_secSum = g_secMax = g_secSizeSum = g_secDelaySum = g_secWait = 0;
	{
		std::lock_guard<std::mutex> arrivals(g_arrivalMx);
		out.resends = static_cast<int>(g_secResends);
		out.lateMaxMs = g_secLateMax;
		g_secResends = 0;
		g_secLateMax = -1;
	}
	return out;
}

void NetTuneAfterAdvance(void *transport)
{
	if (!transport || !g_freq.QuadPart) return;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const unsigned tick = At<unsigned>(transport, kOffTick);
	if (!g_haveLast || tick != g_lastTick + 1) {
		FlushWindow();
		g_win.since = GetTickCount();
	}
	else {
		const double ms = static_cast<double>(now.QuadPart - g_last.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart);
		const unsigned interval = static_cast<unsigned>(ms + 0.5);
		const unsigned suggested = At<unsigned>(transport, kOffSuggested), delay = At<unsigned>(transport, kOffDelay);
		if (suggested < 5000 && delay < 5000 && interval < 60000) {
			g_win.intervals.push_back(interval);
			{
				std::lock_guard<std::mutex> lock(g_secMx);
				g_secCount++;
				g_secSum += interval;
				g_secMax = std::max(g_secMax, interval);
				g_secSizeSum += suggested;
				g_secDelaySum += delay;
				if (interval > suggested) g_secWait += interval - suggested;
			}
			g_win.suggestedSum += suggested;
			g_win.delaySum += delay;
			g_win.suggestedMax = std::max(g_win.suggestedMax, suggested);
			if (interval > (suggested + delay) * 3 / 2 + 30) g_win.slow++;
		}
		if (GetTickCount() - g_win.since >= 30000) {
			FlushWindow();
			g_win.since = GetTickCount();
		}
	}
	g_last = now;
	g_lastTick = tick;
	g_haveLast = true;
}
