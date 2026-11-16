// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "core/log.h"
#include "hooks/gamestate.h"
#include "hooks/hookcount.h"
#include "game/diplomacy.h"
#include "game/diplomacyrules.h"
#include "game/gamelayout.h"
#include "game/gamemenu.h"
#include "game/gameroom.h"
#include "mods/modoptions.h"

#include <cstdint>
#include <string>
#include <vector>

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

namespace {

// CVXUIDiplomacyMenu: +0x60 the listed players, +0x7c how many. Row r (1 to 7) has its controls at r * 0x10 + 2..5:
// ceasefire, vision, support and control, in that order; the toggle's state (+0x7e) is 0 when the permission is given.
const size_t kDlgPlayers = 0x60, kDlgCount = 0x7c, kToggleState = 0x7e;
const int kMaxRows = 7, kAlliedVictoryId = 0x100a, kOkId = 0x1000, kCancelId = 0x1001;
const unsigned kRowBits[4] = {diplomacy::kCeasefire, diplomacy::kVision, diplomacy::kSupport, diplomacy::kControl};

typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *SetState_t)(void *button, void *edx, int state, int redraw);

void *Toggle(void *dlg, int id)
{
	return reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(dlg))[0x7c / 4])(dlg, nullptr, id,
		*reinterpret_cast<const char **>(addr::kClassNameCUIButton));
}

int ToggleState(void *button)
{
	return *reinterpret_cast<short *>(static_cast<char *>(button) + kToggleState);
}

void SetToggleState(void *button, int state)
{
	if (ToggleState(button) == state) return;
	reinterpret_cast<SetState_t>((*reinterpret_cast<void ***>(button))[0x70 / 4])(button, nullptr, state, 1);
}

struct RawRow {
	int player;
	char name[65];
	unsigned color, theirs, mine;
	bool ai;
	int team;
};

struct RawDialog {
	RawRow rows[kMaxRows];
	int count;
	bool alliedVictory;
};

bool ReadRaw(void *dlg, RawDialog &out)
{
	__try {
		const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
		if (!game) return false;
		const char *local = layout::LocalPlayer(game);
		if (!local) return false;
		const int ours = Field<int>(local, layout::kPlayerId);
		const char *settings = *reinterpret_cast<const char *const *>(addr::kGameSettings);
		const int count = *reinterpret_cast<const int *>(static_cast<char *>(dlg) + kDlgCount);
		for (int i = 0; i < count && i < kMaxRows; i++) {
			const int index = *reinterpret_cast<const int *>(static_cast<char *>(dlg) + kDlgPlayers + i * 4);
			if (index < 0 || index >= layout::kPlayerCount || ours < 0 || ours >= layout::kPlayerCount) continue;
			RawRow &row = out.rows[out.count++];
			row.player = index;
			const char *player = layout::PlayerAt(game, index);
			row.theirs = Field<unsigned>(player, layout::kPlayerDiplomacy + ours * 4) & diplomacy::kAllFlags;
			for (int b = 0; b < 4; b++) {
				void *button = Toggle(dlg, (i + 1) * 0x10 + 2 + b);
				if (button && ToggleState(button) == 0) row.mine |= kRowBits[b];
			}
			const char *name = layout::PlayerName(player);
			for (int c = 0; name && c < 64 && name[c]; c++) row.name[c] = name[c];
			row.color = Field<unsigned>(player, layout::kPlayerColor);
			if (settings && index < layout::kSettingsEntryCount) {
				row.ai = Field<int>(layout::SettingsEntry(settings, index), layout::kEntryType) == layout::kEntryTypeAi;
				const unsigned char team = Field<unsigned char>(settings, layout::kSettingsTeams + index);
				row.team = team >= 1 && team <= 8 ? team : 0;
			}
		}
		void *allied = Toggle(dlg, kAlliedVictoryId);
		out.alliedVictory = allied && ToggleState(allied) == 1;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

}

bool DiplomacyDialogRead(DiplomacyDialogState &out)
{
	out = DiplomacyDialogState();
	void *dlg = *reinterpret_cast<void **>(addr::kDiplomacyDlg);
	if (!dlg) return false;
	out.dlg = dlg;
	out.locked = (ModRulesActive() & kRuleLockDiplomacy) != 0;
	RawDialog raw = {};
	if (!ReadRaw(dlg, raw)) return false;
	out.alliedVictory = raw.alliedVictory;
	for (int i = 0; i < raw.count; i++) {
		const RawRow &r = raw.rows[i];
		DiplomacyRow row;
		row.player = r.player;
		row.name = r.name;
		row.rgb = Rgb555ToRgb(r.color);
		row.ai = r.ai;
		row.team = r.team;
		row.mine = r.mine;
		row.theirs = r.theirs;
		out.rows.push_back(row);
	}
	return !out.rows.empty();
}

void DiplomacyDialogSetMine(void *dlg, int row, unsigned flags)
{
	if (dlg != *reinterpret_cast<void **>(addr::kDiplomacyDlg) || row < 0 || row >= kMaxRows) return;
	for (int b = 0; b < 4; b++) {
		void *button = Toggle(dlg, (row + 1) * 0x10 + 2 + b);
		if (button) SetToggleState(button, (flags & kRowBits[b]) ? 0 : 1);
	}
}

void DiplomacyDialogSetAlliedVictory(void *dlg, bool on)
{
	if (dlg != *reinterpret_cast<void **>(addr::kDiplomacyDlg)) return;
	void *button = Toggle(dlg, kAlliedVictoryId);
	if (button) SetToggleState(button, on ? 1 : 0);
}

void DiplomacyDialogApply(void *dlg)
{
	if (dlg == *reinterpret_cast<void **>(addr::kDiplomacyDlg)) NativeButtonClick(dlg, kOkId);
}

void DiplomacyDialogCancel(void *dlg)
{
	if (dlg == *reinterpret_cast<void **>(addr::kDiplomacyDlg)) NativeButtonClick(dlg, kCancelId);
}
