// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallTweaks(bool knownExe);

bool TweaksIntendedResolution(int &w, int &h);

void TweaksApplySmoothScroll();
void TweaksFrameTick();
bool TweaksOnlyHumanOnline(void *game);
// The game window goes to the background or comes back (WM_ACTIVATEAPP), on the game thread.
void TweaksWindowActive(bool active);
void TweaksOnConfigLoaded(void (*callback)());
