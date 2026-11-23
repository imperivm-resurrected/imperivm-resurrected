// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

#include "core/leaguemods.h"

// The gameplay mods a ranked season or a tournament asks for, as the screens show them, and "Prepare mods": the required GameBanana
// files found among the installed mods or downloaded, a profile with them and the active profile's other (cosmetic) mods made the
// active one, and the game restarted with it. The base game: the same profile without any gameplay mod.

// How this computer's mounted gameplay mods stand against the rule.
LeagueModsCheck LeagueModsFitNow(const LeagueModsRule &rule);
// "Played without gameplay mods" / "Played with: Greek Civilization Mod 4.2"; empty when the rule is not set. Translated.
std::string LeagueModsRuleText(const LeagueModsRule &rule);
// What is wrong with the player's mods, translated; empty when they fit.
std::string LeagueModsProblemText(const LeagueModsRule &rule, const LeagueModsCheck &check);

// The screen that started it ("ranked", "tourney:<id>"): the error of a preparation is only shown there; the progress everywhere.
void LeagueModsPrepStart(const LeagueModsRule &rule, const std::string &owner);
struct LeagueModsPrepView {
	// Looking for the mods, downloading them or waiting to restart: the button to prepare stays off.
	bool busy = false, failed = false;
	// The progress, the error or the restart to come, translated; empty while idle.
	std::string line;
};
LeagueModsPrepView LeagueModsPrepState(const std::string &owner);
// The mods are ready and wait for LeagueModsPrepTick to restart the game.
bool LeagueModsPrepReady();
// Every frame of the multiplayer menu: once the mods are ready and `canRestart` (no room, no game), the profile is made the active one
// and the game restarts.
void LeagueModsPrepTick(bool canRestart);
