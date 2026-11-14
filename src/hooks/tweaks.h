// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <vector>

#include "hooks/respick.h"

void InstallTweaks(bool knownExe);

bool TweaksIntendedResolution(int &w, int &h);

// A negative monitor is the one chosen in the settings.
struct ResolutionChoice {
	int monitor = -1;
	bool windowed = false;
	bool extendedView = false;
};

// Rebuilds the game's resolution list for settings not applied yet; nullptr goes back to the applied ones.
void TweaksPreviewResolutions(const ResolutionChoice *choice);
std::vector<ResSize> TweaksResolutionList();

void TweaksApplySmoothScroll();
void TweaksFrameTick();
bool TweaksOnlyHumanOnline(void *game);
// Pauses or resumes the match at once, without the game's pause notice: only for a player alone in it, whose pause no one else follows.
void TweaksSetPausedQuietly(void *game, bool paused);
bool TweaksGamePaused(void *game);
// While held, the pause stays: the pause key, the menu and the window coming back do not resume the match.
void TweaksHoldPause(bool held);
// The game window goes to the background or comes back (WM_ACTIVATEAPP), on the game thread.
void TweaksWindowActive(bool active);
void TweaksOnConfigLoaded(void (*callback)());
