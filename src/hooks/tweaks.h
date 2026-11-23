// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <vector>

#include "hooks/respick.h"

void InstallTweaks(bool knownExe);
// The executable is the one the addresses are for (as InstallTweaks was told): code that reads or calls gbr.exe checks it.
bool TweaksKnownExe();
// The game's sites the crash protection patched (0 when it is off): it changes what scripts of the simulation decide, so it goes in what
// rooms compare.
int TweaksCrashFixSites();

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
// The window mode changed (Alt+Enter): on the game thread, at once or once the options close, the game takes the size saved for
// the new mode.
void TweaksModeSwitched();
// The mode switch and the answer to a full screen's size on trial: on the game thread, between frames (the wake of the game
// thread, which the frames ask for while one waits).
void TweaksResolutionTick();
void TweaksFrameTick();
bool TweaksOnlyHumanOnline(void *game);
// Pauses or resumes the match at once, without the game's pause notice: only for a player alone in it, whose pause no one else follows.
void TweaksSetPausedQuietly(void *game, bool paused);
bool TweaksGamePaused(void *game);
// While held, the pause stays: the pause key, the menu and the window coming back do not resume the match.
void TweaksHoldPause(bool held);
// Asked on the game thread before this computer's request to pause an online game with other players goes out: false keeps it
// here. The league limits each player's pauses in a ranked game (net/leagueclient_match.cpp); without a gate every request goes.
void TweaksSetPauseGate(bool (*mayPause)());
// Asks every computer of an online game to resume it, as closing the game's menu does.
void TweaksRequestUnpause(void *game);
// The game window goes to the background or comes back (WM_ACTIVATEAPP), on the game thread.
void TweaksWindowActive(bool active);
void TweaksOnConfigLoaded(void (*callback)());
