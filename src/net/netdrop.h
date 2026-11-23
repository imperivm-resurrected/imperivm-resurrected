// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
void InstallNetDrop(bool knownExe);

void NetDropReset();
// An online game's network starts (its transport is built, before its first tick): whether the league's drop rules hold for it is read
// once now, from its Steam room's "drop", which the host set when it made the room and every player got when joining. Every computer
// decides each drop by that, never by what reaches it later.
void NetDropNetworkStarted();
// The hooks that decide whether the AI plays for whoever drops are in: they change the simulation, so rooms compare it.
bool NetDropHooksReady();

// In a ranked 2v2 the AI plays for a player who drops, until they come back (a normal game keeps their place empty instead): whether
// that player's army has such an AI (for the host, who lets them in), and, on every computer on the same tick (the rejoin's stop order),
// taking the AI of a ranked 2v2's player away, by the game's state alone.
bool NetDropAiReturnable(int player);
void NetDropAiRelease(int player);
