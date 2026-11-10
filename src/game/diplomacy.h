// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The game only offers diplomacy when several humans play; the mod also offers it in a match against the AI.
bool DiplomacyAvailable();

// Called after the game handled a key down that ended up as F5: opens diplomacy where the game would not.
void DiplomacyKeyDown();

void InstallDiplomacy(bool knownExe);
