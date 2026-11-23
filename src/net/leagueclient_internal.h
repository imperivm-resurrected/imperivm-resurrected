// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "game/gamestats.h"

namespace leagueclient_impl {

struct HttpReply {
	// 0 when no answer came.
	int status = 0;
	std::string body;
	// Why there was no answer.
	std::string failure;
	// The whole seconds a 429 or a 503 asks to wait, from the Retry-After header or the body's retryAfter; 0 when not said.
	int retryAfterSec = 0;
	bool Ok() const { return failure.empty() && status >= 200 && status < 300; }
	// For the log: "network error 12029", or "403 forbidden: <message>".
	std::string Describe() const;
};

// A request to the league server; `path` starts with "/v1/". With `auth` it carries the account's token, and fails without asking
// when the account is not linked to the Steam user playing now. A 401 forgets the token. Never on the game thread.
HttpReply Http(const char *method, const std::string &path, const std::string &body, bool auth);

// What a request of files needs beyond Http's: another type of body and of answer, a larger answer, longer waits, and how far it went.
struct HttpOptions {
	// Null: JSON.
	const char *contentType = nullptr, *accept = nullptr;
	// 0: Http's.
	size_t maxBody = 0;
	unsigned sendMs = 0, receiveMs = 0;
	// The bytes sent, or received, so far and of how many (0 when the answer does not say), on the request's thread.
	std::function<void(uint64_t done, uint64_t total)> progress;
};
HttpReply HttpWith(const char *method, const std::string &path, const std::string &body, bool auth, const HttpOptions &options);

// The account linked to the Steam user playing now.
bool AccountFor(std::string &token, std::string &steamId);
// The SteamID the stored account was linked with, even when it is another user's.
std::string AccountSteamId();

// Under the module's lock: a copy of the view with `change` made, in its place; whoever holds the old one keeps it as it was.
template <class T, class F> void ChangeView(std::shared_ptr<const T> &view, F change)
{
	auto copy = std::make_shared<T>(*view);
	change(*copy);
	view = std::move(copy);
}

int64_t NowMs();
// This computer's clock in ms, for the waits measured here.
int64_t TickMs();
std::string UrlEncode(const std::string &s);
// The Steam user playing now, as text; empty while Steam is not running.
std::string OwnId();
// The game's language as the league names it: "es", "en" or "it". On the game thread only, as ModsLanguage: a request's thread is
// given it.
std::string LangCode();
// A game is on, played or watched.
bool InGame();
// The game's window is the one in front.
bool GameInFront();
// Nobody looks at the game: it is behind other windows, or the mouse and the keyboard were not touched for 10 minutes. The reads that
// only keep a screen fresh wait, and the polls for what can wait slow down, until the player is back.
bool PlayerAway();
// The wait before a request that repeats is sent again, after `r` and `fails` failures in a row (0 after a success): LeagueBackoffMs
// with what the server asked.
unsigned RetryDelayMs(const HttpReply &r, int fails, unsigned normalMs, unsigned firstMs, unsigned capMs);
// A request that got no answer, or a refusal: the English text to translate that tells the player.
const char *FailureText(const HttpReply &r);

// The matches module, run from LeagueGameThreadTick.
void MatchTick();
// The limit on each player's own pauses in a ranked game, every frame on the game thread: `ranked` while a ranked game is recorded,
// with its match for the ticket's settings; `held` while the wait for an opponent who dropped holds the pause, which is nobody's;
// `over` once the game is decided.
void PauseTick(bool ranked, int64_t matchId, bool held, bool over);
// The tournaments module, run from LeagueGameThreadTick: polls the player's tournaments at their pace.
void TourneyTick();

// The ranked module, run from LeagueGameThreadTick.
void RankedTick();
// A ranked game ended on this computer and its report is on its way: the result screen waits for the answer, and shows what the game
// recorded of this player (`facts`, when `read`).
void RankedGameReported(const LeagueReport &report, const std::string &ownSteamId, const GameRecordFacts &facts, bool read);
// The server answered a report (from the sending thread); only ranked ones matter here.
void RankedReportAnswered(const Json &report, const std::string &answer);
// The game of a ranked match is being played on this computer: the server is told once, and the match can no longer be called off.
// While it is played the match is not polled.
void RankedGameStarted(int64_t matchId);
// The game of a ranked match is over on this computer without a report: the match is polled again, so the queue follows the server.
void RankedGameEnded(int64_t matchId);
// The ticket of the ranked match being followed, when it is that one.
bool RankedTicketOf(int64_t matchId, LeagueMatchTicket &out);

// The duo, run from RankedTick: polled at its pace while the account is linked.
void PartyTick(bool linked);
// The 2v2 is on screen (LeagueDuoWatch).
bool PartyScreenOpen();

// The humans of the ranked 2v2 being played, as the game shows them now: their alliance, whether the lockstep still gets them (a
// player who dropped is played by the AI), whether they left for good (they quit, or their 5 minutes are over) and when they left.
struct TeamSeat {
	int index = 0, team = 0;
	std::string name;
	uint64_t steamId = 0;
	bool local = false, connected = true, gone = false, quit = false;
	uint32_t leftAt = 0;
};
struct TeamGame {
	int64_t matchId = 0;
	// A ranked 2v2: the partner's vote to surrender. Any game under the drop rules has the drops' notices.
	bool team = false;
	uint32_t clockMs = 0;
	bool decided = false;
	std::vector<TeamSeat> seats;
};
// False outside a game under the drop rules.
bool MatchTeamGame(TeamGame &out);
// The 2v2 module, run from MatchTick on the game thread: the vote and the notices of drops.
void TeamTick();

}
