// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"

// What the mod shares with the league server (imperivm-league): the shapes of its answers and of the match report, and the
// decisions that only depend on them. Nothing here talks to the network or the game.

constexpr const char *kLeagueDefaultUrl = "https://api.imperivmresurrected.com";
constexpr const char *kLeagueLinkPage = "https://imperivmresurrected.com/vincular";

// The server's address from the configuration: https anywhere, plain http only to this computer (a local server under
// development). Anything else, or nothing, is the public server. Never ends in a slash.
std::string LeagueBaseUrl(const std::string &configured);
bool LeagueUrlIsDefault(const std::string &base);

// The ranks, from the lowest. Pretorian and Imperator go by position in the leaderboard and have no divisions.
enum class LeagueTier { None, Recruit, Legionary, Signifer, Centurion, Tribune, Legate, Praetorian, Imperator };
constexpr int kLeagueTierCount = 8;

// The server's id ("centurion"), its number (1 is Recruit) or nothing. Accents, case and the English names are accepted.
LeagueTier LeagueTierFromJson(const Json *v);
LeagueTier LeagueTierFromId(const std::string &id);
const char *LeagueTierId(LeagueTier t);
// The English name, the key of its translation.
const char *LeagueTierName(LeagueTier t);
bool LeagueTierHasDivisions(LeagueTier t);
// The Glory a rank starts at: 300 a rank, 100 a division. -1 for the ranks that go by position.
int LeagueTierGloryFrom(LeagueTier t);
// 3, 2 or 1 (III is the lowest); 0 when there is none. Numbers or Roman numerals.
int LeagueDivisionFromJson(const Json *v);
// "III", "II", "I", or empty.
std::string LeagueDivisionText(int division);
// "Centurion II" with the tier's name as given (already translated or not).
std::string LeagueRankText(const std::string &tierName, int division);

enum class LeagueBadgeKind { None, Rank, Season, Trophy };

// The one badge a player shows to the right of their name.
struct LeagueBadge {
	LeagueBadgeKind kind = LeagueBadgeKind::None;
	std::string id;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	bool Shown() const { return kind != LeagueBadgeKind::None; }
	bool operator==(const LeagueBadge &o) const { return kind == o.kind && id == o.id && tier == o.tier && division == o.division; }
};
// {kind, id, tier?, division?}, or null. A rank badge without a known tier is not shown.
LeagueBadge LeagueBadgeFromJson(const Json *v);

// One player of GET /v1/players/lookup.
struct LeagueLookupEntry {
	std::string patronTier;
	int patronPlace = 0;
	LeagueBadge badge;
};
// {"<steamId>": {patron: {tier, place}|null, badge: {...}|null}}. False when it is not an object.
bool LeagueParsePlayersLookup(const Json &j, std::map<uint64_t, LeagueLookupEntry> &out);

struct LeagueRanked {
	bool present = false;
	std::string queue;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	int glory = 0, placementLeft = 0, shield = 0, wins = 0, losses = 0, position = 0;
	bool elite = false;
};

// GET /v1/league/me.
struct LeagueMe {
	std::string steamId, name;
	LeagueRanked ranked;
	int64_t queueBanUntil = 0;
	LeagueBadge badge;
	std::vector<LeagueBadge> badges;
};
bool LeagueParseMe(const Json &j, LeagueMe &out);

// POST /v1/link/start.
struct LeagueLinkCode {
	std::string code, pollToken, url;
	int64_t expiresAt = 0;
};
bool LeagueParseLinkStart(const Json &j, LeagueLinkCode &out);

// GET /v1/link/poll.
enum class LeaguePollStatus { Unknown, Pending, Linked, Expired };
struct LeagueLinkPoll {
	LeaguePollStatus status = LeaguePollStatus::Unknown;
	std::string gameToken, steamId, name;
};
// `httpStatus` 410 is an expired code whatever the body says.
LeagueLinkPoll LeagueParseLinkPoll(int httpStatus, const Json &j);

// "ABCD-2345": upper case, in two halves when it has the server's eight characters.
std::string LeagueCodeDisplay(const std::string &code);
// The address as it is read aloud: "imperivmresurrected.com/vincular", without the scheme, "www." or the query.
std::string LeagueUrlDisplay(const std::string &url);
// A page the browser may open: http(s) only, plain http only on this computer; the link page otherwise.
std::string LeagueBrowserUrl(const std::string &url);

// "<lobbyId>:<startedAt>", the same on every player's report of a game.
std::string LeagueMatchKey(uint64_t lobby, int64_t startedAtMs);

enum class LeagueMatchKind { Normal, Ranked, Tournament };
const char *LeagueMatchKindId(LeagueMatchKind k);
// The tag a ranked or tournament room carries in its lobby data, "irm=<kind>:<id>". False for anything else.
bool LeagueParseRoomTag(const std::string &tag, LeagueMatchKind &kind, int64_t &matchId);

struct LeagueMapFacts {
	std::string name, type, size;
};
// From the options the host publishes ("Map type: ...", "Map size: ...", "Scenario: ..."), with the lobby's map name as the fallback.
LeagueMapFacts LeagueMapFromOptions(const std::string &optionsText, const std::string &lobbyMap);

struct LeagueReportPlayer {
	std::string steamId, name, civ, result;
	int team = 0;
};

// POST /v1/league/matches/report.
struct LeagueReport {
	std::string key;
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t matchId = 0;
	LeagueMapFacts map;
	std::string contentKey;
	int64_t startedAt = 0;
	uint32_t durationMs = 0;
	std::string endedBy;
	std::vector<LeagueReportPlayer> players;
	std::vector<uint32_t> hashes;
};
// matchId only goes with a ranked or tournament game.
Json LeagueReportJson(const LeagueReport &r);

// The game's own state hash of every tick that is a multiple of kEvery, as every client compared it with the others. A tick
// lower than the last one seen starts again (a new game, or the same one loaded again for a player who rejoined), so the lists
// of two clients stay aligned from their first tick.
class LeagueCheckpoints {
public:
	static constexpr uint32_t kEvery = 1000;
	static constexpr size_t kMax = 4096;
	void Add(uint32_t tick, uint32_t hash);
	// The hashes of ticks 1000, 2000... up to the first one missing: a client that came in late has none.
	std::vector<uint32_t> Hashes() const;
	void Clear();
	uint32_t LastTick() const { return lastTick_; }

private:
	std::vector<uint32_t> hashes_;
	std::vector<bool> have_;
	uint32_t lastTick_ = 0;
	bool any_ = false;
};

// A human player of the game as the end of the game left them.
struct LeagueSeat {
	int index = 0;
	uint64_t steamId = 0;
	std::string name, civ;
	// The game's alliance: the lowest player index among the allies, the same on every computer.
	int team = 0;
	// The game's state: 1 playing, 2 defeated, 3 victorious.
	int state = 1;
	bool local = false;
	// Lost the connection while still playing and never came back.
	bool left = false;
	bool surrendered = false;
};

struct LeagueOutcome {
	std::string endedBy;
	// One per seat, in the same order: "win", "loss" or "left".
	std::vector<std::string> results;
	// The local player closed a game nobody had won yet.
	bool localLeft = false;
};
// Who won: the victorious team, or the only team still standing. A game that closed undecided on this computer, without a desync,
// is this player leaving it.
LeagueOutcome LeagueResolve(const std::vector<LeagueSeat> &seats, bool desync);
// The players of the report: the seats with a SteamID, by player index, with teams numbered from 1 in the order of the game's.
std::vector<LeagueReportPlayer> LeagueReportPlayers(const std::vector<LeagueSeat> &seats, const LeagueOutcome &outcome);

// Reports kept on disk until the server takes them.
struct LeagueQueuedReport {
	std::string steamId;
	Json body;
	int tries = 0;
	int64_t createdAt = 0, nextAt = 0;
};
constexpr size_t kLeagueQueueMax = 50;
constexpr int64_t kLeagueQueueKeepMs = 7LL * 24 * 3600 * 1000;
std::string LeagueQueueEncode(const std::vector<LeagueQueuedReport> &queue);
std::vector<LeagueQueuedReport> LeagueQueueDecode(const std::string &text);
// A report of the same game and account replaces the older one; past the limit the oldest go.
void LeagueQueueAdd(std::vector<LeagueQueuedReport> &queue, const LeagueQueuedReport &item);
// Drops the reports older than a week.
size_t LeagueQueuePrune(std::vector<LeagueQueuedReport> &queue, int64_t nowMs);
// How long to wait after the n-th failure: 30 s, doubling up to 30 min.
int64_t LeagueRetryDelayMs(int tries);
