// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Game thread, at a game's first update: loads the graphics of the units of every civilization in the game, which the game would
// otherwise load the first time each unit shows up, stalling play for up to a few hundred milliseconds each time. Once per game.
void UnitArtPreload();
