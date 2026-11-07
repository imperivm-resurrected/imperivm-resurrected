// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Pauses a single player match while the game window is in the background, and resumes only the match it paused itself.
struct FocusPause {
	const void *pausedGame = nullptr;

	// True when the match has to be paused now.
	bool Leave(bool enabled, const void *game, bool multiplayer, bool paused);
	// True when the match has to be resumed now.
	bool Return(const void *game, bool paused);
};
