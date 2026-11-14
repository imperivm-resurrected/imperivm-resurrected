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

// Every frame on the game thread: polls a pending link.
void LeagueGameThreadTick();
