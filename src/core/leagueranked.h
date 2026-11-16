// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"

// The ranked queues of the league server (1v1 and 2v2), as the mod reads them: the season, the queue, the match ticket, the ladder and
// what a report gave, plus the maths of the screens (the Glory bar, the countdown and the result's animation). Nothing here talks to the
// network or draws.

// The room options the server fixes for every ranked match; -1 (or empty) where the season says nothing.
struct LeagueRoomSettings {
	std::string preset;
	int hideCivs = -1, fog = -1, explored = -1, cheats = -1;
	std::string diplomacy;
	// Seconds of delay for spectators; 0 without them, -1 when not said.
	int specDelay = -1;
	int pauseCount = -1, pauseSeconds = -1;
};
LeagueRoomSettings LeagueParseRoomSettings(const Json *v);

// GET /v1/league/season.
struct LeagueSeason {
	int number = 0;
	std::string name, theme, contentKey;
	// The season's name in every language the server has ("es", "en", "it").
	std::map<std::string, std::string> names;
	int64_t startsAt = 0, endsAt = 0;
	bool beta = false, active = false;
	std::vector<LeagueMapFacts> maps;
	LeagueRoomSettings settings;
	// The queues the season plays ("1v1", "2v2"; only the duel from a server that does not say) and the 2v2 queue's maps for 4 players.
	std::vector<std::string> queues;
	std::vector<LeagueMapFacts> maps2v2;
};
bool LeagueParseSeason(const Json &j, LeagueSeason &out);
bool LeagueSeasonHasQueue(const LeagueSeason &s, const std::string &queue);
// The name in `lang` ("es", "en" or "it"), the one the server chose otherwise.
std::string LeagueSeasonName(const LeagueSeason &s, const std::string &lang);
// Whole days left until the end, rounded up; 0 once it is over.
int LeagueDaysLeft(int64_t endsAt, int64_t nowMs);
// How much of the season has gone, 0 to 1.
float LeagueSeasonElapsed(const LeagueSeason &s, int64_t nowMs);
// "es", "en" or "it" from the game's language ("spanish", "Italiano"...): English for anything else.
std::string LeagueLangCode(const std::string &gameLanguage);

// A player as the queue and the ticket show them.
struct LeaguePlayerCard {
	std::string steamId, name;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	bool accepted = false, host = false;
	// Said "I am in the room" in the last 90 s (only in the ticket).
	bool inRoom = false;
	// The record the found match and the ticket give of the opponent: wins, losses, Glory and place in the leaderboard (0 while
	// unranked). `standing` is false when the server sent none of them.
	bool standing = false;
	int wins = 0, losses = 0, glory = 0, place = 0;
	// Games won (positive) or lost (negative) in a row, when the server said.
	bool streakKnown = false;
	int streak = 0;
	// 2v2: the player came with a duo partner, and their side in the ticket (1 or 2; 0 in a 1v1).
	bool duo = false;
	int team = 0;
};

// A side of a 2v2 match: 1 or 2, and its two players.
struct LeagueMatchTeam {
	int side = 0;
	std::vector<LeaguePlayerCard> players;
};

// The Glory of the opponents the queue takes now (2-clasificatoria-sistema.md §2.4): an end it does not hold is -1.
struct LeagueSearchWindow {
	bool present = false;
	int from = -1, to = -1;
};

enum class LeagueQueueStatus { Unknown, Idle, Searching, Found };

// POST and GET /v1/league/queue.
struct LeagueQueueAnswer {
	LeagueQueueStatus status = LeagueQueueStatus::Unknown;
	// "1v1" or "2v2"; empty from a server that does not say.
	std::string queue;
	int64_t since = 0;
	int searching = 0, estimateSec = 0;
	// The server's clock when it answered (ms since 1970); 0 when it does not say.
	int64_t serverNow = 0;
	// Found: the match waiting for answers. Idle: the match being played, if any (a restarted mod finds it so).
	int64_t matchId = 0;
	LeaguePlayerCard opponent;
	LeagueMapFacts map;
	int64_t acceptBy = 0;
	bool accepted = false;
	// Searching: the Glory of the opponents the server takes now. Found: this player's chance to win, 0 to 1 (Glicko-2); -1 when not
	// said.
	LeagueSearchWindow window;
	double expected = -1;
	// Found in 2v2 (`opponent` is empty): both sides with their players, and this player's side; `expected` is their team's chance.
	std::vector<LeagueMatchTeam> teams;
	int mySide = 0;
};
bool LeagueParseQueue(const Json &j, LeagueQueueAnswer &out);

enum class LeagueMatchStatus { Unknown, Accepting, Ready, Playing, Done, Cancelled };

// The `ranked` block of a report's answer and of a finished ticket: what one game did to the player's standing.
struct LeagueRankedResult {
	bool present = false;
	int gloryBefore = 0, gloryAfter = 0, delta = 0;
	LeagueTier tierBefore = LeagueTier::None, tierAfter = LeagueTier::None;
	int divisionBefore = 0, divisionAfter = 0;
	bool promoted = false, demoted = false;
	int shield = 0, placementLeft = 0;
	// The game that ended the placement: it gives a rank, not a change of Glory (`delta` is null and reads 0).
	bool placed = false;
};
LeagueRankedResult LeagueParseRankedResult(const Json *v);

// GET /v1/league/matches/:id, the answers to accept and to the room.
struct LeagueMatchTicket {
	int64_t id = 0;
	LeagueMatchStatus status = LeagueMatchStatus::Unknown;
	// "1v1" or "2v2"; empty from a server that does not say.
	std::string queue;
	std::string host, lobby, lobbyKey, contentKey, outcome;
	LeagueMapFacts map;
	LeagueRoomSettings settings;
	int64_t acceptBy = 0;
	// When both accepted, until when calling off a match without a room is the caller's fault (afterwards the host's), and when
	// its game started (0 while it has not).
	int64_t readyAt = 0, roomDueAt = 0, startedAt = 0;
	// A match called off before its game (outcome "abandoned"): who called it off (a SteamID or "system") and who got the lock.
	std::string abandonedBy, fault;
	std::vector<LeaguePlayerCard> players;
	LeagueRankedResult ranked;
	int64_t serverNow = 0;
	// The other player as the one who asks sees them, with their record, and this player's chance to win (-1 when not said).
	LeaguePlayerCard opponent;
	double expected = -1;
	// 2v2: both sides and this player's side; `players` carry their `team` too.
	std::vector<LeagueMatchTeam> teams;
	int mySide = 0;
};
bool LeagueParseMatch(const Json &j, LeagueMatchTicket &out);
// The player of the ticket with that SteamID, or the other one; null when there is none.
const LeaguePlayerCard *LeagueTicketPlayer(const LeagueMatchTicket &t, const std::string &steamId, bool other);
// The lobby the server gives, as a number; 0 when there is none yet.
uint64_t LeagueTicketLobby(const LeagueMatchTicket &t);
// "ranked:<id>", what the host's Steam room carries in its "irm" data.
std::string LeagueRankedRoomTag(int64_t matchId);
// The match may still be called off: both accepted and its game has not started.
bool LeagueTicketAbandonable(const LeagueMatchTicket &t);
// Whether calling it off now would lock that player out of the queue, as the server decides it: without a room, whoever walks away
// before roomDueAt and the host after it; with a room every player is in, whoever leaves it; with one never reached, nobody.
bool LeagueAbandonCosts(const LeagueMatchTicket &t, const std::string &steamId, int64_t nowMs);

struct LeagueBoardRow {
	int position = 0;
	std::string steamId, name;
	LeagueTier tier = LeagueTier::None;
	int division = 0, glory = 0, wins = 0, losses = 0;
	// The Glory won over the last 7 days; none during the placement.
	bool hasDelta7 = false;
	int delta7 = 0;
	// +3: three wins in a row, -2: two losses.
	int streak = 0;
};

// GET /v1/league/leaderboard.
struct LeagueBoard {
	int season = 0, total = 0, offset = 0;
	// The player's own place with around=me; 0 when not ranked.
	int me = 0;
	std::vector<LeagueBoardRow> rows;
};
bool LeagueParseBoard(const Json &j, LeagueBoard &out);
// The row of a SteamID in the page, or null.
const LeagueBoardRow *LeagueBoardFind(const LeagueBoard &b, const std::string &steamId);
// The offset of the page of `limit` rows that holds `position` (1 = first).
int LeagueBoardPageOf(int position, int limit);
// Rounded win rate in percent; 0 without games.
int LeagueWinRate(int wins, int losses);

// POST /v1/league/matches/report.
struct LeagueReportAnswer {
	std::string status, outcome;
	LeagueRankedResult ranked;
};
bool LeagueParseReportAnswer(const Json &j, LeagueReportAnswer &out);

// The body of an error: {error, message} and what some errors add.
struct LeagueError {
	std::string code, message, contentKey;
	// wrong_content: the name of the content the season is played with.
	std::string contentName;
	// Why a player is locked out of the queue (409 banned): "declined", "abandoned", "desync" or "staff", and the team's reason for a
	// staff lock.
	std::string kind, reason;
	int64_t until = 0, matchId = 0;
	// Too many requests (429 rate_limited): whole seconds to wait before asking again; 0 when not said.
	int retryAfter = 0;
	// The two of a duo are too far apart (409 rank_gap): their gap of Glory and the most the queue takes.
	int gap = 0, max = 0;
	// "partner" when the error is the duo partner's (their lock, their match), not this player's; the player an error is about
	// (already_in_party: whose duo).
	std::string who, steamId;
};
LeagueError LeagueParseError(const Json &j);
// How long to wait before the next poll: after a 429, what the server asks (at least 1 s, 5 s when it does not say), and never
// less than `normalMs`; after anything else, `normalMs`.
int64_t LeagueRetryAfterMs(int httpStatus, const LeagueError &e, int64_t normalMs);
// Why the queue is locked, as the English text to translate: a match declined or let go (also when the server does not say), a game
// left, or too many games out of sync.
const char *LeagueBanText(const std::string &kind);

// GET /v1/league/stats: who uses the queue now, for the hub before searching; in all, and by queue ("1v1", "2v2") when the server says.
struct LeagueQueueStats {
	int searching = 0, playing = 0, gamesToday = 0;
};
struct LeagueStats {
	int searching = 0, playing = 0, gamesToday = 0;
	int64_t serverNow = 0;
	std::map<std::string, LeagueQueueStats> byQueue;
};
bool LeagueParseStats(const Json &j, LeagueStats &out);
// The figures of one queue: the server's for it; without them, the totals for the duel and nothing for the 2v2.
LeagueQueueStats LeagueStatsOf(const LeagueStats &s, const std::string &queue);

// How far ahead of this computer's clock the server's is, from an answer's serverNow and this computer's clock when the request left
// and when the answer came (it was written about halfway). 0 without serverNow.
int64_t LeagueClockOffsetMs(int64_t serverNow, int64_t sentAtMs, int64_t receivedAtMs);

// One ranked game of the player, as GET /v1/league/matches?mine=1 lists it.
enum class LeagueGameResult { Pending, Win, Loss, Void, Disputed };
struct LeagueHistoryGame {
	int64_t id = 0, playedAt = 0;
	uint32_t durationMs = 0;
	LeagueMapFacts map;
	LeaguePlayerCard opponent;
	// 2v2: the partner and both opponents with their rank; `opponent` is then the first of them.
	LeaguePlayerCard partner;
	std::vector<LeaguePlayerCard> opponents;
	std::string myCiv, opponentCiv, endedBy;
	LeagueGameResult result = LeagueGameResult::Pending;
	// The Glory it gave: none until it counts, nor for the game that ended the placement (`placed`).
	bool hasDelta = false;
	int gloryDelta = 0;
	bool placed = false;
};
struct LeagueHistoryPage {
	std::vector<LeagueHistoryGame> games;
	bool more = false;
};
bool LeagueParseHistory(const Json &j, LeagueHistoryPage &out);
// One game of such a list; false when it is not one.
bool LeagueParseHistoryGame(const Json &m, LeagueHistoryGame &out);
// The `before` of the next, older page: when the last game of this one was played; 0 when there is none.
int64_t LeagueHistoryNextBefore(const LeagueHistoryPage &p);
// "Report a problem" stays open for a day after the game.
constexpr int64_t kLeagueDisputeWindowMs = 24LL * 3600 * 1000;
bool LeagueDisputeOpen(int64_t playedAt, int64_t nowMs);

// The place in the leaderboard only shows in its top half: below it, the bar to the next division says more.
bool LeaguePlaceShown(int position, int total);
// The line that closes the Imperators: under the last one, when someone who is not one follows.
bool LeagueImperatorLineAfter(const LeagueBoardRow &row, const LeagueBoardRow *next);

// A ranked game can only be surrendered from minute 5 of the game's clock.
constexpr uint32_t kLeagueSurrenderFromMs = 5 * 60 * 1000;
bool LeagueSurrenderAllowed(bool ranked, uint32_t gameMs);

// A ranked 1v1 whose opponent dropped waits 3 minutes for them, paused (2-clasificatoria-sistema.md §8.3); then the one who stayed may
// claim the win. The times are this computer's clock in ms: the game's own does not run while it is paused.
constexpr int64_t kLeagueDropWaitMs = 3 * 60 * 1000;
enum class LeagueDropPhase { None, Waiting, Claimable, Claimed };
struct LeagueDropWait {
	bool gone = false, claimed = false;
	int64_t since = 0;
	// The game's time when they left, for the report.
	uint32_t dropAtMs = 0;
};
// The opponent left at `gameMs` of the game (leaving again starts the wait again).
void LeagueDropGone(LeagueDropWait &w, int64_t nowMs, uint32_t gameMs);
// They came back: the game goes on.
void LeagueDropBack(LeagueDropWait &w);
// The player claims the win: only once the wait is over. True when it was taken.
bool LeagueDropClaim(LeagueDropWait &w, int64_t nowMs);
LeagueDropPhase LeagueDropPhaseAt(const LeagueDropWait &w, int64_t nowMs);
// What is left of the wait, never negative.
int64_t LeagueDropLeftMs(const LeagueDropWait &w, int64_t nowMs);

// The accounts of a ranked 1v1 that the game's names could not tell, from its ticket: this computer's seat is `ownId` and the other
// human is the ticket's other player.
void LeagueSeatsFromTicket(std::vector<LeagueSeat> &seats, const LeagueMatchTicket &t, uint64_t ownId);

// Where some Glory leaves a player: their rank and how far into its division (0 to 99). Legate I has no top: `capped` is false there
// and `into` keeps counting.
struct LeagueGloryPlace {
	LeagueTier tier = LeagueTier::Recruit;
	int division = 3, into = 0;
	bool capped = true;
};
LeagueGloryPlace LeagueGloryPlaceOf(int glory);
// The next division up from a tier and division ("Centurion II" -> "Centurion I", "Legionary I" -> "Signifer III"); None at the top.
void LeagueNextDivision(LeagueTier tier, int division, LeagueTier &nextTier, int &nextDivision);
// About how many wins are still needed to reach the next division, at a typical 20 Glory a win; at least 1.
int LeagueWinsToNext(int glory);

// How much of the 20 s window to accept is left, 1 to 0.
constexpr int64_t kLeagueAcceptWindowMs = 20000;
float LeagueCountdownFraction(int64_t acceptBy, int64_t nowMs, int64_t windowMs = kLeagueAcceptWindowMs);
// Whole seconds left, rounded up; never negative.
int LeagueCountdownSeconds(int64_t acceptBy, int64_t nowMs);
// "1:05" or "12:40" for a running time.
std::string LeagueClockText(int64_t ms);

// The badges a player may wear next to their name, in the order the profile lists them: the live rank, every season and trophy of
// theirs, and none. `chosen` marks the one the server has.
struct LeagueBadgeChoice {
	std::string kind, id;
	LeagueBadge look;
	bool chosen = false;
};
std::vector<LeagueBadgeChoice> LeagueBadgeChoices(const LeagueMe &me);

// The result screen's animation (RankedUiAnimacion): the bar, the counter and the emblems at a moment after the screen opened.
enum class LeagueResultKind { Steady, Promotion, Demotion, Placement, PlacementDone };
LeagueResultKind LeagueResultKindOf(const LeagueRankedResult &r);
int LeagueResultDurationMs(LeagueResultKind kind);

struct LeagueResultFrame {
	LeagueResultKind kind = LeagueResultKind::Steady;
	// The rank whose name and emblem are shown now: the old one until the change, the new one after.
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	bool switched = false;
	// The Glory the counter shows, and the Glory won or lost counting from 0 to the game's while it shows.
	int glory = 0, delta = 0;
	// How full the bar is, 0 to 1, and the faint part that is about to be won (or that was lost, `ghostLoss`).
	float bar = 0, ghostFrom = 0, ghostTo = 0;
	bool ghostLoss = false;
	bool deltaShown = false;
	// 0-255: the white flash over the bar, the old emblem fading, the new one coming in and the rays of a new tier.
	int flash = 0, oldAlpha = 255, newAlpha = 0, rays = 0;
	// The new emblem's size, 85 to 100 %.
	int newScale = 100;
	bool shieldShown = false;
	bool done = false;
};
// The frame at `ms` after the screen opened; anything past the end is the end, which is also what skipping shows.
LeagueResultFrame LeagueResultAt(const LeagueRankedResult &r, uint32_t ms);
// The sounds of the result screen: the game won or lost when its Glory shows, and the new rank (or the rank lost) when its emblem
// comes. The ones due by `ms`, as a mask.
constexpr int kLeagueCueOutcome = 1, kLeagueCueRank = 2;
int LeagueResultCues(const LeagueRankedResult &r, uint32_t ms);
