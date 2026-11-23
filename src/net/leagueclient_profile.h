// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <memory>
#include <string>

#include "core/leagueprofile.h"

// Any player's statistics on the league, for the public profile: read on a background thread, kept a minute.

// Missing when the player has no league games (404).
enum class LeagueProfileState { None, Loading, Ready, Missing, Failed };
struct LeagueProfileView {
	LeagueProfileState state = LeagueProfileState::None;
	// Never changed once given; a new answer makes a new one. Kept while it is read again.
	std::shared_ptr<const LeagueProfile> profile;
};
LeagueProfileView LeagueProfileOf(const std::string &steamId);
// GET /v1/league/players/:steamid/profile in the game's language, again only after a minute unless forced. On the game thread.
void LeagueProfileRequest(const std::string &steamId, bool force = false);
