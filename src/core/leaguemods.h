// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"

// The gameplay mods a ranked season or a tournament is played with, as the league fixes them: GameBanana mods pinned to one of their
// files. Nothing here reads the installed mods: the player's own list comes from mods/.

struct LeagueModRef {
	uint64_t gbId = 0, fileId = 0;
	// What the league shows of it; `version` may be empty.
	std::string name, file, version;
};

// `mods` of the season or the tournament. Not set (null): any content, and the gameplay key pairs the players as before. Set and empty:
// the base game, with no gameplay mod. Set with mods: exactly those files, and no other gameplay mod.
struct LeagueModsRule {
	bool set = false;
	std::vector<LeagueModRef> mods;
	bool BaseGame() const { return set && mods.empty(); }
};
LeagueModsRule LeagueParseModsRule(const Json *v);

// What the player sends of their own gameplay mods: [{"gbId": N, "fileId": N}], with zeros for one not installed from GameBanana.
Json LeagueModsOwnJson(const std::vector<LeagueModRef> &own);

// A required mod as the player reads it: its name and version ("Greek Civilization Mod 4.2"), its GameBanana number without a name.
std::string LeagueModLabel(const LeagueModRef &m);
// The labels joined with commas.
std::string LeagueModsNames(const std::vector<LeagueModRef> &mods);

// How the player's gameplay mods stand against a rule, the first that applies: a required file is not among them (Missing, with the
// ones missing), one of theirs can't be told because it is not from GameBanana, or they have one more than the rule (Extra; with the
// base game, any).
enum class LeagueModsFit { Ok, Missing, NotFromGameBanana, Extra };
struct LeagueModsCheck {
	LeagueModsFit fit = LeagueModsFit::Ok;
	std::vector<LeagueModRef> missing;
};
LeagueModsCheck LeagueModsCompare(const LeagueModsRule &rule, const std::vector<LeagueModRef> &own);
