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
void TweaksOnConfigLoaded(void (*callback)());
