// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The room's "Dynamic seasons" rule: spring, autumn and winter take turns every 5, 10 or 20 minutes of the match's clock, through
// the engine's own CVXBasicGame::SetSeason, which the editor and the console's _chs use: terrain, decors and the objects' seasonal
// pictures. The season only changes pictures, so each computer changes it on its own at the same time of the match. The picture
// fades from one season into the next (core/framefade.h).
// On the game thread, every frame: asks for the game thread's wake when a change is due.
void SeasonsGameTick();
// On the game thread, between frames (the wake): makes the change.
void SeasonsQueueTick();
