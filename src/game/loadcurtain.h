// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallLoadCurtain(bool knownExe);
bool LoadCurtainUp();
void LoadCurtainLift(const char *why);
// Called on the game thread each time the game makes the view of a match (CVXMapView), right after it is built.
void LoadCurtainOnMatchView(void (*callback)());
