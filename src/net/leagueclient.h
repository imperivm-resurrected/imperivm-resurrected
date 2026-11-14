// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/league.h"

// The client of the league server (imperivm-league): the account linked to this computer's Steam user, the player's standing and
// the report of every online game. Every request runs on a background thread; the game thread only reads what came back.
//
// The server is [League] Url in imperivm-mod.ini (the public one when it is missing). Plain http is only accepted to this computer,
// so a developer points the mod at a local server with Url=http://localhost:8787.

// The address in use, without a trailing slash.
std::string LeagueApiBase();
// A GET without the account, for what anyone may ask (the players lookup). Empty, or what went wrong.
std::string LeagueGetJson(const std::string &path, Json &out);

enum class LeagueLinkPhase { Unlinked, Starting, Waiting, Linked, Unlinking };

struct LeagueLinkView {
	LeagueLinkPhase phase = LeagueLinkPhase::Unlinked;
	// While waiting: the code to type on the website and its page.
	std::string code, url;
	int64_t expiresAt = 0;
	// The name the account was linked with.
	std::string name;
	// The last thing that went wrong, as an English text to translate; empty when nothing did.
	std::string error;
	bool steamReady = false;
	// A token of another Steam user is stored: it is kept, but not used while this user plays.
	bool otherAccount = false;
};

LeagueLinkView LeagueLinkState();
// The account is linked to the Steam user playing now.
bool LeagueLinked();
// Asks the server for a code and waits, polling, until the player confirms it on the website or it expires.
void LeagueLinkStart();
void LeagueLinkCancel();
// Revokes the token on the server and forgets it here.
void LeagueUnlink();
// The website's page for the code, in the browser.
void LeagueOpenLinkPage();

struct LeagueMeView {
	bool loaded = false, loading = false;
	LeagueMe me;
};
LeagueMeView LeagueMeState();
// GET /v1/league/me in the background, at most once a minute unless forced.
void LeagueMeRefresh(bool force = false);

// What the multiplayer screen knows of the online game being played. The kind of game comes from the room's "irm" tag, which ranked
// and tournament rooms carry (ranked:<id>, tournament:<id>); any other room is a normal game.
struct LeagueMatchInfo {
	uint64_t lobby = 0;
	// The host's start, the same for every player; 0 until the room's data says it.
	int64_t startedAtMs = 0;
	LeagueMapFacts map;
	std::string contentKey, roomTag;
	// Steam id and name of the room's members, to tell who is who among the game's players.
	std::vector<std::pair<uint64_t, std::string>> members;
};

// On the game thread, every few seconds while this computer plays an online game (never as a spectator): starts recording it, or
// keeps recording it after a rejoin.
void LeagueMatchPlaying(const LeagueMatchInfo &info);
// The game is over for this computer (it is back in the menu). The report goes to the server in the background when the account is
// linked; when it cannot be sent, it waits on disk and is tried again.
void LeagueMatchEnded();
void LeagueMatchSurrendered(int player);
void LeagueMatchDesync();
// From the lockstep, after every tick: keeps the state hash every client compared for the ticks that are multiples of 1000.
void LeagueSyncAfterAdvance(void *transport);
// Every frame on the game thread: follows the players of the game being recorded, polls a pending link and sends waiting reports.
void LeagueGameThreadTick();
