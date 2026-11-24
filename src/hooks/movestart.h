// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
// A unit ordered to move starts at once instead of finishing its idle animation, and one re-ordered while walking turns from where
// it is. Ported from Classic Modern's MovementFastStart.
void InstallMoveStart(bool knownExe);

// True once every part of it is in: the match's simulation is not the original game's, and the rooms must say so.
bool MoveStartActive();
