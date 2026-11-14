// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/tournament.h"

// The league's tournaments, official and of the community: the list, a tournament's page, the player's own (polled), what the player
// does in them and what the organizer of a community cup does in theirs. Every request runs on a background thread; the screens read
// the last answer.

struct TourneyListsView {
	bool loaded = false, loading = false, failed = false;
	// Sign-up or check-in open, being played, over. The official ones go first in the first two.
	std::vector<TourneySummary> open, live, finished;
};
TourneyListsView LeagueTourneyLists();
// The list in the background, at most every 30 s unless forced.
void LeagueTourneyListsRefresh(bool force = false);

struct TourneyPageView {
	bool loaded = false, loading = false, failed = false, missing = false;
	TourneyDetail detail;
};
// A tournament's page as last read; it is read again in the background when older than `maxAgeMs`.
TourneyPageView LeagueTourneyPage(const std::string &slug, int maxAgeMs = 10000);

struct TourneyMineView {
	bool loaded = false, loading = false;
	TourneyMine mine;
};
TourneyMineView LeagueTourneyMine();
// Asks for the player's tournaments now.
void LeagueTourneyMineRefresh();
// The Tournaments tab is on screen: with none of the player's own on, they are asked for every minute while this keeps being called.
void LeagueTourneyWatching();

enum class TourneyOp {
	SignUp, Withdraw, CheckIn, Claim, Register, Presence, NoShow, Dispute,
	// A community cup: made, found by its code, and its organizer's actions.
	Create, FindCode, Cancel, CheckinNow, StartNow, RemoveEntry, Resolve,
};

// The answer to something the player did.
struct TourneyOpResult {
	TourneyOp op = TourneyOp::SignUp;
	// The tournament (sign-up, withdrawal, check-in, the organizer's actions), the match, the entry removed; 0 for a cup made or a code.
	int64_t id = 0;
	bool ok = false;
	// 0 when the server could not be reached.
	int status = 0;
	// The server's code ("closed", "wrong_content"...) and its English text; "not_linked" without an account.
	std::string error, message;
	// The waiting list instead of a place.
	bool waitlist = false;
	// Until when a suspended player cannot sign up, and the content a tournament asks for.
	int64_t until = 0;
	std::string contentName;
	TourneyClaim claim;
	// What the other refusals add: the games played and needed to organise, the players checked in and needed to start, the seconds to
	// wait after too many requests.
	int gamesPlayed = 0, gamesNeeded = 0, checkedIn = 0, needed = 0, retryAfter = 0;
	// The cup made (and its code when private), the one a code found, or the one the player already organises.
	int64_t tournament = 0;
	std::string slug, inviteCode;
};
// In a private cup the code the player found it with goes along.
void LeagueTourneySignUp(int64_t tournament);
void LeagueTourneyWithdraw(int64_t tournament);
// With the content key of this computer's rooms, which must be the tournament's.
void LeagueTourneyCheckIn(int64_t tournament);
// `staleLobby`: the room the server gave to join, which could not be entered; the server forgets it if it is still the match's.
void LeagueTourneyClaimRoom(int64_t match, uint64_t staleLobby = 0);
void LeagueTourneyRegisterRoom(int64_t match, uint64_t lobby, uint64_t specLobby);
// "I am in the room"; `started` when the game starts.
void LeagueTourneyPresence(int64_t match, bool started);
void LeagueTourneyClaimNoShow(int64_t match);
void LeagueTourneyDispute(int64_t match, const std::string &text);
void LeagueTourneyCreate(const TourneyCupForm &form);
// The cup of an invitation code; its page is read by the code from then on, which also opens the private ones.
void LeagueTourneyFindByCode(const std::string &code);
// The code the player found or made a cup with; empty when none.
std::string LeagueTourneyCodeOf(const std::string &slug);
// The organizer's actions on their cup.
void LeagueTourneyCancel(int64_t tournament);
void LeagueTourneyCheckinNow(int64_t tournament);
void LeagueTourneyStartNow(int64_t tournament);
void LeagueTourneyRemoveEntry(int64_t tournament, int64_t entry);
enum class TourneyVerdict { GameToA, GameToB, Replay, WalkoverToA, WalkoverToB, WalkoverNobody };
void LeagueTourneyResolve(int64_t match, TourneyVerdict verdict);

struct TourneyDisputesView {
	bool loaded = false, loading = false, failed = false;
	std::vector<TourneyDispute> disputes;
};
// The open disputes of the player's cup as last read; read again in the background when older than `maxAgeMs`.
TourneyDisputesView LeagueTourneyDisputes(int64_t tournament, int maxAgeMs = 15000);

// A cup being made and a code being looked up go by the id 0.
bool LeagueTourneyBusy(TourneyOp op, int64_t id);
// The answers, oldest first, each once. Presence answers are not given: they only update the player's next match.
bool LeagueTourneyTakeResult(TourneyOpResult &out);
// What to tell the player in any menu, each once: a match ready, a check-in open, one of their tournaments over.
bool LeagueTourneyTakeNotice(TourneyNotice &out);
