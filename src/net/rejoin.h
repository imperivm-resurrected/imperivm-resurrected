// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

void InstallRejoin(bool knownExe);
// The hooks of the rejoin are in: some of them change the simulation (the paths' limits), so rooms compare it.
bool RejoinHooksReady();
// An online game's network starts (its transport is built): the sync snapshots of the game before are forgotten.
void RejoinNetworkStarted();

void RejoinGameThreadTick();
bool RejoinWantsWake();
void RejoinMenuTick();
void RejoinOnSteamMessage(uint64_t peer, uint8_t type, const std::string &data);

void RejoinRequest(uint64_t hostId, const std::string &hostName, const std::vector<std::pair<uint64_t, std::string>> &others = {});
void RejoinCancel();
bool RejoinClientActive();
std::string RejoinClientStatus();
bool RejoinClientFailed();
void RejoinClientDismiss();
bool RejoinBusy();
bool RejoinReloading();
// This computer is bringing a player who dropped back into its game.
bool RejoinHosting();

bool RejoinPendingLoad();
void RejoinAfterLoad(void *game, bool loaded);
