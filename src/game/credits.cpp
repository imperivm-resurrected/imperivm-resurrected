// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/credits.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "addr/ui.h"
#include "core/log.h"
#include "game/gamehook.h"
#include "mods/vfs.h"

namespace {

// menuini/CreditsMenu.ini: the Close button.
const int kIdBack = 0x1001;
const unsigned kMsgButtonClick = 0x12345604;
// CVXUICreditsMenu: +0x64 PixPerSec, +0x6c the scrolling text (null when InitCredits failed), +0x70 1 while its scrolling is paused.
const int kDlgSpeed = 0x64, kDlgText = 0x6c, kDlgPaused = 0x70;
const int kDefaultSpeed = 100, kMinSpeed = 20, kMaxSpeed = 400;

typedef void(__fastcall *OnShow_t)(void *dlg);
typedef void *(__fastcall *DeletingDtor_t)(void *self, void *edx, unsigned flags);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);

std::atomic<void *> g_dialog{nullptr};
OnShow_t g_origOnShow = nullptr;
DeletingDtor_t g_origDtor = nullptr;

void __fastcall HookOnShow(void *dlg)
{
	g_origOnShow(dlg);
	char *d = static_cast<char *>(dlg);
	// Without its text the game goes back to the main menu by itself.
	if (!*reinterpret_cast<void **>(d + kDlgText)) {
		LogW("ui", "the game's credits screen has no text");
		return;
	}
	// As a click on its text does: the mod's screen covers it, and a still menu does not repaint under it.
	*reinterpret_cast<int *>(d + kDlgPaused) = 1;
	if (g_dialog.exchange(dlg) != dlg) LogI("ui", "the credits screen opens");
}

// CVXUIMenu's deleting destructor, which the credits screen shares with the other menus.
void *__fastcall HookDeletingDtor(void *self, void *edx, unsigned flags)
{
	void *expected = self;
	g_dialog.compare_exchange_strong(expected, nullptr);
	return g_origDtor(self, edx, flags);
}

bool HookAt(uintptr_t at, void *detour, void **orig, const char *what)
{
	if (HookFunction(at, detour, orig)) return true;
	LogE("ui", "could not hook %s", what);
	return false;
}

}

void *CreditsDialogOpen()
{
	return g_dialog;
}

int CreditsDialogSpeed()
{
	void *dlg = g_dialog;
	const int speed = dlg ? *reinterpret_cast<int *>(static_cast<char *>(dlg) + kDlgSpeed) : 0;
	return speed >= kMinSpeed && speed <= kMaxSpeed ? speed : kDefaultSpeed;
}

bool CreditsGameText(std::string &out)
{
	const bool ok = VfsReadUnpatched("currentlang/credits.txt", out);
	if (!ok) LogW("ui", "could not read the game's credits text");
	return ok;
}

bool CreditsDialogClose()
{
	void *dlg = g_dialog;
	if (!dlg) return false;
	void **vt = *reinterpret_cast<void ***>(dlg);
	char *button = static_cast<char *>(reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, kIdBack, *reinterpret_cast<const char **>(addr::kClassNameCUIButton)));
	if (!button) {
		LogW("ui", "the credits screen has no Close button");
		return false;
	}
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(button))[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(button + 0x34), kMsgButtonClick,
		*reinterpret_cast<int *>(button + 0x30), *reinterpret_cast<short *>(button + 0x7e), 0);
	LogI("ui", "the credits screen closes");
	return true;
}

void InstallCreditsHooks(bool knownExe)
{
	if (!knownExe) return;
	// The destructor first: without it the mod's screen would not know when to go.
	const bool ok = HookAt(addr::kMenuDeletingDtor, reinterpret_cast<void *>(&HookDeletingDtor), reinterpret_cast<void **>(&g_origDtor), "CVXUIMenu deleting destructor") &&
		HookAt(addr::kCreditsOnShow, reinterpret_cast<void *>(&HookOnShow), reinterpret_cast<void **>(&g_origOnShow), "CVXUICreditsMenu::OnShow");
	LogAt(ok ? kLogInfo : kLogError, "ui", "credits screen %s", ok ? "connected to the mod's" : "not connected");
}
