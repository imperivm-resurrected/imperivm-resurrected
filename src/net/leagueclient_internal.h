// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/json.h"
#include "core/league.h"

namespace leagueclient_impl {

struct HttpReply {
	// 0 when no answer came.
	int status = 0;
	std::string body;
	// Why there was no answer.
	std::string failure;
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

int64_t NowMs();
std::string UrlEncode(const std::string &s);

// The matches module, run from LeagueGameThreadTick.
void MatchTick();
// The tournaments module, run from LeagueGameThreadTick: polls the player's tournaments at their pace.
void TourneyTick();

// The ranked module, run from LeagueGameThreadTick.
void RankedTick();
// A ranked game ended on this computer and its report is on its way: the result screen waits for the answer.
void RankedGameReported(const LeagueReport &report, const std::string &ownSteamId);
// The server answered a report (from the sending thread); only ranked ones matter here.
void RankedReportAnswered(const Json &report, const std::string &answer);
// The game of a ranked match is being played on this computer: the server is told once, and the match can no longer be called off.
void RankedGameStarted(int64_t matchId);

}
