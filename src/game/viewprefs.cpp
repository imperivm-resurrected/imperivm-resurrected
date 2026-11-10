// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/viewprefs.h"

int HealthBarsClampOwners(int value)
{
	return value >= kBarsNone && value < kBarsOwnersCount ? value : kBarsNone;
}

int HealthBarsNativeMode(const HealthBarsChoice &choice)
{
	return HealthBarsClampOwners(choice.owners) == kBarsNone ? 0 : 1;
}

bool HealthBarsFilters(const HealthBarsChoice &choice)
{
	const int owners = HealthBarsClampOwners(choice.owners);
	return owners != kBarsNone && (owners != kBarsAll || choice.heroesOnly);
}

bool HealthBarsFilterHolds(int nativeMode)
{
	return nativeMode == 0 || nativeMode == 1;
}

UnitSide HealthBarsSide(bool mine, bool known, bool allied)
{
	if (mine) return UnitSide::kMine;
	return known && allied ? UnitSide::kAlly : UnitSide::kEnemy;
}

bool HealthBarsKeep(const HealthBarsChoice &choice, bool hero, UnitSide side)
{
	if (choice.heroesOnly && !hero) return false;
	switch (HealthBarsClampOwners(choice.owners)) {
	case kBarsMine: return side == UnitSide::kMine;
	case kBarsEnemies: return side == UnitSide::kEnemy;
	default: return true;
	}
}
