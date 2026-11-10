// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

void InstallScriptEvents(bool knownExe);

// The game made its script library again: the event scripts have to be compiled again the next time they fire.
void ScriptEventsReset();

int ModAttackDamage();
void ModSetAttackDamage(int damage);
