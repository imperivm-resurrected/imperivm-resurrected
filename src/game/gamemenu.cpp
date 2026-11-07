// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/gamemenu.h"

#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "core/log.h"
#include "hooks/hookcount.h"

namespace {
const unsigned kMsgButtonClick = 0x12345604, kFlagVisible = 2, kFlagDisabled = 0x200;

typedef int(__fastcall *IsKindOf_t)(void *obj, void *edx, const char *cls);
typedef bool(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);
typedef int(__fastcall *ConfirmSetup_t)(void *dlg, void *edx, const char *text, void *owner);

ConfirmSetup_t g_origConfirmSetup = nullptr;
void *g_confirm = nullptr;
std::string g_confirmText;

int __fastcall HookConfirmSetup(void *dlg, void *edx, const char *text, void *owner)
{
	g_confirm = dlg;
	g_confirmText = text ? text : "";
	return g_origConfirmSetup(dlg, edx, text, owner);
}

bool IsVisibleGameMenu(char *w)
{
	__try {
		char *obj = w + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(w + 4) + 4);
		const char *cls = *reinterpret_cast<const char *const *>(addr::kClassNameCVXUIGameMenu);
		if (reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(obj))[1])(obj, nullptr, cls) == 0) return false;
		return reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(w, nullptr, kFlagVisible);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void *FindButton(void *dlg, int id)
{
	return reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(dlg))[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCUIButton));
}
}

void InstallGameMenuHooks(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kConfirmSetup);
	if (HookCreate(target, reinterpret_cast<void *>(&HookConfirmSetup), reinterpret_cast<void **>(&g_origConfirmSetup)) != MH_OK || HookEnable(target) != MH_OK) {
		LogE("ui", "could not hook the confirmation dialog");
	}
}

void *GameMenuVisible()
{
	char *root = *reinterpret_cast<char **>(addr::kUIRoot);
	if (!root) return nullptr;
	int guard = 0;
	for (char *w = *reinterpret_cast<char **>(root + 0x20); w && guard < 64; w = *reinterpret_cast<char **>(w + 0x18), guard++) {
		if (IsVisibleGameMenu(w)) return w;
	}
	return nullptr;
}

void *ConfirmOpen()
{
	if (!g_origConfirmSetup) return nullptr;
	if (!*reinterpret_cast<void **>(addr::kConfirmDlg)) g_confirm = nullptr;
	return g_confirm;
}

std::string ConfirmText()
{
	return g_confirmText;
}

bool NativeButtonEnabled(void *dlg, int id, bool &enabled)
{
	void *button = FindButton(dlg, id);
	if (!button) return false;
	enabled = !reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(button, nullptr, kFlagDisabled);
	return true;
}

void NativeButtonClick(void *dlg, int id)
{
	void *button = FindButton(dlg, id);
	if (!button) return;
	char *b = static_cast<char *>(button);
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(button))[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgButtonClick,
		*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}
