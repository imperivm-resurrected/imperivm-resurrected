// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

void InstallNetTune(bool knownExe);

void NetTuneAfterAdvance(void *transport);

// The network turns since the last call: how long they took, their planned size and added delay (the transport's own), the time the
// game spent waiting for the other players' data (the turns' time beyond their size), the resend requests, and how late the latest
// player's data arrived after it was due ("PEER DELAY"; -1 with no report).
struct NetTurnSecond {
	int count = 0;
	int avgMs = -1;
	int maxMs = 0;
	int sizeAvgMs = -1;
	int delayAvgMs = -1;
	int waitMs = 0;
	int resends = 0;
	int lateMaxMs = -1;
};

NetTurnSecond NetTuneTakeSecond();

// What decides the simulation besides the build and the rules (the forced network turns, Config.ini's pathfinder and AI, the crash
// protection, the mod's patches of the simulation): rooms compare it before a game starts.
std::string NetSimSignature();
// The rules level a Steam room announces and compares ("irlvl"): the mods' one with the signature.
std::string NetRulesLevel();
// The [Network] values the game uses now, for the log of a desync.
std::string NetTuneDescribe();
// The game's last lines of its network transport (NACK, PEER DELAY, AdvanceTick...), oldest first, for the log of a desync.
std::vector<std::string> NetTuneRecentLines();
