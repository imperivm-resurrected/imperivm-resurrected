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
#include "core/leagueranked.h"

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
// Every frame on the game thread: follows the players of the game being recorded, polls a pending link, sends waiting reports and
// follows the ranked queue.
void LeagueGameThreadTick();

// The ranked 1v1 queue. Searching goes on while the player looks at other screens; the server pairs players when they poll.
//   Idle: not searching. Joining: asking to enter the queue. Searching: in the queue, polled every 5 s.
//   Found: a match waits for this player's answer (Accepted once they said yes and the other one has not yet).
//   Ready: both accepted; the server's host makes the Steam room. Playing: the room is registered and the game may be running.
enum class LeagueRankedPhase { Idle, Joining, Searching, Found, Accepted, Ready, Playing };

// A ranked game as this computer remembers it (the server has no list of a player's games yet).
struct LeagueRankedGame {
	int64_t matchId = 0, at = 0;
	std::string opponent, opponentId, map;
	bool won = false;
	int delta = 0;
	uint32_t durationMs = 0;
	// "confirmed", "awaiting_other", "disputed"... as the last answer said; empty until the server answered.
	std::string status;
	LeagueTier tierAfter = LeagueTier::None;
	int divisionAfter = 0;
};

// What the result screen shows after a ranked game.
enum class LeagueResultState { None, Sending, Awaiting, Ready };
struct LeagueRankedOutcome {
	LeagueResultState state = LeagueResultState::None;
	LeagueRankedGame game;
	LeagueReportAnswer answer;
	unsigned serial = 0;
};

struct LeagueRankedView {
	bool linked = false;
	bool seasonLoaded = false, seasonLoading = false, noSeason = false;
	LeagueSeason season;
	std::string lang;
	// The first ten and the page around the player, for the hub.
	bool boardLoaded = false;
	LeagueBoard top, around;
	// The page the full leaderboard asked for.
	bool pageLoading = false, pageLoaded = false;
	LeagueBoard page;
	std::string pageError;
	LeagueRankedPhase phase = LeagueRankedPhase::Idle;
	// While searching: since when (server time, ms since 1970), how many search and the wait the server expects.
	int64_t since = 0;
	int searching = 0, estimateSec = 0;
	// The match found or being played.
	int64_t matchId = 0;
	LeagueQueueAnswer found;
	bool ticketLoaded = false;
	LeagueMatchTicket ticket;
	// This player's side and the other one in the ticket.
	LeaguePlayerCard me, opponent;
	bool host = false, roomRegistered = false, answering = false;
	// The queue is locked until then (ms since 1970) after declining or letting a match go.
	int64_t banUntil = 0;
	// The last thing that happened, as an English text to translate (with its values filled in by the server's), and whether it is bad.
	std::string notice;
	bool noticeBad = false;
	// Counts the matches found, so the interface can call the player once per match.
	unsigned foundSerial = 0;
	LeagueRankedOutcome result;
	std::vector<LeagueRankedGame> recent;
	// Choosing the badge.
	bool badgeBusy = false;
	std::string badgeError;
};
LeagueRankedView LeagueRankedState();
// GET /v1/league/season in the language of the game, at most every ten minutes unless forced.
void LeagueSeasonRefresh(bool force = false);
// The first ten and the player's place, at most once a minute unless forced.
void LeagueBoardRefresh(bool force = false);
// A page of the full leaderboard: `limit` rows from `offset`, or the page around the player.
void LeagueBoardRequest(int offset, int limit, bool aroundMe);
// Enters the queue with this computer's gameplay content. Refused (with a notice) when not linked, banned or already in a match.
void LeagueQueueJoin();
void LeagueQueueLeave();
void LeagueMatchAnswer(bool accept);
// The host's Steam room of the match is open: the server gets its id and the other player joins it.
void LeagueRankedRoomOpened(uint64_t lobby);
// Polls the match again now: the room failed and the interface tries once more.
void LeagueRankedRetryRoom();
// The player closed the result screen.
void LeagueRankedResultSeen();
void LeagueRankedClearNotice();
// PUT /v1/league/me/badge: "rank", "season" or "trophy" (with its id) or "none".
void LeagueBadgeChoose(const std::string &kind, const std::string &id);
