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

// Under the drop rules, `player` (0-7) left the game for good by the game's own state at this tick: still in it for the game, they quit, or
// they dropped 5 minutes of the game's clock ago and did not come back.
bool NetDropGoneDue(int player);
// A "#IRG:k" message command (LeagueGoneMessage), sent once NetDropGoneDue(k) and run on every computer and in the broadcast at the same
// tick: an enemy of its sender whom the AI plays for is defeated there by the game itself, which then declares the victory of the side
// that stayed. It checks only the game's state, which every copy holds alike, a reloaded one too. True when the text was such a command,
// whatever came of it (it is never shown as a message).
bool NetDropGoneCommand(void *cmd, const char *text, unsigned size);
