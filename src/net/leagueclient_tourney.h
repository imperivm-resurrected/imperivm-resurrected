// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/tournament.h"

// The league's tournaments, official and of the community: the list, a tournament's page, the player's own (polled), what the player
// does in them and what the organizer of a community cup does in theirs. Every request runs on a background thread; the screens read
// the last answer.
//
// A page and the player's tournaments are given as a shared view that never changes: a change makes a new one, so a screen keeps the one
// it got for its frame without copying it.

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
std::shared_ptr<const TourneyPageView> LeagueTourneyPage(const std::string &slug, int maxAgeMs = 10000);

struct TourneyMineView {
	bool loaded = false, loading = false;
	TourneyMine mine;
};
std::shared_ptr<const TourneyMineView> LeagueTourneyMine();
// Asks for the player's tournaments now.
void LeagueTourneyMineRefresh();
// The Tournaments tab is on screen: with none of the player's own on, they are asked for every minute while this keeps being called.
void LeagueTourneyWatching();

enum class TourneyOp {
	SignUp, Withdraw, CheckIn, Claim, Register, Presence, NoShow, Dispute,
	// A community cup: made, found by its code, and its organizer's actions.
	Create, FindCode, Cancel, CheckinNow, StartNow, RemoveEntry, Resolve, Edit,
	// The map of a series' next game chosen.
	Pick,
	// A cup of teams: the invitation to a team answered, and the captain's team withdrawn while it waits for the partner; both by the cup.
	Accept, Decline, CancelTeam,
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
	// The waiting list instead of a place; a team that waits for its partner to accept.
	bool waitlist = false, pendingPartner = false;
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
	// requirement: "games" or "tier", what the player has and what the cup asks, and "partner" in `who` when the partner misses it.
	// "partner_not_linked" in `error`: the partner of a team's sign-up has no league account (the league's not_linked).
	std::string requirement, have, need, who;
	// A scenario of the cup that could not go up (its title), or the uploaded one the league does not have (map_missing: its SHA-256).
	std::string mapTitle, hash;
	// The map a pick chose.
	TourneyMap map;
	// A win claimed too early by rounds: from when the no-show counts.
	int64_t noshowFrom = 0;
};
// In a private cup the code the player found it with goes along.
void LeagueTourneySignUp(int64_t tournament);
// A cup of teams: the player signs up as the captain with a partner, who has to accept it from the game, and an optional team name. It is
// the SignUp op, with `pendingPartner` once the league takes it.
void LeagueTourneySignUpTeam(int64_t tournament, uint64_t partner, const std::string &teamName);
// The invitation to a team answered: the Accept or Decline op, by the cup (a player has one invitation a cup at most).
void LeagueTourneyAnswerInvite(int64_t tournament, int64_t entry, bool accept);
// The captain's team taken back before the partner accepted: the CancelTeam op.
void LeagueTourneyCancelTeam(int64_t tournament);
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
// The map of the next game of the player's series, by its place among the pick's options; once the league takes it, the player's next
// match has no pick any more and that game has its map.
void LeagueTourneyPick(int64_t match, int index);
// The organizer's own scenarios of the form, the rounds' and the series', go up first (PUT /v1/maps/:sha256), each once, then the cup is
// made (POST /v1/tournaments).
// A scenario that changed since it was chosen fails with "map_changed".
void LeagueTourneyCreate(const TourneyCupForm &form);
// The same for a change of the organizer's cup (PATCH /v1/tournaments/:id); `descriptionOnly` once someone else signed up.
void LeagueTourneyEdit(int64_t tournament, const TourneyCupForm &form, bool descriptionOnly);
// The scenarios going up for a cup being made or changed: which of how many, and its bytes.
struct TourneyUploadView {
	bool active = false;
	int index = 0, count = 0;
	uint64_t done = 0, total = 0;
	std::string title;
};
TourneyUploadView LeagueTourneyUploading();
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

// The league's random map types with the sizes they let four players in on (GET /v1/league/maps), read once and every hour; empty until
// they come, when TourneySizesFor falls back on the templates' own.
std::vector<TourneyMapType> LeagueTourneyMapTypes();

// The scenario of a cup's round on this computer.
enum class CupMapStatus { Checking, Ready, Downloading, Failed, Missing, Different };
struct CupMapState {
	CupMapStatus status = CupMapStatus::Checking;
	uint64_t got = 0, total = 0;
	// Failed: "not_found" (the league does not have it), "network", "hash" (what came is not the file), "save", "too_large" or "pack".
	std::string reason;
	// The content pack that brings its own copy of the file, when that is what is in the way.
	std::string pack;
};
// A round's map on this computer; Ready for a random one. A scenario is looked for the first time it is asked and every minute after;
// an uploaded one the player lacks is downloaded from the league (GET /v1/maps/:sha256), checked and kept in Scenarios.
CupMapState LeagueCupMap(const TourneyMap &m);
// The downloads of these maps that failed are tried again.
void LeagueCupMapsRetry(const std::vector<TourneyMap> &maps);

// A cup being made and a code being looked up go by the id 0.
bool LeagueTourneyBusy(TourneyOp op, int64_t id);
// The answers, oldest first, each once. Presence answers are not given: they only update the player's next match.
bool LeagueTourneyTakeResult(TourneyOpResult &out);
// What to tell the player in any menu, each once: a match ready, a check-in open, one of their tournaments over.
bool LeagueTourneyTakeNotice(TourneyNotice &out);
