// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/leaguestats.h"
#include "core/leagueteam.h"
#include "core/matchhistory.h"

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
	// While waiting: the code to type on the website and its page, and the server's page that links with one Steam sign-in
	// (empty when the server has none: then the player types the code).
	std::string code, url, steamUrl;
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
// Asks the server for a code and opens its Steam sign-in page in the Steam overlay (the browser when the overlay is off), then
// waits, polling, until the player signs in there or types the code on the website, or the code expires.
void LeagueLinkStart();
void LeagueLinkCancel();
// Revokes the token on the server and forgets it here.
void LeagueUnlink();
// The page of the code again: the Steam sign-in when the server gave one, the website's page otherwise.
void LeagueOpenLinkPage();
// The player said "Not now" to the offer to link: Ranked and Tournaments no longer offer it on their own (kept in the
// configuration).
bool LeagueLinkOfferDeclined();
void LeagueDeclineLinkOffer();

struct LeagueMeView {
	bool loaded = false, loading = false;
	// The last answer did not come (no connection, the server down): what the screen tells instead of waiting.
	bool failed = false;
	LeagueMe me;
};
LeagueMeView LeagueMeState();
// The same view shared, never changed once given (an answer makes a new one): for what reads it every frame, without a copy.
std::shared_ptr<const LeagueMeView> LeagueMeShared();
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
	// The room says it is a ranked 2v2 (its "mode", kLeagueTeamRoomMode): the same on every computer before the game starts.
	bool team = false;
	// Steam id and name of the room's members, to tell who is who among the game's players.
	std::vector<std::pair<uint64_t, std::string>> members;
};

// On the game thread, every few seconds while this computer plays an online game (never as a spectator): starts recording it, or
// keeps recording it after a rejoin.
void LeagueMatchPlaying(const LeagueMatchInfo &info);
// The league and cup games this computer started, as noted when each started (the statistics tell them by it).
std::vector<MatchTag> LeagueGameTags();
// The game is over for this computer (it is back in the menu). The report goes to the server in the background when the account is
// linked; when it cannot be sent, it waits on disk and is tried again.
void LeagueMatchEnded();
// After LeagueMatchEnded: the online game reached its end (a winner, a surrender, a decided drop), so it is not one to come back to.
bool LeagueLastGameOver();
void LeagueMatchSurrendered(int player);
void LeagueMatchDesync();
// The online game being played is a ranked one (its room's tag says so).
bool LeagueMatchRanked();
// A ranked game cannot be surrendered yet: its clock is under minute 5.
bool LeagueSurrenderLocked();

// A ranked or a cup game being played (its room's tag), the same on every computer from the game's start: the game goes on, whoever
// drops is played by the AI and may come back within 5 minutes of the game's clock; who does not has left it (2-clasificatoria-sistema.md
// §8.3, 4-torneos.md §6).
bool LeagueMatchDropRules();
// A ranked 2v2 being played (its room's tag and mode), the same on every computer from the game's start.
bool LeagueMatchRankedTeam();
// A player of a game under the drop rules left it on purpose: they are gone for good (in a 2v2 the AI keeps their army).
void LeagueMatchPlayerQuit(int player);
// The player of that index dropped less than 5 minutes ago (or the game does not know): their return is let in.
bool LeagueTeamDropOpen(int player);
// The players of the 2v2 who dropped and still have time to come back, and the team's vote to surrender, for the notices over the game.
struct LeagueTeamDrop {
	std::string name;
	bool partner = false;
	uint32_t leftMs = 0;
};
enum class LeagueTeamVote { None, Proposed, Asked };
struct LeagueTeamView {
	bool active = false;
	std::vector<LeagueTeamDrop> drops;
	// Proposed: this player asked the partner, who has voteLeftMs to answer. Asked: the partner proposed it and waits for this one.
	LeagueTeamVote vote = LeagueTeamVote::None;
	int64_t voteLeftMs = 0;
	std::string partner;
};
LeagueTeamView LeagueTeamState();
// The game menu's Surrender in a ranked 2v2: NotTeam and Alone let the game surrender as always (no 2v2, or the partner is not in the
// game); TooEarly refuses it before minute 5; Proposed sent the vote to the partner, Pending finds one already open.
enum class LeagueSurrenderAsk { NotTeam, TooEarly, Alone, Proposed, Pending };
LeagueSurrenderAsk LeagueSurrenderPress();
// This player's answer to the partner's proposal: on yes, both surrender.
void LeagueVoteAnswer(bool yes);
// A message command of the game, run on every computer: true when it was the vote's (and then it is not shown).
bool LeagueVoteCommand(void *cmd, const char *text, unsigned size);
// From the lockstep, after every tick: keeps the state hash every client compared for the ticks that are multiples of 1000.
void LeagueSyncAfterAdvance(void *transport);
// Every frame on the game thread: follows the players of the game being recorded, polls a pending link, sends waiting reports and
// follows the ranked queue.
void LeagueGameThreadTick();

// The ranked queues (1v1 and 2v2). Searching goes on while the player looks at other screens; the server pairs players when they poll.
//   Idle: not searching. Joining: asking to enter the queue. Searching: in the queue, polled every 5 s (the partner of a duo follows the
//   leader's search this way too).
//   Found: a match waits for this player's answer (Accepted once they said yes and the other one has not yet).
//   Ready: both accepted; the server's host makes the Steam room. Playing: the room is registered and the game may be running.
enum class LeagueRankedPhase { Idle, Joining, Searching, Found, Accepted, Ready, Playing };

// The ranked game just played on this computer, for the result screen (the list of games is the server's).
struct LeagueRankedGame {
	int64_t matchId = 0, at = 0;
	// `map` is the league's id of the map.
	std::string opponent, opponentId, map;
	// "1v1" or "2v2"; a 2v2's partner (whether they left it for good) and both opponents, as SteamID and name.
	std::string queue = "1v1", partner, partnerId;
	bool partnerLeft = false;
	std::vector<std::pair<std::string, std::string>> opponents;
	bool won = false;
	// This player left the game before it was decided: the league locks the queue for them.
	bool left = false;
	int delta = 0;
	uint32_t durationMs = 0;
	// "confirmed", "awaiting_other", "disputed"... as the last answer said; empty until the server answered.
	std::string status;
	LeagueTier tierAfter = LeagueTier::None;
	int divisionAfter = 0;
	// Each player's civilization as the report gave it (the game's id), by SteamID.
	std::vector<std::pair<std::string, std::string>> civs;
	// What this player did, from the game's own record: units killed, lost and produced, the largest army, the gold gathered and the unit that
	// reached the highest level (the game's name for it, for GameText). `stats` is false when the game could not be read.
	bool stats = false;
	uint32_t killed = 0, lost = 0, armyMax = 0, gold = 0, bestLevel = 0, produced = 0;
	std::string bestUnit;
};

// A page of the full leaderboard: `limit` rows from `offset`, or the page around the player; of a season (-1: the one being played);
// only the names with `name` in them (two letters at least) and, with `friends`, only the players of `ids` (at most 200).
struct LeagueBoardQuery {
	int offset = 0, limit = 13;
	bool aroundMe = false;
	int season = -1;
	std::string name;
	bool friends = false;
	std::vector<std::string> ids;
	// The queue's leaderboard; LeagueBoardRequest puts the one picked (LeagueRankedSelectQueue).
	std::string queue = "1v1";
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
	// The queue the hub shows and searches ("1v1" or "2v2"): the leaderboard, the games and the figures below are the ones of it.
	std::string queue = "1v1";
	// The queue of the search or the match on now, and whether it is the leader of this player's duo who searches.
	std::string searchQueue = "1v1";
	bool partnerSearch = false;
	bool seasonLoaded = false, seasonLoading = false, noSeason = false;
	// The last answer for the season, or for the leaderboard, failed: shown as such while it is asked again.
	bool seasonFailed = false, boardFailed = false;
	LeagueSeason season;
	std::string lang;
	// The first ten and the page around the player, for the hub.
	bool boardLoaded = false;
	LeagueBoard top, around;
	// The page the full leaderboard asked for, and what it asked.
	bool pageLoading = false, pageLoaded = false;
	LeagueBoard page;
	LeagueBoardQuery pageQuery;
	std::string pageError;
	LeagueRankedPhase phase = LeagueRankedPhase::Idle;
	// While searching: since when (server time, ms since 1970), how many search and the wait the server expects.
	int64_t since = 0;
	int searching = 0, estimateSec = 0;
	// Searching 1 vs 1 on the staging server, which can put its practice bot against the search (LeagueRankedPracticeBot).
	bool practiceBot = false;
	// The Glory of the opponents the server takes now (no window from a server without it).
	LeagueSearchWindow window;
	// The match found or being played.
	int64_t matchId = 0;
	LeagueQueueAnswer found;
	bool ticketLoaded = false;
	LeagueMatchTicket ticket;
	// This player's side and the other one in the ticket.
	LeaguePlayerCard me, opponent;
	bool host = false, roomRegistered = false, answering = false;
	// The match's game started on this computer (it may have closed since): before the ticket says so too, no room is made or joined for
	// it again.
	bool startedHere = false;
	// Calling off the match is on its way; the last match called off before its game.
	bool abandoning = false;
	int64_t calledOff = 0;
	// The queue is locked until then (server time, ms since 1970), and why ("declined", "abandoned", "desync"; empty when not said).
	int64_t banUntil = 0;
	std::string banKind, banReason;
	// The last thing that happened, as an English text to translate (with its values filled in by the server's), and whether it is bad.
	std::string notice;
	bool noticeBad = false;
	// The season is played with other gameplay content (wrong_content): its key and name, and a count of the times the queue said so.
	std::string seasonContentKey, seasonContentName;
	unsigned wrongContentSerial = 0;
	// Counts the matches found, so the interface can call the player once per match.
	unsigned foundSerial = 0;
	LeagueRankedOutcome result;
	// The player's latest ranked games for the hub, newest first.
	bool recentLoaded = false, recentFailed = false;
	std::vector<LeagueHistoryGame> recent;
	// The page of the full history: the games played before `historyBefore` (0: the newest).
	bool historyLoading = false, historyLoaded = false;
	int64_t historyBefore = 0;
	LeagueHistoryPage history;
	std::string historyError;
	// The page of one game of the history (GET /v1/league/matches/:id/detail): which game, and its answer.
	bool detailLoading = false, detailLoaded = false;
	int64_t detailId = 0;
	LeagueMatchDetail detail;
	std::string detailError;
	// Who uses the queue now.
	bool statsLoaded = false;
	LeagueStats stats;
	// "Report a problem": a report on its way, the matches reported in this session and the last answer (its serial changes with each).
	bool disputeBusy = false;
	std::vector<int64_t> disputed;
	std::string disputeNotice;
	bool disputeBad = false;
	unsigned disputeSerial = 0;
	// Choosing the badge.
	bool badgeBusy = false;
	std::string badgeError;
};
LeagueRankedView LeagueRankedState();
// Picks the queue the hub shows and searches: its leaderboard and games are read again.
void LeagueRankedSelectQueue(const std::string &queue);
// GET /v1/league/season in the language of the game, at most every ten minutes unless forced.
void LeagueSeasonRefresh(bool force = false);
// The first ten and the player's place, at most once a minute unless forced.
void LeagueBoardRefresh(bool force = false);
void LeagueBoardRequest(const LeagueBoardQuery &query);
// The hub's latest ranked games (GET /v1/league/matches?mine=1), at most once a minute unless forced.
constexpr int kLeagueRecentGames = 5, kLeagueHistoryPage = 10;
void LeagueRecentRefresh(bool force = false);
// A page of the full history: the games played before `before` (0: the newest).
void LeagueHistoryRequest(int64_t before);
// The page of a finished ranked game, for anyone: who played and every player's numbers through it.
void LeagueMatchDetailRequest(int64_t matchId);
// The 1v1 record of `a` against `b` (GET /v1/league/players/:a/vs/:b): asked once a pair, kept until another pair is asked.
struct LeagueHeadToHeadView {
	bool loaded = false, loading = false;
	std::string a, b;
	LeagueHeadToHead h2h;
};
void LeagueHeadToHeadRequest(const std::string &a, const std::string &b);
LeagueHeadToHeadView LeagueHeadToHeadState();
// GET /v1/league/stats, at most every 15 s.
void LeagueStatsRefresh();
// "Report a problem" with a ranked game (POST /v1/league/matches/:id/dispute): the result stays, and the league reviews it.
void LeagueMatchDispute(int64_t matchId, const std::string &text);
// The league server's clock now (ms since 1970), from this computer's corrected by the serverNow of its answers; and how far ahead it is.
int64_t LeagueServerNowMs();
int64_t LeagueServerClockOffsetMs();
// Enters the queue picked with this computer's gameplay content (the 2v2 alone, or as the leader of a duo). Refused (with a notice) when
// not linked, banned or already in a match, or when the duo is not ready or this player does not lead it.
void LeagueQueueJoin();
void LeagueQueueLeave();
void LeagueMatchAnswer(bool accept);
// The host's Steam room of the match is open: the server gets its id and the other player joins it.
void LeagueRankedRoomOpened(uint64_t lobby);
// Polls the match again now: the room failed and the interface tries once more.
void LeagueRankedRetryRoom();
// Calls off the match before its game starts, when its room can't be made or joined: no Glory changes, and only whoever clearly
// failed gets the lock (the server decides). `automatic` when the room's own retries gave up.
void LeagueMatchAbandon(bool automatic);
// A 1v1 whose game closed on this player and that they cannot go back to: given up, it is a loss and the opponent wins at once.
void LeagueRankedSurrender();
// The match's room is open on this computer: "I am in the room" goes to the server every 30 s, which is what tells a room that
// worked for both players from one that did not.
void LeagueRankedInRoom();
// The player is ready in the match room, or not: told to the server when it changes (the room has a minute to start).
void LeagueRankedReady(bool ready);
// The staging server only: a practice bot (the game's AI in its seat) is put against this 1 vs 1 search.
void LeagueRankedPracticeBot();
// The player closed the result screen.
void LeagueRankedResultSeen();
void LeagueRankedClearNotice();
// PUT /v1/league/me/badge: "rank", "season" or "trophy" (with its id) or "none".
void LeagueBadgeChoose(const std::string &kind, const std::string &id);

// The duo of the 2v2 queue (GET /v1/league/party): asked every 5 s while the 2v2 is on screen or an invitation waits, every 20 s in the
// other menus (so an invitation is seen anywhere) and never during a game.
struct LeagueDuoView {
	// `missing`: the server has no duo (before the 2v2), asked again only now and then.
	bool loaded = false, busy = false, missing = false;
	LeagueParty party;
	std::vector<LeaguePartyInvite> invites;
	LeagueDuoRole role = LeagueDuoRole::Solo;
	// The other member, when there is one.
	LeaguePartyMember partner;
};
LeagueDuoView LeagueDuoState();
// Only this player's role in it, for the checks made every frame.
LeagueDuoRole LeagueDuoOwnRole();
// The 2v2 is on screen: the duo is asked closely for a few seconds.
void LeagueDuoWatch();
// POST /v1/league/party/invite: a Steam friend (their name for the notices).
void LeagueDuoInvite(const std::string &steamId, const std::string &name);
// POST /v1/league/party/:id/accept or /decline.
void LeagueDuoAnswer(const std::string &inviteId, bool accept);
// DELETE /v1/league/party: leaves the duo (or calls off the invitation sent).
void LeagueDuoLeave();
// What happened with the duo since the last call, already translated: invitations got and answered, the partner leaving, refusals.
struct LeagueDuoNotice {
	std::string text;
	bool bad = false;
};
std::vector<LeagueDuoNotice> LeagueDuoTakeNotices();
