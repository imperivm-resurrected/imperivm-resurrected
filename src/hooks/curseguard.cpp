// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>

#include "hooks/curseguard.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
typedef void(__fastcall *ClearCurseBless_t)(char *unit);
typedef char *(__fastcall *IsKindOf_t)(char *object, void *unused, const char *className);
ClearCurseBless_t g_origClear = nullptr;

const unsigned kHandle = 0x08;
const unsigned kFlags = 0x194;
const unsigned kEffect = 0x1c6;
const unsigned kFollowed = 0x70;
const uint32_t kBlessed = 0x1;
const uint32_t kCursed = 0x8000000;
const uint16_t kNoHandle = 0xffff;

bool EffectOfUnit(char *unit, uint16_t effect)
{
	char *object = reinterpret_cast<char **>(addr::kHandleTable)[effect];
	if (!object) return true;
	__try {
		char *bases = *reinterpret_cast<char **>(object + 4);
		if (!bases) return false;
		char *base = object + *reinterpret_cast<int *>(bases + 4) + 4;
		const IsKindOf_t isKindOf = reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(base))[1]);
		char *feedback = isKindOf(base, nullptr, *reinterpret_cast<const char **>(addr::kFeedbackClassName));
		if (!feedback) return true;
		return *reinterpret_cast<uint16_t *>(feedback + kHandle) == effect &&
			*reinterpret_cast<uint16_t *>(feedback + kFollowed) == *reinterpret_cast<uint16_t *>(unit + kHandle);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void __fastcall HookClearCurseBless(char *unit)
{
	if (unit) {
		uint32_t &flags = *reinterpret_cast<uint32_t *>(unit + kFlags);
		uint16_t &effect = *reinterpret_cast<uint16_t *>(unit + kEffect);
		if ((flags & (kBlessed | kCursed)) && effect != kNoHandle && !EffectOfUnit(unit, effect)) {
			LogW("game", "curse: the effect %u of unit %u is no longer its own; the curse or blessing ends without destroying it",
				static_cast<unsigned>(effect), static_cast<unsigned>(*reinterpret_cast<uint16_t *>(unit + kHandle)));
			flags &= (flags & kBlessed) ? ~kBlessed : ~kCursed;
			effect = kNoHandle;
			return;
		}
	}
	g_origClear(unit);
}
}

void InstallCurseGuard(bool knownExe)
{
	if (!knownExe) return;
	if (!HookInstall(addr::kClearCurseBless, &HookClearCurseBless, &g_origClear)) {
		LogE("game", "curse: could not hook the end of curses and blessings; a crash of the original game when they end stays possible");
		return;
	}
	LogD("game", "curse: a curse or blessing only destroys an effect that still belongs to its unit");
}
