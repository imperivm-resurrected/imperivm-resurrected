// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/focuspause.h"

bool FocusPause::Leave(bool enabled, const void *game, bool multiplayer, bool paused)
{
	pausedGame = nullptr;
	if (!enabled || !game || multiplayer || paused) return false;
	pausedGame = game;
	return true;
}

bool FocusPause::Return(const void *game, bool paused)
{
	const bool ours = pausedGame && pausedGame == game && paused;
	pausedGame = nullptr;
	return ours;
}
