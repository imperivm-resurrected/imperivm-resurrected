// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/tournament.h"
#include "mods/mods.h"

// The scenarios of the community cups on this computer: the ones a cup can be played on, the file the game loads for a cup's round,
// and the files a cup brings, kept in Scenarios as the ones a host passes in a room.

// A scenario a cup can use: one with two human seats at least, for a game of the room's selector (not an adventure), that is the game's
// own or the player's: loose, received or from a content pack of maps only.
struct CupScenario {
	// "Scenarios\Torneo_1vs1_Egypt", as the room's selector names it.
	std::string path;
	// "Torneo_1vs1_Egypt.BFHP".
	std::string file;
	// Its own name, else the file's; at most 80 characters, as the league keeps it.
	std::string title;
	std::string sha256;
	uint64_t bytes = 0;
	int humans = 0;
	// Its human places in the order the room gives them out, the players of the computer the scenario plays, and what its scripts do
	// (TourneyScriptFlags): what TourneyScenarioFor needs to tell whether it suits a cup.
	std::vector<TourneyPlace> places;
	int computers = 0;
	unsigned scripts = 0;
	// The game's own, untouched: every player has it, and it is not uploaded.
	bool game = false;
	MapOrigin origin;
};

struct CupScenariosView {
	bool loaded = false, loading = false;
	std::vector<CupScenario> list;
	// Scenarios left out because a content pack that changes the rules brings them: those go with the cup's content, not as a map.
	int fromRules = 0;
	// Those scenarios, for the room's selector, where they can be played with their pack.
	std::vector<CupScenario> rules;
};
// The last list read; it is read again in the background the first time and after CupScenariosRescan.
CupScenariosView CupScenarios();
void CupScenariosRescan();

// The bytes of the scenario of that file name as the game would load it (a content pack's before a loose one). Blocking.
bool CupScenarioRead(const std::string &file, std::string &bytes);
// The SHA-256 of that scenario on this computer; false when there is none. Blocking.
bool CupScenarioLocal(const std::string &file, std::string &sha256, uint64_t &bytes);
// Saves a scenario a cup brings in Scenarios: the file there before, if any, is kept as .previous, and it is noted as received.
bool CupScenarioStore(const std::string &file, const std::string &bytes, std::string &error);
// The room's path of a cup's scenario file: "Scenarios\<name without .bfhp>".
std::string CupScenarioPath(const std::string &file);
