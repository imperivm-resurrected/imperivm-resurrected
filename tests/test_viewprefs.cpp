// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "game/viewprefs.h"

static void HealthBarsStart()
{
	CHECK_EQ(HealthBarsClampStart(-1), static_cast<int>(kBarsHidden));
	CHECK_EQ(HealthBarsClampStart(4), static_cast<int>(kBarsHidden));
	CHECK_EQ(HealthBarsClampStart(3), static_cast<int>(kBarsMine));
	CHECK_EQ(HealthBarsNativeMode(kBarsHidden), 0);
	CHECK_EQ(HealthBarsNativeMode(kBarsAll), 1);
	CHECK_EQ(HealthBarsNativeMode(kBarsHeroes), 1);
	CHECK_EQ(HealthBarsNativeMode(kBarsMine), 1);
	CHECK_EQ(HealthBarsNativeMode(9), 0);
	CHECK_EQ(HealthBarsFilter(kBarsHidden), static_cast<int>(kBarsAll));
	CHECK_EQ(HealthBarsFilter(kBarsAll), static_cast<int>(kBarsAll));
	CHECK_EQ(HealthBarsFilter(kBarsHeroes), static_cast<int>(kBarsHeroes));
	CHECK_EQ(HealthBarsFilter(kBarsMine), static_cast<int>(kBarsMine));
}

static void HealthBarsFilterRules()
{
	CHECK(HealthBarsFilterHolds(0));
	CHECK(HealthBarsFilterHolds(1));
	CHECK(!HealthBarsFilterHolds(2));
	CHECK(!HealthBarsFilterHolds(3));
	CHECK(HealthBarsKeep(kBarsAll, false, false));
	CHECK(HealthBarsKeep(kBarsHeroes, true, false));
	CHECK(!HealthBarsKeep(kBarsHeroes, false, true));
	CHECK(HealthBarsKeep(kBarsMine, false, true));
	CHECK(!HealthBarsKeep(kBarsMine, true, false));
}

int main()
{
	HealthBarsStart();
	HealthBarsFilterRules();
	return test::Summary("viewprefs");
}
