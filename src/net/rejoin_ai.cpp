// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <map>
#include "MinHook.h"
#include "core/log.h"
#include "net/steamnet.h"
#include "addr/state.h"
#include "addr/script.h"

#include "net/rejoin_internal.h"

using namespace rejoin_impl;

namespace rejoin_impl {
namespace {
typedef void *(__fastcall *GetScriptData_t)(void *owner, void *edx, int slot);
}
}

namespace rejoin_impl {
namespace {
uint32_t AiPlayer(char *exec)
{
	char *owner = *reinterpret_cast<char **>(exec + 0x1c);
	return owner ? *reinterpret_cast<uint32_t *>(owner + 0x24) : 0xffff;
}
}
}

namespace rejoin_impl {
bool AiBusy(char *exec)
{
	char *owner = *reinterpret_cast<char **>(exec + 0x1c);
	return owner && reinterpret_cast<GetScriptData_t>(addr::kGetScriptData)(owner, nullptr, 2) != nullptr;
}

uint32_t AiOrdersHash(char *exec, uint32_t &count, uint32_t &active)
{
	uint32_t h = 2166136261u;
	auto mix = [&](const void *p, size_t n) {
		for (size_t k = 0; k < n; k++) h = (h ^ static_cast<const uint8_t *>(p)[k]) * 16777619u;
	};
	mix(exec + 0x10, 2);
	mix(exec + 0x14, 8);
	char *begin = *reinterpret_cast<char **>(exec + 4), *end = *reinterpret_cast<char **>(exec + 8);
	count = begin && end > begin ? static_cast<uint32_t>((end - begin) / 0x14) : 0;
	active = 0;
	for (uint32_t k = 0; k < count; k++) {
		char *o = begin + k * 0x14;
		mix(o, 8);
		mix(o + 0xc, 8);
		if (*reinterpret_cast<short *>(o) != 0 && *reinterpret_cast<short *>(o + 0x10) > 0) active++;
	}
	return h;
}
}

namespace rejoin_impl {
namespace {
std::deque<std::string> g_aiHist;
size_t g_aiHistBytes = 0;
std::map<uint32_t, uint32_t> g_aiHistLast;

void AiHistoryNote(char *exec, uint32_t before, uint32_t after, uint32_t count)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game) return;
	const uint32_t clock = *reinterpret_cast<uint32_t *>(game + 0x1258), player = AiPlayer(exec);
	char line[160];
	const auto last = g_aiHistLast.find(player);
	const bool moved = last != g_aiHistLast.end() && last->second != before;
	snprintf(line, sizeof(line), "t=%u ai=%u commands=%u before=%08x after=%08x%s", clock, player, count, before, after, moved ? " CHANGED-BETWEEN-THOUGHTS" : "");
	std::string s = line;
	if (moved || last == g_aiHistLast.end()) {
		char *begin = *reinterpret_cast<char **>(exec + 4);
		s += " ref=" + std::to_string(*reinterpret_cast<int *>(exec + 0x14)) + "," + std::to_string(*reinterpret_cast<int *>(exec + 0x18)) +
			" free=" + std::to_string(*reinterpret_cast<short *>(exec + 0x10)) + " |";
		for (uint32_t k = 0; k < count && k < 60 && begin; k++) {
			char hex[48];
			const unsigned char *o = reinterpret_cast<const unsigned char *>(begin + k * 0x14);
			snprintf(hex, sizeof(hex), " %02x%02x%02x%02x%02x%02x%02x%02x.%02x%02x%02x%02x%02x%02x%02x%02x", o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7],
				o[0xc], o[0xd], o[0xe], o[0xf], o[0x10], o[0x11], o[0x12], o[0x13]);
			s += hex;
		}
	}
	g_aiHistLast[player] = after;
	g_aiHistBytes += s.size() + 1;
	g_aiHist.push_back(std::move(s));
	while (g_aiHistBytes > 24u * 1024 * 1024 && !g_aiHist.empty()) {
		g_aiHistBytes -= g_aiHist.front().size() + 1;
		g_aiHist.pop_front();
	}
}
}
}

namespace rejoin_impl {
void WriteAiHistory()
{
	if (g_aiHist.empty()) return;
	EnsureLogDir();
	FILE *f = _fsopen(DiskPath(kAiHistFile).c_str(), "wb", _SH_DENYWR);
	if (!f) return;
	for (const std::string &l : g_aiHist) fprintf(f, "%s\n", l.c_str());
	fclose(f);
	LogD("rejoin", "AI history (%u thoughts) in %s", static_cast<unsigned>(g_aiHist.size()), kAiHistFile);
}

void __fastcall HookAiUpdate(void *execv)
{
	int tick;
	char *exec = static_cast<char *>(execv);
	if (!exec) {
		g_orig.aiUpdate(execv);
		return;
	}
	if (!Tracing(tick)) {
		uint32_t cnt = 0, act = 0;
		const uint32_t before = AiOrdersHash(exec, cnt, act);
		g_orig.aiUpdate(execv);
		uint32_t cnt2 = 0, act2 = 0;
		const uint32_t after = AiOrdersHash(exec, cnt2, act2);
		char *game = *reinterpret_cast<char **>(addr::kGame);
		const uint32_t clock = game ? *reinterpret_cast<uint32_t *>(game + 0x1258) : 0;
		static std::map<uint32_t, uint32_t> lastNote;
		const uint32_t player = AiPlayer(exec);
		if (before != after || clock - lastNote[player] >= 1000) {
			lastNote[player] = clock;
			AiHistoryNote(exec, before, after, cnt2);
		}
		return;
	}
	uint32_t count = 0, active = 0;
	const uint32_t hash = AiOrdersHash(exec, count, active);
	const bool busy = AiBusy(exec);
	g_orig.aiUpdate(execv);
	const bool launched = !busy && AiBusy(exec);
	{
		uint32_t cnt2 = 0, act2 = 0;
		const uint32_t after = AiOrdersHash(exec, cnt2, act2);
		AiHistoryNote(exec, hash, after, cnt2);
	}
	char *mgr = ThreadManager(0);
	TracePush({tick, 'U', static_cast<uint16_t>(AiPlayer(exec)), mgr ? *reinterpret_cast<uint32_t *>(mgr) : 0, launched ? 1u : 0u,
		count | (active << 16), hash, busy ? 1u : 0u, ThreadsHash()});
}

int __cdecl HookAiAddOrder(void *execv, short prio, short type, unsigned short a, char *b, unsigned char flag)
{
	int tick;
	char *exec = static_cast<char *>(execv);
	if (exec && Tracing(tick)) {
		char *mgr = ThreadManager(0);
		const uint32_t bv = type == 2 ? 0 : (reinterpret_cast<uintptr_t>(b) & 0xffff);
		TracePush({tick, 'O', static_cast<uint16_t>(AiPlayer(exec)), mgr ? *reinterpret_cast<uint32_t *>(mgr) : 0,
			static_cast<uint32_t>(static_cast<int>(prio)), static_cast<uint32_t>(static_cast<int>(type)), a | (bv << 16),
			static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress())), ThreadsHash()});
	}
	return g_orig.aiAddOrder(execv, prio, type, a, b, flag);
}
}
