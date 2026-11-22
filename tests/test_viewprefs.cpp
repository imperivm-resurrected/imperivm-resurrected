// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "game/viewprefs.h"

static HealthBarsChoice Choice(int owners, bool heroesOnly)
{
	HealthBarsChoice c;
	c.owners = owners;
	c.heroesOnly = heroesOnly;
	return c;
}

static void ModeAndFilter()
{
	CHECK_EQ(HealthBarsClampOwners(-1), static_cast<int>(kBarsNone));
	CHECK_EQ(HealthBarsClampOwners(4), static_cast<int>(kBarsNone));
	CHECK_EQ(HealthBarsClampOwners(3), static_cast<int>(kBarsEnemies));
	CHECK_EQ(HealthBarsNativeMode(Choice(kBarsNone, true)), 0);
	CHECK_EQ(HealthBarsNativeMode(Choice(kBarsAll, false)), 1);
	CHECK_EQ(HealthBarsNativeMode(Choice(kBarsMine, false)), 1);
	CHECK_EQ(HealthBarsNativeMode(Choice(kBarsEnemies, true)), 1);
	CHECK_EQ(HealthBarsNativeMode(Choice(9, false)), 0);
	CHECK(!HealthBarsFilters(Choice(kBarsNone, true)));
	CHECK(!HealthBarsFilters(Choice(kBarsAll, false)));
	CHECK(HealthBarsFilters(Choice(kBarsAll, true)));
	CHECK(HealthBarsFilters(Choice(kBarsMine, false)));
	CHECK(HealthBarsFilters(Choice(kBarsEnemies, false)));
	CHECK(HealthBarsFilterHolds(0));
	CHECK(HealthBarsFilterHolds(1));
	CHECK(!HealthBarsFilterHolds(2));
	CHECK(!HealthBarsFilterHolds(3));
}

static void Sides()
{
	CHECK(HealthBarsSide(true, true, true) == UnitSide::kMine);
	CHECK(HealthBarsSide(true, false, false) == UnitSide::kMine);
	CHECK(HealthBarsSide(false, true, true) == UnitSide::kAlly);
	CHECK(HealthBarsSide(false, true, false) == UnitSide::kEnemy);
	CHECK(HealthBarsSide(false, false, true) == UnitSide::kEnemy);
}

static void Keep()
{
	const UnitSide sides[] = {UnitSide::kMine, UnitSide::kAlly, UnitSide::kEnemy};
	for (UnitSide side : sides) {
		CHECK(HealthBarsKeep(Choice(kBarsAll, false), false, side));
		CHECK(HealthBarsKeep(Choice(kBarsAll, true), true, side));
		CHECK(!HealthBarsKeep(Choice(kBarsAll, true), false, side));
	}
	CHECK(HealthBarsKeep(Choice(kBarsMine, false), false, UnitSide::kMine));
	CHECK(!HealthBarsKeep(Choice(kBarsMine, false), false, UnitSide::kAlly));
	CHECK(!HealthBarsKeep(Choice(kBarsMine, false), true, UnitSide::kEnemy));
	CHECK(HealthBarsKeep(Choice(kBarsMine, true), true, UnitSide::kMine));
	CHECK(!HealthBarsKeep(Choice(kBarsMine, true), false, UnitSide::kMine));
	CHECK(HealthBarsKeep(Choice(kBarsEnemies, false), false, UnitSide::kEnemy));
	CHECK(!HealthBarsKeep(Choice(kBarsEnemies, false), false, UnitSide::kAlly));
	CHECK(!HealthBarsKeep(Choice(kBarsEnemies, false), true, UnitSide::kMine));
	CHECK(HealthBarsKeep(Choice(kBarsEnemies, true), true, UnitSide::kEnemy));
	CHECK(!HealthBarsKeep(Choice(kBarsEnemies, true), false, UnitSide::kEnemy));
}

static void Key()
{
	CHECK_EQ(HealthBarsNextOwners(kBarsAll), static_cast<int>(kBarsMine));
	CHECK_EQ(HealthBarsNextOwners(kBarsMine), static_cast<int>(kBarsEnemies));
	CHECK_EQ(HealthBarsNextOwners(kBarsEnemies), static_cast<int>(kBarsAll));
	CHECK_EQ(HealthBarsNextOwners(kBarsNone), static_cast<int>(kBarsAll));
	// Tapping the key hides, and shows again whose bars were shown.
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsMine, kBarsMine, 1, 0), static_cast<int>(kBarsNone));
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsNone, kBarsMine, 0, 1), static_cast<int>(kBarsMine));
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsNone, kBarsNone, 0, 1), static_cast<int>(kBarsAll));
	// Ctrl+` from hidden only shows them; over shown bars it moves to the next.
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsNone, kBarsEnemies, 0, 2), static_cast<int>(kBarsEnemies));
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsAll, kBarsAll, 1, 3), static_cast<int>(kBarsMine));
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsEnemies, kBarsEnemies, 1, 3), static_cast<int>(kBarsAll));
	CHECK_EQ(HealthBarsOwnersAfterKey(kBarsMine, kBarsMine, 1, 1), static_cast<int>(kBarsMine));
}

int main()
{
	ModeAndFilter();
	Key();
	Sides();
	Keep();
	return test::Summary("viewprefs");
}
