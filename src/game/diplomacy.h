// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The game only offers diplomacy when several humans play; the mod also offers it in a match against the AI.
bool DiplomacyAvailable();

// Called after the game handled a key down that ended up as F5: opens diplomacy where the game would not.
void DiplomacyKeyDown();

void InstallDiplomacy(bool knownExe);

// A row of the game's diplomacy dialog (CVXUIDiplomacyMenu). Flags are diplomacy::k* bits.
struct DiplomacyRow {
	int player = 0;
	std::string name;
	uint32_t rgb = 0;
	bool ai = false;
	int team = 0;  // the room's team, 0 when none
	unsigned mine = 0;  // what the dialog's check boxes say now, not yet applied
	unsigned theirs = 0;
};

struct DiplomacyDialogState {
	void *dlg = nullptr;
	std::vector<DiplomacyRow> rows;
	bool alliedVictory = false;
	bool locked = false;
};

// False when the dialog is not open.
bool DiplomacyDialogRead(DiplomacyDialogState &out);
// Sets the dialog's check boxes; nothing changes in the game until DiplomacyDialogApply.
void DiplomacyDialogSetMine(void *dlg, int row, unsigned flags);
void DiplomacyDialogSetAlliedVictory(void *dlg, bool on);
// The dialog's own OK and Cancel: OK queues the game's CVXDiplomacyChange commands and both close it.
void DiplomacyDialogApply(void *dlg);
void DiplomacyDialogCancel(void *dlg);
