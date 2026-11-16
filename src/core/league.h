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
// The divisions counted from the lowest, as the server does: 0 is Recruit III, 17 is Legate I, which has no top (100 Glory each).
constexpr int kLeagueDivisionCount = 18;
int LeagueDivisionIndex(int glory);
// The rank of a division index: its tier (Recruit to Legate) and its division (3 is III).
LeagueTier LeagueIndexTier(int index);
int LeagueIndexDivision(int index);

enum class LeagueBadgeKind { None, Rank, Season, Trophy };

// The metal of a tournament's phalera: gold for the champion, silver for the finalist, bronze for the semifinalists; Season for the
// champion of a season's final.
enum class LeagueMetal { None, Gold, Silver, Bronze, Season };
// The server's id ("gold", "silver", "bronze", "season"); None for anything else.
LeagueMetal LeagueMetalFromId(const std::string &id);

// The one badge a player shows to the right of their name.
struct LeagueBadge {
	LeagueBadgeKind kind = LeagueBadgeKind::None;
	std::string id;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	// From the player's own list of badges: the season it was won in and a trophy's title. Not part of the comparison.
	int season = 0;
	std::string title;
	// A trophy's phalera; gold when the server does not say.
	LeagueMetal metal = LeagueMetal::None;
	// The emblem of the test season (its tier is "founder"), which everyone who played it keeps instead of a rank's.
	bool founder = false;
	bool Shown() const { return kind != LeagueBadgeKind::None; }
	bool operator==(const LeagueBadge &o) const
	{
		return kind == o.kind && id == o.id && tier == o.tier && division == o.division && metal == o.metal && founder == o.founder;
	}
};
// {kind, id, tier?, division?}, or null. A rank badge without a known tier is not shown.
LeagueBadge LeagueBadgeFromJson(const Json *v);

// The avatar frame (or the title) a closed season gave: the season and, when the server says it, the rank it was won with. A player
// without one has season -1.
struct LeagueFrame {
	int season = -1;
	LeagueTier tier = LeagueTier::None;
	// Won in the test season with no rank given: the Founder's colours.
	bool founder = false;
	std::string seasonName;
	bool Shown() const { return season >= 0; }
};
// A season number, {season, tier?, seasonName?} or null.
LeagueFrame LeagueFrameFromJson(const Json *v);

// One player of GET /v1/players/lookup.
struct LeagueLookupEntry {
	std::string patronTier;
	int patronPlace = 0;
	LeagueBadge badge;
	LeagueFrame frame;
};
// {"<steamId>": {patron: {tier, place}|null, badge: {...}|null}}. False when it is not an object.
bool LeagueParsePlayersLookup(const Json &j, std::map<uint64_t, LeagueLookupEntry> &out);

// The best of a player's season: the most Glory, the rank it gave and, for the elite, the best place.
struct LeaguePeak {
	bool present = false;
	int glory = 0, place = 0;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
};
// {glory, tier, division, place?}, or a place alone.
LeaguePeak LeaguePeakFromJson(const Json *v);

enum class LeaguePlacementMark { Win, Loss, Void };

struct LeagueRanked {
	bool present = false;
	std::string queue;
	LeagueTier tier = LeagueTier::None;
	int division = 0;
	int season = 0, glory = 0, placementLeft = 0, shield = 0, wins = 0, losses = 0, position = 0;
	bool elite = false;
	LeaguePeak peak;
	// +3: three wins in a row, -2: two losses.
	int streak = 0;
	// The placement games played, in order.
	std::vector<LeaguePlacementMark> placement;
};

// What a closed season left the player: the vexillum of the highest rank reached (the Founder's in the test season) from 10 games,
// and with 25 the avatar frame and the title too (`frame`).
struct LeagueReward {
	int season = 0;
	std::string seasonName, queue;
	LeagueTier tier = LeagueTier::None;
	bool founder = false;
	int place = 0, games = 0;
	bool frame = false;
};

// The last reward the player has not seen: the end of season ceremony.
struct LeagueCeremony {
	bool present = false;
	LeagueReward reward;
	int wins = 0, losses = 0;
	LeagueTier finalTier = LeagueTier::None;
	int finalDivision = 0;
};

// A lock out of the ranked queue: until when (server time, ms since 1970) and why ("declined", "abandoned", "desync"; empty when the
// server does not say).
struct LeagueBan {
	int64_t until = 0;
	std::string kind;
	// A lock the league's team put ("staff"): why, as the team wrote it.
	std::string reason;
};

// A finished cup the player was drawn into: official ones give the podium its phalerae, community ones nothing but the place.
struct LeagueCup {
	int64_t id = 0, finishedAt = 0;
	std::string name, mode = "1v1", format;
	bool official = false;
	// -1 when the cup belongs to no season.
	int season = -1;
	// 1 for the champion; 0 for a player knocked out without playing.
	int placement = 0, entries = 0;
};

// GET /v1/league/me.
struct LeagueMe {
	std::string steamId, name;
	// The standing in the duel and in the 2v2 queue (`ranked2v2`, absent from a server without it), and the queues the season plays.
	LeagueRanked ranked, ranked2v2;
	std::vector<std::string> queues;
	int64_t queueBanUntil = 0;
	// `ban` of the server, or what queueBanUntil says when it sends none.
	LeagueBan ban;
	LeagueBadge badge;
	// The choice as the server keeps it: "rank" (the live rank, the default), "season", "trophy" or "none", and its id.
	std::string badgeKind, badgeId;
	std::vector<LeagueBadge> badges;
	// Every season's reward, newest first, and the one still to be shown.
	std::vector<LeagueReward> rewards;
	LeagueCeremony ceremony;
	// The seasons whose frame and title the player wears; -1 for none.
	int equippedFrame = -1, equippedTitle = -1;
	// The last finished cups, newest first; `cupsKnown` is false for a server before the list.
	bool cupsKnown = false;
	std::vector<LeagueCup> cups;
};
bool LeagueParseMe(const Json &j, LeagueMe &out);

// POST /v1/link/start. `steamUrl` is the server's page that links with one Steam sign-in; servers before it send none.
struct LeagueLinkCode {
	std::string code, pollToken, url, steamUrl;
	int64_t expiresAt = 0;
};
bool LeagueParseLinkStart(const Json &j, LeagueLinkCode &out);
// The Steam sign-in page of a link code when it is the league server's own (`base`, as LeagueBaseUrl gives it) /v1/link/steam
// page; empty for anything else, and then the player types the code on the website.
std::string LeagueSteamLinkUrl(const std::string &steamUrl, const std::string &base);
// Whether opening the Ranked or Tournaments tab offers to link the account: to a player who is not linked nor linking, while
// Steam runs, once a session, and never again once they said "Not now".
bool LeagueOfferLinkNow(bool linked, bool linking, bool steamReady, bool offeredThisSession, bool declined);

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
	// The game's time when a player of a 2v2 left it and had not come back when it ended; -1 (null) otherwise. The league decides from it
	// whether they abandoned (out 5 minutes or more) or voided it (out before minute 3).
	int64_t leftAtMs = -1;
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
	// The game's time when the other side left and did not come back; negative when nobody did (null in the report).
	int64_t dropAtMs = -1;
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
	// 0 for the AI, and for a human whose account could not be told.
	uint64_t steamId = 0;
	bool human = false;
	std::string name, civ;
	// The game's alliance: the lowest player index among the allies, the same on every computer.
	int team = 0;
	// The game's state: 1 playing, 2 defeated, 3 victorious.
	int state = 1;
	bool local = false;
	// Lost the connection while still playing and never came back.
	bool left = false;
	bool surrendered = false;
	// The game's time when they left, for a 2v2's report; -1 when not known.
	int64_t leftAtMs = -1;
};

struct LeagueOutcome {
	std::string endedBy;
	// One per seat, in the same order: "win", "loss" or "left".
	std::vector<std::string> results;
	// The local player closed a game nobody had won yet.
	bool localLeft = false;
};
// How a ranked 1v1 ends when the other player dropped (2-clasificatoria-sistema.md §8.3): it waits for them, and once the wait is over
// the one who stayed claims the win ("opponent_left", which the league gives to whoever reports it). Closing the game before that is
// leaving it too, as any undecided game closed ("disconnect").
struct LeagueDropRules {
	bool ranked = false;
	// The wait for the one who left is over (or the win was claimed).
	bool claimable = false;
	// A ranked 2v2 (§8.3): the AI plays for whoever drops, and only those gone for good (`left`: they quit, or 5 minutes passed) are
	// out. A team whose humans all left loses: closing the game then is the other team's win ("opponent_left").
	bool team = false;
};
// Who won: the victorious team, or the only team still standing. A game that closed undecided on this computer, without a desync,
// is this player leaving it. A desync ends it as "desync", whoever stands.
LeagueOutcome LeagueResolve(const std::vector<LeagueSeat> &seats, bool desync, const LeagueDropRules &rules = LeagueDropRules());
// The players of the report: the seats with a SteamID, by player index, with teams numbered from 1 in the order of the game's. The other
// players take their leftAtMs (a 2v2's who were out when it ended), this computer's only when it left.
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
