// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdint>

#include "core/crashlog.h"
#include "core/log.h"

thread_local int g_crashLogQuiet = 0;

namespace {

LONG g_logged = 0;
const LONG kMaxLogged = 4;

bool DescribeAddress(uintptr_t addr, char *out, size_t size)
{
	HMODULE mod = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(addr), &mod) || !mod) {
		return false;
	}
	char path[MAX_PATH] = {};
	GetModuleFileNameA(mod, path, MAX_PATH);
	const char *name = strrchr(path, '\\');
	sprintf_s(out, size, "%s+%x", name ? name + 1 : path, static_cast<unsigned>(addr - reinterpret_cast<uintptr_t>(mod)));
	return true;
}

bool IsCodeAddress(uintptr_t addr)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(reinterpret_cast<void *>(addr), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
	return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

void LogContext(const char *tag, const CONTEXT *c, LogLevel level)
{
	LogAt(level, "crash", "%s registers: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", tag, c->Eax, c->Ebx, c->Ecx, c->Edx,
		c->Esi, c->Edi, c->Ebp, c->Esp);
	const uintptr_t *sp = reinterpret_cast<const uintptr_t *>(c->Esp);
	MEMORY_BASIC_INFORMATION mbi;
	uintptr_t stackEnd = 0;
	if (VirtualQuery(sp, &mbi, sizeof(mbi))) stackEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	int shown = 0;
	char line[1024] = "";
	for (int k = 0; k < 2048 && reinterpret_cast<uintptr_t>(sp + k + 1) <= stackEnd && shown < 48; k++) {
		const uintptr_t v = sp[k];
		if (v < 0x10000 || !IsCodeAddress(v)) continue;
		char d[160];
		if (!DescribeAddress(v, d, sizeof(d))) continue;
		char item[200];
		sprintf_s(item, " [+%x]%s", k * 4, d);
		if (strlen(line) + strlen(item) > 900) {
			LogAt(level, "crash", "%s stack:%s", tag, line);
			line[0] = 0;
		}
		strcat_s(line, item);
		shown++;
	}
	if (line[0]) LogAt(level, "crash", "%s stack:%s", tag, line);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS *ep)
{
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code == 0xE06D7363 && g_crashLogQuiet == 0) {
		static LONG cpp = 0;
		if (InterlockedIncrement(&cpp) <= 8) {
			g_crashLogQuiet++;
			char where[160] = "?";
			DescribeAddress(reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress), where, sizeof(where));
			LogW("crash", "C++ exception thrown in %s; thread %lu", where, GetCurrentThreadId());
			g_crashLogQuiet--;
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
		code != 0xC0000374 && code != EXCEPTION_PRIV_INSTRUCTION) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if (g_crashLogQuiet > 0) return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedIncrement(&g_logged) > kMaxLogged) return EXCEPTION_CONTINUE_SEARCH;
	const CONTEXT *c = ep->ContextRecord;
	char where[160] = "?";
	DescribeAddress(c->Eip, where, sizeof(where));
	const ULONG_PTR *info = ep->ExceptionRecord->ExceptionInformation;
	LogF("crash", "CRASH %08lx at %08lx (%s)%s%08lx; thread %lu", code, static_cast<unsigned long>(c->Eip), where,
		code == EXCEPTION_ACCESS_VIOLATION ? (info[0] == 1 ? ", writing to " : info[0] == 8 ? ", executing " : ", reading ") : ", data",
		static_cast<unsigned long>(code == EXCEPTION_ACCESS_VIOLATION ? info[1] : 0), GetCurrentThreadId());
	LogContext("CRASH", c, kLogFatal);
	return EXCEPTION_CONTINUE_SEARCH;
}

}

namespace {

HANDLE g_mainThread = nullptr;
std::atomic<DWORD> g_loadingSince{0};
const int kHangChecks = 3, kHangChecksLoading = 24;
const DWORD kBootGraceMs = 90000;
DWORD g_bootTick = 0;

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM param)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
	*reinterpret_cast<HWND *>(param) = hwnd;
	return FALSE;
}

DWORD WINAPI Watchdog(void *)
{
	int hung = 0, logged = 0;
	bool reported = false;
	for (;;) {
		Sleep(1000);
		HWND wnd = nullptr;
		EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&wnd));
		if (!wnd) continue;
		DWORD_PTR result = 0;
		if (SendMessageTimeoutA(wnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &result)) {
			hung = 0;
			reported = false;
			continue;
		}
		const bool starting = GetTickCount() - g_bootTick < kBootGraceMs;
		const int limit = g_loadingSince.load() || starting ? kHangChecksLoading : kHangChecks;
		if (++hung < limit || reported || logged >= 3) continue;
		reported = true;
		logged++;
		CONTEXT c = {};
		c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		if (SuspendThread(g_mainThread) == static_cast<DWORD>(-1)) continue;
		const bool ok = GetThreadContext(g_mainThread, &c) != 0;
		if (ok) {
			char where[160] = "?";
			DescribeAddress(c.Eip, where, sizeof(where));
			g_crashLogQuiet++;
			LogE("crash", "HANG: the main thread has not responded for a few seconds; it is at %08lx (%s)", static_cast<unsigned long>(c.Eip), where);
			LogContext("HANG", &c, kLogError);
			g_crashLogQuiet--;
		}
		ResumeThread(g_mainThread);
	}
}

}

void CrashLogLoadingBegin()
{
	g_loadingSince = GetTickCount() | 1;
}

bool CrashLogLoadingEnd()
{
	const DWORD since = g_loadingSince.exchange(0);
	if (!since) return false;
	LogI("game", "game load took %.1f s", static_cast<double>(GetTickCount() - since) / 1000.0);
	return true;
}

void InstallCrashLog()
{
	g_bootTick = GetTickCount();
	AddVectoredExceptionHandler(1, Handler);
	ULONG guarantee = 64 * 1024;
	SetThreadStackGuarantee(&guarantee);
	if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0)) {
		CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
	}
}
