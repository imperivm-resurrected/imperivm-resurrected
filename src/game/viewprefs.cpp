// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/viewprefs.h"

int HealthBarsClampStart(int value)
{
	return value >= kBarsHidden && value < kBarsStartCount ? value : kBarsHidden;
}

int HealthBarsNativeMode(int start)
{
	return HealthBarsClampStart(start) == kBarsHidden ? 0 : 1;
}

int HealthBarsFilter(int start)
{
	const int s = HealthBarsClampStart(start);
	return s == kBarsHeroes || s == kBarsMine ? s : kBarsAll;
}

bool HealthBarsFilterHolds(int nativeMode)
{
	return nativeMode == 0 || nativeMode == 1;
}

bool HealthBarsKeep(int filter, bool hero, bool mine)
{
	if (filter == kBarsHeroes) return hero;
	if (filter == kBarsMine) return mine;
	return true;
}
