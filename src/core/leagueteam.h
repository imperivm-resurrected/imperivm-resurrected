// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"

// The ranked 2v2 queue as the mod reads it (PLAN-LIGA.md, "2 contra 2"): the duo, the two sides of a match, the five minutes a player
// who dropped has to come back and the team's vote to surrender. Nothing here talks to the network or the game.

constexpr const char *kLeagueQueueDuel = "1v1";
constexpr const char *kLeagueQueueTeam = "2v2";
// The "mode" the host's Steam room of a ranked 2v2 publishes: every computer reads it before the game, so they all treat its drops alike.
constexpr const char *kLeagueTeamRoomMode = "Ranked 2v2";
// The room of a cup match of teams: its team plays as a ranked 2v2's does (the partner's vote to surrender, the drops' notices).
constexpr const char *kLeagueTeamCupRoomMode = "Tournament 2v2";
// "2v2" for the team queue, "1v1" for anything else (the server's ids; empty is the duel).
std::string LeagueQueueId(const std::string &queue);
bool LeagueQueueIsTeam(const std::string &queue);
// The player's standing in a queue: `ranked2v2` for the team queue.
const LeagueRanked &LeagueMeRanked(const LeagueMe &me, const std::string &queue);

// GET /v1/league/party: the duo the player is in ({id, leader, members, expiresAt} or null) and the invitations they got.
struct LeaguePartyMember {
	std::string steamId, name;
	bool accepted = false;
};
struct LeagueParty {
	bool present = false;
	std::string id, leader;
	std::vector<LeaguePartyMember> members;
	int64_t expiresAt = 0;
};
struct LeaguePartyInvite {
	std::string id, fromId, fromName;
	int64_t expiresAt = 0;
};
struct LeaguePartyState {
	LeagueParty party;
	std::vector<LeaguePartyInvite> invites;
};
// {party, invites}, and the {party} the invitation answers with. False when it is not an object.
bool LeagueParseParty(const Json &j, LeaguePartyState &out);
LeagueParty LeaguePartyOf(const Json *v);
// The other member of the duo, or null.
const LeaguePartyMember *LeaguePartyPartner(const LeagueParty &p, const std::string &me);
// Both members accepted.
bool LeaguePartyReady(const LeagueParty &p);

// What the duo lets the player do in the 2v2 queue: alone they search for themselves; with an invitation out, they wait for their
// friend; in a duo the leader searches for both and the other one follows.
enum class LeagueDuoRole { Solo, Waiting, Leader, Member };
LeagueDuoRole LeagueDuoRoleOf(const LeagueParty &p, const std::string &me);
// The invitations not in `seen` (their ids), for the notice of a new one.
std::vector<LeaguePartyInvite> LeagueNewInvites(const std::vector<LeaguePartyInvite> &now, const std::vector<std::string> &seen);
// How often to ask for the duo (GET /v1/league/party), which every linked player does in every menu: every 5 s while the 2v2 is on
// screen or an invitation waits for an answer, every minute in a duo and every 2 alone (an invitation lasts 5); while the player is
// `away`, every minute with an invitation waiting, 5 in a duo and 10 alone; every 10 from a server without the duo (`missing`).
int LeaguePartyPollMs(bool missing, bool waiting, bool screen, bool inDuo, bool away);
// How often the partner of a duo, not searching, asks the queue whether the leader started (the server's party does not say): the
// match found then has 20 s to be accepted. Every 5 s with the 2v2 on screen, 10 in the other menus, 15 with the game behind other
// windows (a match found flashes it).
int LeagueDuoQueuePollMs(bool screen, bool inFront);

// The requests whose path says the queue. The duel's are the ones a server before the 2v2 knows (only the leaderboard names its queue);
// the 2v2's add queue=2v2. Values that need it come URL-encoded.
std::string LeagueBoardPath(const std::string &queue);
std::string LeagueHistoryPath(int64_t before, int limit, const std::string &queue);
std::string LeaguePlayerPath(const std::string &steamId, int season, const std::string &queue);
std::string LeagueHallPath(int season, const std::string &lang, const std::string &queue);
std::string LeagueCeremonySeenPath(int season, const std::string &queue);
// POST /v1/league/party/:id/accept or /decline.
std::string LeaguePartyAnswerPath(const std::string &inviteId, bool accept);

// The sides of a 2v2 ticket: its `teams`, or its players grouped by their `team` when it sends none.
std::vector<LeagueMatchTeam> LeagueTicketTeams(const LeagueMatchTicket &t);
// The side of a SteamID among the teams; 0 when it is in none.
int LeagueSideOf(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId);
// The player of the ticket with that SteamID, with what the ticket's players say of it (accepted, in the room); null when absent.
const LeaguePlayerCard *LeagueTicketCard(const LeagueMatchTicket &t, const std::string &steamId);
// How many of the ticket's players accepted.
int LeagueAcceptedCount(const LeagueMatchTicket &t);
// The first player of `steamId`'s side who is not them, from the teams; null when there is none.
const LeaguePlayerCard *LeagueTeamPartner(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId);
// The players of the other side.
std::vector<LeaguePlayerCard> LeagueTeamOpponents(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId);

// A player of a ranked 2v2 who drops is played by the AI and has 5 minutes of the game's clock to come back; then they left it for good
// (2-clasificatoria-sistema.md §8.3).
constexpr uint32_t kLeagueTeamDropWaitMs = 5 * 60 * 1000;
// Whether the wait of a player who dropped at `leftAtMs` of the game is over at `clockMs`.
bool LeagueTeamDropOver(uint32_t leftAtMs, uint32_t clockMs);
// What is left of it; 0 once over.
uint32_t LeagueTeamDropLeftMs(uint32_t leftAtMs, uint32_t clockMs);

// The vote to surrender a ranked 2v2: from minute 5, pressing Surrender proposes it to the partner, who has 30 s to say yes; only then
// does the team surrender. A player whose partner is gone surrenders at once.
constexpr int64_t kLeagueVoteMs = 30000;
enum class LeagueSurrenderStep { TooEarly, Alone, Propose, Pending };
LeagueSurrenderStep LeagueSurrenderOnPress(uint32_t gameMs, bool partnerHere, bool voteOpen, bool ranked = true);

// A vote as the game's messages left it: who proposed it (the game's player index), when (the game's clock, the same on every computer at
// that tick) and whether it is still open.
struct LeagueVote {
	bool open = false;
	int proposer = -1;
	int64_t since = 0;
};
void LeagueVoteOpen(LeagueVote &v, int proposer, int64_t nowMs);
// What is left of the 30 s at the game's clock `nowMs`; 0 once over.
int64_t LeagueVoteLeftMs(const LeagueVote &v, int64_t nowMs);
bool LeagueVoteExpired(const LeagueVote &v, int64_t nowMs);
// An answer the game ran at `atMs` of its clock counts: the vote is open and its 30 s had not passed then.
bool LeagueVoteAnswerCounts(const LeagueVote &v, int64_t atMs);

// The vote travels as a message command of the game, which every computer runs on the same tick: "#IRV:p" proposes, "#IRV:y" and
// "#IRV:n" answer.
enum class LeagueVoteVerb { None, Propose, Yes, No };
std::string LeagueVoteMessage(LeagueVoteVerb verb);
LeagueVoteVerb LeagueVoteParse(const char *text, unsigned size);

// The side that stayed ends a game whose other side left it for good (the drop rules decided it) through the game's own defeat of each
// player gone, in a message command every computer, and a spectator of the broadcast, runs on the same tick: "#IRG:k", k the game's index
// of the player (0-7). Empty, or -1, for anything else.
std::string LeagueGoneMessage(int player);
int LeagueGoneParse(const char *text, unsigned size);
