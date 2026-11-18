// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// "Dynamic seasons" (Config::dynamicSeasons): spring, autumn and winter take turns as the match's clock runs, through the engine's
// own CVXBasicGame::SetSeason, which the editor and the console's _chs use: terrain, decors and the objects' seasonal pictures. The
// season only changes pictures, so each computer changes it on its own at the same time of the match.
// On the game thread, every frame: asks for the game thread's wake when a change is due.
void SeasonsGameTick();
// On the game thread, between frames (the wake): makes the change.
void SeasonsQueueTick();
