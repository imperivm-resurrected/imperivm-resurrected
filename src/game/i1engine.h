// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Imperivm I's engine for the classes of a converted Imperivm I or Imperivm II campaign (tools/imperivm1, tools/imperivm2):
// the ones whose id is "I1" or "I2" and a capital letter. Each one is its game's class, with all its behaviours written in
// its own file, and its combat values in data/mod/imperivm1/stats.ini or data/mod/imperivm2/stats.ini, which Great Battles
// of Rome's classes cannot hold. Imperivm II fights with Imperivm I's min/max attack and defences, with its own
// specialities, order and constants; its garrisons, healing in buildings and towers are already Great Battles of Rome's.
void InstallI1Engine(bool knownExe);

// What the attacker was before the game's damage ran (that run clears a curse): taken just before it.
struct I1AttackState {
	bool cursed = false, parrying = false;
};
I1AttackState I1BeforeAttack(const void *attacker);
// A hit given by an Imperivm I or II class takes its game's formula: the life each side gains (negative: loses).
void I1AfterAttack(const I1AttackState &before, void *attacker, void *defender, int *dLifeAtt, int *dLifeDef);
