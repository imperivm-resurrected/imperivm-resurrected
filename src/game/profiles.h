// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "game/playerini.h"

void InstallProfileHooks(bool knownExe);

// The game's "Change player" dialog stays open under the mod's screen and does the work: the profiles are created, renamed,
// deleted and chosen by its own buttons, never by the mod writing their files. Null when it is not open. Everything but this
// call belongs on the game thread.
void *ProfileDialogOpen();

struct ProfileSlot {
	std::string name;
	// "profiles/<folder>", as the game keeps it.
	std::string dir;
};

struct ProfileDialogState {
	std::vector<ProfileSlot> slots;
	int selected = -1;
	int active = -1;
};

bool ProfileDialogRead(ProfileDialogState &out);
bool ProfileDialogSelect(int index);
bool ProfileDialogCreate(const std::string &name);
bool ProfileDialogRename(int index, const std::string &name);
bool ProfileDialogDelete(int index);
// Makes the profile the active one; the game closes the dialog and goes back to the main menu.
bool ProfileDialogPlay(int index);

// The game's limit for a name: its edit box holds 23 characters.
constexpr size_t kProfileNameMax = 23;

std::string ProfileIniPath(const std::string &dir);
// The ranks of the game's constants (CONST.INI, [Ranks]) with their names translated as the game shows them.
std::vector<RankStep> ProfileGameRanks();
std::string ProfileCivName(int race);
std::string ProfileUnitName(const std::string &name);
// The saved games kept in the profile's folder, which the game deletes with it.
int ProfileSaveCount(const std::string &dir);
