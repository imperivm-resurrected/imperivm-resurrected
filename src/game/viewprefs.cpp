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

int HealthBarsNextOwners(int owners)
{
	switch (HealthBarsClampOwners(owners)) {
	case kBarsAll: return kBarsMine;
	case kBarsMine: return kBarsEnemies;
	default: return kBarsAll;
	}
}

int HealthBarsOwnersAfterKey(int owners, int lastShown, int before, int now)
{
	if (now == before) return HealthBarsClampOwners(owners);
	if (now == 0) return kBarsNone;
	if (before == 0) {
		const int back = HealthBarsClampOwners(lastShown);
		return back == kBarsNone ? kBarsAll : back;
	}
	return HealthBarsNextOwners(owners);
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
