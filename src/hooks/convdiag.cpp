// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hooks/convdiag.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "addr/script.h"
#include "addr/world.h"

// Diagnostic only (not for main): logs what CVXConversationWnd::ShowPhrase is about to draw for the current phrase.
namespace {
typedef void(__fastcall *ShowPhrase_t)(int *wnd);
ShowPhrase_t g_orig = nullptr;
typedef void(__fastcall *HeroSerialize_t)(char *hero, void *unused, int *stream);
HeroSerialize_t g_origHero = nullptr;
void Text(const char *s, char *out, size_t size);

void DescribeHero(char *hero, int *stream)
{
	__try {
		const unsigned short handle = *reinterpret_cast<unsigned short *>(hero + 8);
		char *slot = reinterpret_cast<char **>(addr::kHandleTable)[handle];
		const uint32_t flags = *reinterpret_cast<uint32_t *>(hero + 0x2c);
		const char *type = *reinterpret_cast<const char **>(hero + 0x3c);
		char name[48] = "?";
		if (type) Text(type + 4, name, sizeof(name));
		LogI("game", "herodiag: %s hero=%08x handle=%u slot=%08x%s class=%s flags=%08x vtable=%08x stream=%08x",
			stream && stream[4] == 1 ? "load" : "save", reinterpret_cast<uintptr_t>(hero), handle, reinterpret_cast<uintptr_t>(slot),
			slot == hero ? "" : " (NOT THIS)", name, flags, *reinterpret_cast<uint32_t *>(hero), reinterpret_cast<uintptr_t>(stream));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogW("game", "herodiag: could not read hero %08x", reinterpret_cast<uintptr_t>(hero));
	}
}

void __fastcall HookHero(char *hero, void *unused, int *stream)
{
	DescribeHero(hero, stream);
	g_origHero(hero, unused, stream);
}

void Text(const char *s, char *out, size_t size)
{
	const uint32_t len = *reinterpret_cast<const uint32_t *>(s + 0x14), cap = *reinterpret_cast<const uint32_t *>(s + 0x18);
	const char *p = cap < 16 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	const size_t n = len < size - 1 ? len : size - 1;
	memcpy(out, p, n);
	out[n] = 0;
}

void Describe(int *w)
{
	__try {
		int cur = w[0x5e];
		if (cur == -1 && w[0x61] && w[0x62] != w[0x61]) cur = *reinterpret_cast<int *>(w[0x61]);
		const int actor = cur >= 0 ? *reinterpret_cast<int *>(cur * 0x104 + w[0x41] + 0x54) : -2;
		const int count = w[0x3d] ? (w[0x3e] - w[0x3d]) / 0x1c : 0;
		LogI("game", "convdiag: phrase %d actor index %d of %d actors", cur, actor, count);
		for (int i = 0; i < count && i < 8; i++) {
			char name[64], shown[64];
			Text(reinterpret_cast<const char *>(w[0x3d] + i * 0x1c), name, sizeof(name));
			shown[0] = 0;
			if (w[0x4f]) Text(reinterpret_cast<const char *>(w[0x4f] + i * 0x1c), shown, sizeof(shown));
			const int unit = w[0x47] ? reinterpret_cast<int *>(w[0x47])[i] : -1;
			const int icon = w[0x4b] ? reinterpret_cast<int *>(w[0x4b])[i] : -1;
			int handle = -1, own = -1, ownImage = -1, type = -1, typeIcon = -1;
			if (unit > 0) {
				handle = *reinterpret_cast<uint16_t *>(unit + 8);
				own = *reinterpret_cast<int *>(unit + 0x190);
				if (own) ownImage = *reinterpret_cast<int *>(own + 0x1c);
				type = *reinterpret_cast<int *>(unit + 0x3c);
				if (type) typeIcon = *reinterpret_cast<int *>(type + 0x704);
			}
			LogI("game", "convdiag:  actor %d name='%s' shown='%s' unit=%08x handle=%d icon=%08x unit190=%08x unit190img=%08x type=%08x typeicon=%08x",
				i, name, shown, unit, handle, icon, own, ownImage, type, typeIcon);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		LogW("game", "convdiag: could not read the conversation window");
	}
}

void __fastcall Hook(int *wnd)
{
	Describe(wnd);
	g_orig(wnd);
	__try {
		LogI("game", "convdiag: drawn picture=%08x", wnd[0x1a]);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
	}
}
}

void InstallConversationDiag(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kConversationShowPhrase);
	if (HookCreate(target, reinterpret_cast<void *>(&Hook), reinterpret_cast<void **>(&g_orig)) != MH_OK || HookEnable(target) != MH_OK) {
		LogE("game", "convdiag: could not hook ShowPhrase");
		return;
	}
	LogI("game", "convdiag: logging every conversation phrase");
	void *hero = reinterpret_cast<void *>(addr::kHeroSerialize);
	if (HookCreate(hero, reinterpret_cast<void *>(&HookHero), reinterpret_cast<void **>(&g_origHero)) != MH_OK || HookEnable(hero) != MH_OK)
		LogE("game", "herodiag: could not hook CVXHero::Serialize");
}
