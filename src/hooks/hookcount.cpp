// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/hookcount.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"

namespace {

std::mutex g_lock;
int g_created = 0;
int g_enabled = 0;
std::vector<std::string> g_failed;
bool g_batch = false;
std::vector<LPVOID> g_queued;
DWORD g_applyMs = 0;

void Fail(const char *what, LPVOID target, MH_STATUS status)
{
	char text[80];
	sprintf_s(text, "%s 0x%08x (%s)", what, static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)), MH_StatusToString(status));
	std::lock_guard<std::mutex> lock(g_lock);
	g_failed.push_back(text);
}

}

MH_STATUS HookCreate(LPVOID target, LPVOID detour, LPVOID *original)
{
	const MH_STATUS status = MH_CreateHook(target, detour, original);
	if (status == MH_OK) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_created++;
	}
	else {
		Fail("create", target, status);
	}
	return status;
}

MH_STATUS HookEnable(LPVOID target)
{
	if (g_batch) {
		const MH_STATUS queued = MH_QueueEnableHook(target);
		if (queued == MH_OK) g_queued.push_back(target);
		else Fail("enable", target, queued);
		return queued;
	}
	const MH_STATUS status = MH_EnableHook(target);
	if (status == MH_OK) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_enabled++;
	}
	else {
		Fail("enable", target, status);
	}
	return status;
}

bool HookInstall(LPVOID target, LPVOID detour, LPVOID *original)
{
	return HookCreate(target, detour, original) == MH_OK && HookEnable(target) == MH_OK;
}

void HookBatchBegin()
{
	g_batch = true;
}

void HookBatchEnd()
{
	g_batch = false;
	const DWORD start = GetTickCount();
	if (MH_ApplyQueued() == MH_OK) {
		g_enabled += static_cast<int>(g_queued.size());
	}
	else {
		for (LPVOID target : g_queued) {
			const MH_STATUS status = MH_EnableHook(target);
			if (status == MH_OK || status == MH_ERROR_ENABLED) g_enabled++;
			else Fail("enable", target, status);
		}
	}
	g_queued.clear();
	g_applyMs = GetTickCount() - start;
}

void LogHookSummary()
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::string failed;
	for (const std::string &f : g_failed) failed += (failed.empty() ? "" : ", ") + f;
	LogAt(g_failed.empty() ? kLogInfo : kLogError, "boot", "hooks: %d created, %d enabled in %lu ms, %u failed%s%s", g_created, g_enabled, g_applyMs,
		static_cast<unsigned>(g_failed.size()), failed.empty() ? "" : ": ", failed.c_str());
}
