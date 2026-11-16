// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <cstdint>

#include "hooks/hookcount.h"
#include "core/crashlog.h"
#include "hooks/diag.h"
#include "core/log.h"
#include "hooks/tweaks.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
typedef void(__fastcall *SetPaused_t)(void *game, void *edx, int paused);
SetPaused_t g_origSetPaused = nullptr;
int g_logged = 0;

static bool ReadClassId(const char *cls, char *out, size_t size)
{
	const unsigned n = *reinterpret_cast<const unsigned *>(cls + 0x18);
	const unsigned cap = *reinterpret_cast<const unsigned *>(cls + 0x1c);
	const char *text = cap < 0x10 ? cls + 8 : *reinterpret_cast<const char *const *>(cls + 8);
	if (n == 0 || n >= size) return false;
	memcpy(out, text, n);
	out[n] = 0;
	return true;
}

static bool ModOutpostAt(unsigned handle, char *id, size_t size, int *x, int *y)
{
	__try {
		char *obj = reinterpret_cast<char **>(addr::kHandleTable)[handle];
		if (!obj) return false;
		const char *cls = *reinterpret_cast<const char **>(obj + 0x3c);
		if (!cls || !ReadClassId(cls, id, size)) return false;
		if (!(strstr(id, "Outpost") || strstr(id, "Fort"))) return false;
		if (strlen(id) == 8 && strcmp(id + 1, "Outpost") == 0 && strchr("GRCIMBET", id[0])) return false;
		void **vt = *reinterpret_cast<void ***>(obj);
		int buf[4] = {};
		int *p = reinterpret_cast<int *(__fastcall *)(void *, void *, int *)>(vt[0x3c / 4])(obj, nullptr, buf);
		if (!p) return false;
		*x = p[0];
		*y = p[1];
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static void LogModOutposts()
{
	const char *map = *reinterpret_cast<char **>(addr::kMap);
	if (!map) return;
	const int x0 = *reinterpret_cast<const int *>(map + 0xb4), y0 = *reinterpret_cast<const int *>(map + 0xb8);
	const int x1 = *reinterpret_cast<const int *>(map + 0xbc), y1 = *reinterpret_cast<const int *>(map + 0xc0);
	if (x1 <= x0 || y1 <= y0) return;
	int found = 0;
	g_crashLogQuiet++;
	for (unsigned h = 0; h < 0x10000 && found < 40; h++) {
		char id[64];
		int x, y;
		if (!ModOutpostAt(h, id, sizeof(id), &x, &y)) continue;
		found++;
		LogD("game", "diagnostics: mod outpost '%s' on the map: %d%% from the left, %d%% from the top (x=%d y=%d)", id, (x - x0) * 100 / (x1 - x0),
			(y - y0) * 100 / (y1 - y0), x, y);
	}
	g_crashLogQuiet--;
	if (!found) LogD("game", "diagnostics: no mod outposts on the map");
}

void __fastcall HookSetPaused(void *game, void *edx, int paused)
{
	const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
	const bool fromGame = ret >= addr::kGameCodeBegin && ret < addr::kGameCodeEnd;
	if (ret == addr::kAdvanceTickCaller && TweaksOnlyHumanOnline(game)) {
		static int ignored = 0;
		if (ignored++ < 10) {
			const char *t = *reinterpret_cast<char **>(addr::kTransport);
			const int req = t ? *reinterpret_cast<int *>(const_cast<char *>(t) + 0x3f8) : -1, last = t ? *reinterpret_cast<int *>(const_cast<char *>(t) + 0x68) : -1;
			const int p90 = t ? *reinterpret_cast<int *>(const_cast<char *>(t) + 0x90) : -1, p94 = t ? *reinterpret_cast<int *>(const_cast<char *>(t) + 0x94) : -1;
			LogW("game", "pause: the network wanted to toggle the pause (to SetPaused(%d)); ignored because you are the only human in the game. Transport: request of this tick %d, of the previous %d, +0x90 %d, +0x94 %d", paused, req, last, p90, p94);
		}
		return;
	}
	if (g_logged < 40) {
		g_logged++;
		const int was = game ? *reinterpret_cast<int *>(static_cast<char *>(game) + 0x1240) : -1;
		if (ret >= addr::kGameCodeBegin && ret < addr::kGameCodeEnd) LogD("game", "diagnostics: SetPaused(%d) (was %d) is called from 0x%08x of the game", paused, was, static_cast<unsigned>(ret));
		else LogD("game", "diagnostics: SetPaused(%d) (was %d) is called from 0x%08x (not the game's: the DLL or another)", paused, was, static_cast<unsigned>(ret));
	}
	g_origSetPaused(game, edx, paused);
	static DWORD outpostsLast = 0;
	if (!paused && fromGame && GetTickCount() - outpostsLast > 20000) {
		outpostsLast = GetTickCount();
		LogModOutposts();
	}
}

typedef const char *(__cdecl *CreateEntityImage_t)(const char *path, int a, int b, int c, void **out);
CreateEntityImage_t g_origCreateEntityImage = nullptr;

const char *__cdecl HookCreateEntityImage(const char *path, int a, int b, int c, void **out)
{
	const char *err = g_origCreateEntityImage(path, a, b, c, out);
	static int logged = 0;
	if (err && logged++ < 20) {
		char what[80];
		if (reinterpret_cast<uintptr_t>(err) > 0x10000 && !IsBadStringPtrA(err, 64)) snprintf(what, sizeof(what), "%.60s", err);
		else snprintf(what, sizeof(what), "code %p", static_cast<const void *>(err));
		LogW("game", "diagnostics: could not load image '%s' of an entity (%s). If the game closes now, this is why: the .rle or .bmp file with that name is missing in the mod",
			path ? path : "?", what);
	}
	return err;
}
const char *(*g_classNameMapper)(void *registry, const char *name, std::string &buffer) = nullptr;
typedef void *(__fastcall *CreateObject_t)(void *self, void *edx, const char *name, void **out);
CreateObject_t g_origCreateObject = nullptr;

void *__fastcall HookCreateObject(void *self, void *edx, const char *name, void **out)
{
	const DWORD t0 = GetTickCount();
	std::string translated;
	const char *actual = name && g_classNameMapper ? g_classNameMapper(self, name, translated) : name;
	void *r = g_origCreateObject(self, edx, actual, out);
	const DWORD took = GetTickCount() - t0;
	static int slow = 0;
	if (took >= 40 && slow++ < 30) LogD("script", "creating an object of class '%s' took %lu ms (model loading the first time)", name ? name : "?", took);
	static std::map<std::string, int> forts;
	static int fortTotal = 0;
	if (name && (strstr(name, "Outpost") || strstr(name, "Fort"))) {
		forts[name]++;
		if (out && *out == nullptr) LogW("game", "diagnostics: cannot create the outpost '%s'", name);
		if (++fortTotal % 10 == 0 && fortTotal <= 400) {
			std::string all;
			for (const auto &kv : forts) all += " " + kv.first + "=" + std::to_string(kv.second);
			LogD("game", "diagnostics: outposts created (%d):%s", fortTotal, all.c_str());
		}
	}
	if (out && *out == nullptr) {
		static std::set<std::string> seen;
		static int count = 0;
		if (name && count < 100 && seen.insert(name).second) {
			count++;
			LogW("script", "could not create an object of class '%s' (that class does not exist or cannot be created)", name);
		}
	}
	return r;
}

}

void DiagSetClassNameMapper(const char *(*mapper)(void *registry, const char *name, std::string &buffer))
{
	g_classNameMapper = mapper;
}

void InstallDiag(bool knownExe)
{
	if (!knownExe) return;
	if (!HookInstall(addr::kSetPaused, &HookSetPaused, &g_origSetPaused)) {
		LogE("game", "diagnostics: could not hook SetPaused");
	}
	if (!HookInstall(addr::kCreateEntityImage, &HookCreateEntityImage, &g_origCreateEntityImage)) {
		LogE("game", "diagnostics: could not hook CreateEntityImage");
	}
	if (!HookInstall(addr::kCreateObject, &HookCreateObject, &g_origCreateObject)) {
		LogE("game", "diagnostics: could not hook CreateObject");
	}
}
