// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/leagueseasons.h"

// What the league's seasons leave: the list of seasons, a season's hall of fame, the players' cards, the end of season ceremony and the
// frame and the title a season gives. Every request runs on a background thread; the screens read the last answer.

struct LeagueSeasonsView {
	bool loaded = false, loading = false;
	// Newest first.
	std::vector<LeagueSeasonInfo> seasons;
	// The hall of fame asked last: its season and queue, and whether the season has none (404) or the server could not be reached.
	int hallSeason = -1;
	std::string hallQueue;
	bool hallLoading = false, hallLoaded = false, hallFailed = false;
	LeagueHall hall;
	// The frame or the title is being changed; the last thing that went wrong, as an English text to translate.
	bool cosmeticsBusy = false;
	std::string cosmeticsError;
	// The ceremony's answer is on its way.
	bool ceremonyBusy = false;
};
LeagueSeasonsView LeagueSeasonsState();
// The same view shared, never changed once given (a change makes a new one): for what reads it every frame, without a copy.
std::shared_ptr<const LeagueSeasonsView> LeagueSeasonsShared();
// GET /v1/league/seasons, at most every ten minutes unless forced.
void LeagueSeasonsRefresh(bool force = false);
// GET /v1/league/seasons/:id/hall of a queue ("1v1", "2v2"), again only after five minutes.
void LeagueHallRequest(int season, const std::string &queue = "1v1");

// A player's card for a season (-1: the one being played): Missing when they did not play it (404).
enum class LeagueCardState { None, Loading, Ready, Missing, Failed };
struct LeaguePlayerView {
	LeagueCardState state = LeagueCardState::None;
	LeaguePlayerProfile card;
};
LeaguePlayerView LeaguePlayerState(const std::string &steamId, int season, const std::string &queue = "1v1");
// GET /v1/league/players/:steamid?season=&queue=, again only after a minute unless forced.
void LeaguePlayerRequest(const std::string &steamId, int season, bool force = false, const std::string &queue = "1v1");

// PUT /v1/league/me/cosmetics: the season whose frame, or title, the player wears (-1: none); only what `setFrame` and `setTitle` say.
void LeagueCosmeticsSet(bool setFrame, int frame, bool setTitle, int title);
// The ceremony's "Equip and continue": the season's vexillum next to the name (`badgeId`, its id among the player's badges) and, when
// the season gave them, its frame and title; then the ceremony is seen. Each queue has its own ceremony.
void LeagueCeremonyEquip(int season, bool frameAndTitle, const std::string &badgeId, const std::string &queue = "1v1");
// "Choose later": the ceremony is only marked seen.
void LeagueCeremonySeen(int season, const std::string &queue = "1v1");
