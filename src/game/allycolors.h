// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// "Ally and enemy colours" (Config::allyColors): on this PC only, the local player is drawn blue, their allies yellow and everyone else
// red, as Age of Empires does. Only how the players are drawn changes: the game's sync hash does not read their colours, and saves keep
// the real ones.
void InstallAllyColors(bool knownExe);
// On the game thread, every frame: applies the colours when the option or an alliance changed, and puts the real ones back without it.
void AllyColorsGameTick();
// The key that turns it on or off (Alt+G by default); true when it took the key.
bool AllyColorsHandleKey(unsigned msg, unsigned wp);
// A player's real colour (RGB555) while this one shows another; `shown` otherwise.
unsigned AllyColorsRealColor(int index, unsigned shown);
