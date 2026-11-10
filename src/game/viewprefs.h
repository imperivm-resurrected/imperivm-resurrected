// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Whose health bars a match starts with ([Game] HealthBarOwners), and whether only the heroes' ([Game] HealthBarHeroesOnly).
enum HealthBarsOwners { kBarsNone = 0, kBarsAll = 1, kBarsMine = 2, kBarsEnemies = 3 };
inline constexpr int kBarsOwnersCount = 4;

struct HealthBarsChoice {
	int owners = kBarsNone;
	bool heroesOnly = false;
};

// Whose a unit is, seen from the local player.
enum class UnitSide { kMine, kAlly, kEnemy };

int HealthBarsClampOwners(int value);
// The game's own mode for that choice: 0 hidden, 1 every bar.
int HealthBarsNativeMode(const HealthBarsChoice &choice);
// Whether the mod leaves bars out on top of the game's mode.
bool HealthBarsFilters(const HealthBarsChoice &choice);
// The filter lasts while the game's mode is the one it was set on, or hidden (the key hides and shows that same mode);
// cycling the modes with Ctrl gives the game's own ones back.
bool HealthBarsFilterHolds(int nativeMode);
// known: the owner is one of the 16 players; allied: the allied bit of the local player's relation towards the owner.
// A unit without an owner, or of a player that is not allied (neutrals and animals too), is an enemy.
UnitSide HealthBarsSide(bool mine, bool known, bool allied);
bool HealthBarsKeep(const HealthBarsChoice &choice, bool hero, UnitSide side);
