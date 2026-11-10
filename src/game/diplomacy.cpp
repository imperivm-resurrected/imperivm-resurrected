// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "core/log.h"
#include "hooks/gamestate.h"
#include "hooks/hookcount.h"
#include "game/diplomacy.h"

#include <cstdint>

#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"

namespace {

const int kDiplomacyButton = 0x1005;
const unsigned kFlagDisabled = 0x200;
// CMapHeader.game_type: 0 scenario, 1 adventure, 2 conquest. ShowDiplomacyDialog only opens in a scenario.
const size_t kGameMapType = 0xa0;

typedef int(__fastcall *OnShow_t)(void *menu);
OnShow_t g_origEmptyMenuOnShow = nullptr;

bool GameSpyLocksDiplomacy()
{
	// The same GameSpy test (vtable +0x74) that ShowDiplomacyDialog and CVXUIEmptySelectionMenu::OnShow make.
	void *gs = *reinterpret_cast<void **>(addr::kGameSpyManager);
	if (!gs) return false;
	typedef int(__fastcall * Test_t)(void *self);
	return reinterpret_cast<Test_t>((*reinterpret_cast<void ***>(gs))[0x74 / 4])(gs) != 0;
}

bool SeveralHumans()
{
	return reinterpret_cast<bool(__cdecl *)()>(addr::kMultiHumans)();
}

int __fastcall HookEmptyMenuOnShow(void *menu)
{
	const int result = g_origEmptyMenuOnShow(menu);
	if (SeveralHumans() || !DiplomacyAvailable()) return result;
	typedef void *(__fastcall * GetChild_t)(void *dlg, void *edx, int id, const char *cls);
	void *button = reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(menu))[0x7c / 4])(menu, nullptr, kDiplomacyButton,
		*reinterpret_cast<const char **>(addr::kClassNameCWindow));
	if (!button) return result;
	typedef void(__fastcall * ModifyFlags_t)(void *wnd, void *edx, unsigned set, unsigned clear, int redraw);
	reinterpret_cast<ModifyFlags_t>(addr::kModifyFlags)(button, nullptr, 0, kFlagDisabled, 1);
	static bool logged = false;
	if (!logged) {
		logged = true;
		LogI("game", "diplomacy: enabled in a match with one human");
	}
	return result;
}

}

bool DiplomacyAvailable()
{
	const char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || !GameIsMultiplayer() || *reinterpret_cast<const int *>(addr::kEditorMode)) return false;
	if (*reinterpret_cast<const int *>(game + kGameMapType) != 0 || reinterpret_cast<int(__cdecl *)()>(addr::kIsDemoPlayback)()) return false;
	return !GameSpyLocksDiplomacy();
}

void DiplomacyKeyDown()
{
	// CSelectionManager::OnKey opens it on F5 only with several humans; with them the game already did it.
	if (SeveralHumans() || *reinterpret_cast<void **>(addr::kIngameChatDialog) || *reinterpret_cast<void **>(addr::kDiplomacyDlg)) return;
	if (!DiplomacyAvailable()) return;
	reinterpret_cast<void(__fastcall *)(int)>(addr::kShowDiplomacyDialog)(0);
}

void InstallDiplomacy(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kEmptyMenuOnShow);
	const bool ok = HookCreate(target, reinterpret_cast<void *>(&HookEmptyMenuOnShow), reinterpret_cast<void **>(&g_origEmptyMenuOnShow)) == MH_OK &&
		HookEnable(target) == MH_OK;
	if (ok) LogI("game", "diplomacy against the AI: hooked");
	else LogE("game", "diplomacy against the AI: could not hook CVXUIEmptySelectionMenu::OnShow");
}
