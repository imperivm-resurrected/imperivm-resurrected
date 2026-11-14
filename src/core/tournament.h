// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"

// The league's official tournaments (imperivm-league, src/tournaments.ts): the shapes of the server's answers, the decisions that only
// depend on them and the geometry of the bracket on screen. Nothing here talks to the network or draws.

enum class TourneyStatus { Unknown, Signup, Checkin, Running, Finished, Cancelled };
TourneyStatus TourneyStatusFromId(const std::string &id);

enum class TourneyMatchStatus { Unknown, Pending, Ready, Live, Disputed, Done, Bye, Walkover };
TourneyMatchStatus TourneyMatchStatusFromId(const std::string &id);
// Over: played, a bye or given without playing.
bool TourneyMatchOver(TourneyMatchStatus s);

enum class TourneyEntryStatus { Unknown, Signed, Waitlist, Playing, Out, Withdrawn, Disqualified, MissedCheckin };
TourneyEntryStatus TourneyEntryStatusFromId(const std::string &id);
// Still in the tournament: signed up, on the waiting list or playing.
bool TourneyEntryActive(TourneyEntryStatus s);

enum class TourneyStage { Unknown, Final, Semifinal, Quarterfinal, RoundOf16, RoundOf32 };
TourneyStage TourneyStageFromId(const std::string &id);
// The stage of a round counted from the final, as the server names it.
TourneyStage TourneyStageOf(int round, int rounds);
// "Final", "Semifinal"...: the English name, the key of its translation. Empty for Unknown.
const char *TourneyStageName(TourneyStage s);

// What a round is played on: a random map of the league's list, a scenario of the game's own, which every player has, or one of the
// organizer's, which the league keeps and each player downloads.
enum class TourneyMapKind { Random, Game, Upload };

// A map of a round. A random one goes by the server's ids ("mountain_passes", "random", "medium"; core/leaguemaps.h finds them in the
// room); a scenario by its file in Scenarios and the SHA-256 of that file.
struct TourneyMap {
	std::string name, type, size;
	TourneyMapKind kind = TourneyMapKind::Random;
	// A scenario: its title, its file ("Torneo_1vs1_Egypt.BFHP"), the file's SHA-256 in lower-case hex, its bytes and its human seats.
	std::string title, file, hash;
	int64_t bytes = 0;
	int players = 0;
	bool Scenario() const { return kind != TourneyMapKind::Random; }
	bool Known() const { return Scenario() ? !file.empty() : !name.empty(); }
};
// {kind: "random", name, type, size} (no kind is random too) or {kind: "scenario", source: "game"|"upload", title, file, hash, bytes,
// players}.
TourneyMap TourneyMapFromJson(const Json *v);
Json TourneyMapJson(const TourneyMap &m);
// The same map: the same type and size, or the same scenario file and contents.
bool TourneySameMap(const TourneyMap &a, const TourneyMap &b);
// A scenario's file as a cup may name it: a plain name in Scenarios ending in .bfhp, without folders.
bool TourneyScenarioFileValid(const std::string &file);
// The file without its extension ("Torneo_1vs1_Egypt"), as the room's selector names it after "Scenarios\".
std::string TourneyScenarioStem(const std::string &file);
// 64 lower-case hex digits.
bool TourneySha256Valid(const std::string &hash);
// The largest scenario the league keeps.
constexpr int64_t kTourneyScenarioMaxBytes = 16LL * 1024 * 1024;

// The presets of a room's options, as the normal room has them (mpui's kPresets).
enum class TourneyPreset { Competitive, Classic, Various };
const char *TourneyPresetId(TourneyPreset p);
// Unknown ids are the competitive one, as the rooms of the official tournaments.
TourneyPreset TourneyPresetFromId(const std::string &id);
bool TourneyPresetValid(const std::string &id);

// The options a tournament room is locked to: a preset, and switches that override it.
struct TourneySettings {
	std::string preset;
	bool hideCivs = false;
	// 1 on, 0 off, -1 when the tournament does not say: the preset's.
	int fog = -1, explored = -1;
	// "locked", "free", or empty: the preset's.
	std::string diplomacy;
	// Seconds behind the game; 0 without spectators; -1 when the tournament does not say.
	int specDelay = -1;
};
TourneySettings TourneySettingsFromJson(const Json *v);
Json TourneySettingsJson(const TourneySettings &s);
// What a tournament room ends up with once its switches are laid over the preset.
struct TourneyRoomRules {
	TourneyPreset preset = TourneyPreset::Competitive;
	bool hideCivs = false, fog = true, explored = false, lockedDiplomacy = true;
};
// A preset's own fog, map and diplomacy: competitive with fog, unexplored and locked; classic the same but free; various without fog,
// explored and free.
TourneyRoomRules TourneyPresetRules(TourneyPreset p);
TourneyRoomRules TourneyRulesOf(const TourneySettings &s);
// The spectators' delay of a tournament room: 0 without spectators, 120 s when it says nothing, otherwise from 60 to 600 s.
int TourneySpecDelay(const TourneySettings &s);
// The spectators' delays a cup can choose: none, 60, 120 or 300 s.
bool TourneySpecDelayValid(int seconds);
// A new cup's: competitive, civilizations hidden, spectators 120 s behind, and the preset's fog, map and diplomacy.
TourneySettings TourneyCupDefaultSettings();
// The settings with every switch written, as the cup's form keeps them.
TourneySettings TourneyExplicitSettings(const TourneySettings &s);

// Who may sign up for a cup: the online games registered by the league, and the highest rank of the season (None: any). A player
// without a rank this season may always sign up.
struct TourneyRequirements {
	int minGames = 0;
	LeagueTier maxTier = LeagueTier::None;
	bool Any() const { return minGames > 0 || maxTier != LeagueTier::None; }
};
constexpr int kTourneyMinGamesMax = 100;
// {minGames, maxTier: null | "recruit"..."legate"}.
TourneyRequirements TourneyRequirementsFromJson(const Json *v);
Json TourneyRequirementsJson(const TourneyRequirements &r);
// The ranks a cup can cap: Recruit to Legate.
bool TourneyMaxTierValid(LeagueTier t);
enum class TourneyUnmet { None, Games, Tier };
// What keeps a player out of a cup, from their online games and their rank this season (None when not ranked).
TourneyUnmet TourneyCheckRequirements(const TourneyRequirements &r, int gamesPlayed, LeagueTier tier);

// The player who made a community cup; nobody for the official ones.
struct TourneyOrganizer {
	uint64_t steamId = 0;
	std::string name;
	bool Present() const { return steamId != 0 || !name.empty(); }
};
TourneyOrganizer TourneyOrganizerFromJson(const Json *v);

// GET /v1/tournaments: one tournament of the list.
struct TourneySummary {
	int64_t id = 0;
	std::string slug, name, description, format, seeding, contentKey, contentName;
	bool official = false;
	// A community cup that only its code opens (GET /v1/tournaments/code/:code); the list never has them.
	bool isPrivate = false;
	TourneyOrganizer organizer;
	int season = -1;
	TourneyStatus status = TourneyStatus::Unknown;
	int maxEntries = 0, minEntries = 0, entries = 0, bestOf = 1, finalBestOf = 1, noshowMinutes = 0;
	TourneySettings settings;
	TourneyRequirements requirements;
	std::vector<TourneyMap> maps;
	int64_t signupOpensAt = 0, checkinOpensAt = 0, startsAt = 0;
};
bool TourneyParseSummary(const Json &j, TourneySummary &out);
bool TourneyParseList(const Json &j, std::vector<TourneySummary> &out);
// The map of a round (1 is the first); the last one repeats. Empty when the tournament gives none.
// The rounds of a full bracket for this many places: 4 -> 2, 8 -> 3, 16 -> 4, 32 -> 5.
int TourneyFullRounds(int maxEntries);
// The map of a round of a bracket of rounds: a list that covers every round counts from the final backwards (with fewer players
// than places the final keeps its map); a shorter one runs from the first round and its last map repeats.
TourneyMap TourneyMapOfRound(const TourneySummary &t, int round, int rounds);

// A side of a match in the bracket.
struct TourneyCard {
	int64_t entryId = 0;
	uint64_t steamId = 0;
	std::string name;
	int seed = 0;
	bool Present() const { return entryId != 0; }
};

struct TourneyEntrant {
	int64_t entryId = 0;
	uint64_t steamId = 0;
	std::string name;
	TourneyEntryStatus status = TourneyEntryStatus::Unknown;
	int seed = 0, placement = 0;
	bool checkedIn = false;
};

struct TourneyMatch {
	int64_t id = 0;
	int round = 0, position = 0, bestOf = 1, scoreA = 0, scoreB = 0;
	TourneyStage stage = TourneyStage::Unknown;
	TourneyCard a, b;
	// The entry of the winner, 0 while nobody won.
	int64_t winner = 0;
	TourneyMatchStatus status = TourneyMatchStatus::Unknown;
	uint64_t lobby = 0, specLobby = 0;
	int64_t readyAt = 0, deadlineAt = 0, startedAt = 0, finishedAt = 0;
	bool Has(int64_t entryId) const { return entryId && (a.entryId == entryId || b.entryId == entryId); }
};

struct TourneyLogLine {
	int64_t at = 0;
	std::string actor, action, detail;
};

// GET /v1/tournaments/:slug.
struct TourneyDetail : TourneySummary {
	int rounds = 0;
	std::vector<TourneyEntrant> entrants;
	std::vector<TourneyMatch> matches;
	std::vector<TourneyLogLine> log;
};
bool TourneyParseDetail(const Json &j, TourneyDetail &out);
const TourneyMatch *TourneyMatchAt(const TourneyDetail &d, int round, int position);
const TourneyEntrant *TourneyEntrantOf(const TourneyDetail &d, int64_t entryId);
const TourneyEntrant *TourneyEntrantBySteamId(const TourneyDetail &d, uint64_t steamId);
// The matches a player played or plays, by round.
std::vector<const TourneyMatch *> TourneyPathOf(const TourneyDetail &d, int64_t entryId);
// The series to play (byes left out) and how many of them are over.
void TourneyProgress(const TourneyDetail &d, int &done, int &total);
// The final's winner; 0 while it is not decided.
int64_t TourneyChampion(const TourneyDetail &d);
// The entrants that finished in `place` (1, 2, 3...), by seed.
std::vector<const TourneyEntrant *> TourneyPlaced(const TourneyDetail &d, int place);

// A side of the player's next match.
struct TourneyNextSide {
	uint64_t steamId = 0;
	std::string name, side;
	int seed = 0;
	bool inRoom = false;
};

// The player's next match, from GET /v1/me/tournaments and from the presence answer.
struct TourneyNext {
	bool present = false;
	int64_t tournamentId = 0, match = 0;
	std::string slug, name, contentKey, contentName, lobbyKey;
	bool official = false, isPrivate = false;
	TourneyOrganizer organizer;
	int round = 0, rounds = 0, bestOf = 1, game = 1, scoreMe = 0, scoreThem = 0;
	TourneyStage stage = TourneyStage::Unknown;
	TourneyMatchStatus status = TourneyMatchStatus::Unknown;
	int64_t readyAt = 0, deadlineAt = 0;
	TourneyNextSide me, opponent;
	bool hasOpponent = false, canClaimNoShow = false;
	TourneyMap map;
	TourneySettings settings;
	uint64_t lobby = 0, specLobby = 0, roomClaimBy = 0;
};
bool TourneyParseNext(const Json &j, TourneyNext &out);

// The check-in the player has to make.
struct TourneyCheckin {
	bool present = false, checkedIn = false;
	int64_t tournamentId = 0, opensAt = 0, closesAt = 0;
	std::string slug, name;
};

// One of the player's tournaments: still on, or over in the last day.
struct TourneyMyEntry {
	int64_t tournamentId = 0, startsAt = 0;
	std::string slug, name;
	TourneyStatus tournamentStatus = TourneyStatus::Unknown;
	TourneyEntryStatus status = TourneyEntryStatus::Unknown;
	bool checkedIn = false;
	int seed = 0, placement = 0;
};

// Whether the player may make a community cup, and the one they organise now (sign-up, check-in or being played).
struct TourneyOrganize {
	bool known = false, allowed = false;
	int gamesPlayed = 0, gamesNeeded = 0;
	int64_t activeId = 0;
	std::string activeSlug;
	// The online games still to play before making a cup.
	int GamesMissing() const { return gamesNeeded > gamesPlayed ? gamesNeeded - gamesPlayed : 0; }
};

// A community cup the player organises: still on, or over in the last day.
struct TourneyOrganized {
	int64_t id = 0, startsAt = 0, checkinOpensAt = 0;
	std::string slug, name, inviteCode;
	TourneyStatus status = TourneyStatus::Unknown;
	bool isPrivate = false;
	int entries = 0, checkedIn = 0, maxEntries = 0, minEntries = 0, openDisputes = 0;
};

// GET /v1/me/tournaments.
struct TourneyMine {
	TourneyNext next;
	TourneyCheckin checkin;
	std::vector<TourneyMyEntry> entries;
	TourneyOrganize organize;
	std::vector<TourneyOrganized> organized;
};
bool TourneyParseMine(const Json &j, TourneyMine &out);
const TourneyMyEntry *TourneyMyEntryOf(const TourneyMine &m, int64_t tournamentId);
const TourneyOrganized *TourneyOrganizedOf(const TourneyMine &m, int64_t tournamentId);
// A tournament not over yet that the player is still in, or a cup of theirs not over yet: it is what makes the mod ask every 30 s.
bool TourneyHasActiveEntry(const TourneyMine &m);
// How often to ask for the player's tournaments: 30 s with one of their own on, 60 s while the Tournaments tab is open, otherwise never.
int TourneyPollEveryMs(bool activeEntry, bool tabOpen);

// POST /v1/tournaments/matches/:id/room/claim.
struct TourneyClaim {
	enum Kind { None, Create, Join, Wait } kind = None;
	uint64_t lobby = 0, by = 0;
	std::string lobbyKey, contentKey;
	TourneyMap map;
	TourneySettings settings;
};
bool TourneyParseClaim(const Json &j, TourneyClaim &out);

// The tag of a tournament room in its lobby data ("tournament:<match>") and the match of a tag; 0 for any other room.
std::string TourneyRoomTag(int64_t match);
int64_t TourneyMatchOfTag(const std::string &tag);

// What the player is told in any menu once.
struct TourneyNotice {
	enum Kind { MatchReady, CheckinOpen, Finished } kind = MatchReady;
	int64_t tournamentId = 0, match = 0;
	std::string slug, name, opponent;
	TourneyStage stage = TourneyStage::Unknown;
	int round = 0;
	int64_t deadlineAt = 0;
};
// What was told already, so each thing is told once.
struct TourneyTold {
	std::set<int64_t> ready, checkin, finished;
	bool primed = false;
};
// The notices `mine` brings that were not told yet. The first answer of a session only primes what is over, so a tournament that
// ended before the game started is not announced.
std::vector<TourneyNotice> TourneyNewNotices(const TourneyMine &mine, TourneyTold &told);

// "09:12", or "1:02:03" past the hour; 0 for a past time.
std::string TourneyClock(int64_t ms);

// GET /v1/tournaments/:id/disputes: the results a player of a cup disputed, for its organizer.
struct TourneyDispute {
	int64_t matchId = 0, createdAt = 0;
	std::string text;
	// Who opened it: a SteamID, or a name when the server gives one.
	uint64_t openedBy = 0;
	std::string openedByName;
};
bool TourneyParseDisputes(const Json &j, std::vector<TourneyDispute> &out);

// The body of a refusal, {error, message}, with what each error adds.
struct TourneyRefusal {
	std::string code, message, contentName;
	// already_organizing: the cup the player organises now.
	std::string slug;
	int64_t until = 0;
	// not_eligible: the online games played and needed; not_enough: the players checked in and needed.
	int gamesPlayed = 0, gamesNeeded = 0, checkedIn = 0, needed = 0;
	// rate_limited: whole seconds to wait.
	int retryAfter = 0;
	// requirement: "games" or "tier", and what the player has and the cup asks (a number of games, or a rank's id).
	std::string requirement, have, need;
	// map_missing: the SHA-256 of the uploaded scenario the league does not have.
	std::string hash;
};
TourneyRefusal TourneyParseRefusal(const Json &j);

// POST /v1/tournaments: the cup made, with its code when it is private.
struct TourneyCreated {
	int64_t id = 0;
	std::string slug, inviteCode;
};
bool TourneyParseCreated(const Json &j, TourneyCreated &out);

// The invitation code of a private cup: six letters and digits without 0, O, 1 or I.
constexpr int kTourneyCodeLength = 6;
// The code as the player typed or pasted it: upper case, without spaces, dashes or dots.
std::string TourneyTidyCode(const std::string &typed);
bool TourneyCodeValid(const std::string &code);

// What a player chooses to make a community cup (POST /v1/tournaments) or to change it (PATCH /v1/tournaments/:id).
struct TourneyCupForm {
	std::string name, description;
	int maxEntries = 8, minEntries = 4, bestOf = 1, finalBestOf = 3;
	bool randomSeeding = false;
	// One per round from the first, the last one repeated.
	std::vector<TourneyMap> maps;
	int64_t startsAt = 0;
	int checkinMinutes = 30, noshowMinutes = 15;
	bool isPrivate = false, play = true;
	// The organizer's content profile; empty when it is the base game.
	std::string contentKey, contentName;
	TourneySettings settings = TourneyCupDefaultSettings();
	TourneyRequirements requirements;
};
constexpr int kTourneyCupNameMin = 3, kTourneyCupNameMax = 40, kTourneyCupDescriptionMax = 200, kTourneyCupMapsMax = 5;
constexpr int64_t kTourneyCupLeadMinMs = 20 * 60000LL, kTourneyCupLeadMaxMs = 14 * 86400000LL;
enum class TourneyCupProblem {
	NameShort, NameLong, DescriptionLong, Places, Minimum, Series, Settings, NoMaps, TooManyMaps, BadMap, MapTooLarge, StartSoon, StartLate, Checkin,
	Noshow, Requirements,
};
// What the server would refuse in the form, in the order of the form; none when it can be sent.
std::vector<TourneyCupProblem> TourneyCheckCup(const TourneyCupForm &f, int64_t nowMs);
// The request's body: the name and description trimmed, the description and the content left out when empty. For a change (`edit`) the
// description and the content are always written, empty or null, so that they can be taken away.
Json TourneyCupJson(const TourneyCupForm &f, bool edit = false);
// The body of a change once other players signed up: only the description.
Json TourneyCupDescriptionJson(const std::string &description);
// The maps of a form for `rounds` rounds: one each, the missing ones as the last.
void TourneyFitMaps(std::vector<TourneyMap> &maps, int rounds);
// A cup's form as its page shows it: the check-in from its opening, "I play too" from the organizer's own entry.
TourneyCupForm TourneyCupFormOf(const TourneyDetail &d);
// Someone other than the organizer signed up: from then on only the description can change.
bool TourneyCupLocked(const TourneyDetail &d);

// The Summary's figures: the rounds, the series (one per pair of players) and the games at most, and the minutes it lasts: 40 a game and
// 10 between rounds, each round as long as its longest series.
struct TourneyEstimate {
	int rounds = 0, series = 0, maxGames = 0, minutes = 0;
};
TourneyEstimate TourneyEstimateFor(int places, int bestOf, int finalBestOf);

// The cup's calendar, in this computer's time: a day is the number of days since 1 January 1970 and a time is milliseconds from then,
// both counted in local time.
int64_t TourneyDayOf(int64_t localMs);
// 0 is Monday.
int TourneyWeekdayOf(int64_t day);
void TourneyCivilDate(int64_t day, int &year, int &month, int &dayOfMonth);
// The weeks the calendar shows: from the Monday of `firstDay`'s week, as many as reach `lastDay`.
struct TourneyCalendar {
	int64_t first = 0;
	int rows = 0;
};
TourneyCalendar TourneyCalendarOf(int64_t firstDay, int64_t lastDay);
constexpr int64_t kTourneyQuarterMs = 15 * 60000LL;
// The start the calendar keeps for `wantMs`: on a quarter of an hour, no earlier than `minMs` and no later than `maxMs`.
int64_t TourneySnapStart(int64_t wantMs, int64_t minMs, int64_t maxMs);
// A day with at least one start between the two.
bool TourneyDayOpen(int64_t day, int64_t minMs, int64_t maxMs);
// The same time of day on another day, kept between the two.
int64_t TourneyOnDay(int64_t localMs, int64_t day, int64_t minMs, int64_t maxMs);
// The time of day moved to `hour` and `minute`, kept between the two.
int64_t TourneyAtTime(int64_t localMs, int hour, int minute, int64_t minMs, int64_t maxMs);
// The minimum of players a cup of `places` starts with unless the organizer says otherwise: 4, or the places when fewer.
int TourneyCupDefaultMinimum(int places);
// The rounds of a single elimination bracket for `places` players.
int TourneyRoundsFor(int places);
// The characters of a UTF-8 text, as the server counts them.
int TourneyTextLength(const std::string &text);
std::string TourneyTrim(const std::string &text);

// The bracket on screen: each round a column, each match a box centred between the two that feed it.
struct TourneyBracketMetrics {
	int boxW = 236, boxH = 58, colPitch = 300, rowPitch = 76, top = 34;
};
struct TourneySegment {
	int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
int TourneyBoxX(const TourneyBracketMetrics &m, int round);
int TourneyBoxY(const TourneyBracketMetrics &m, int round, int position);
// The middle of a box's height, where its lines start and end.
int TourneyBoxMidY(const TourneyBracketMetrics &m, int round, int position);
// The line from a match to the one its winner goes to: out of its right side, along to the junction and into the next box. None from
// the final.
std::vector<TourneySegment> TourneyConnector(const TourneyBracketMetrics &m, int round, int position, int rounds);
int TourneyBracketWidth(const TourneyBracketMetrics &m, int rounds);
int TourneyBracketHeight(const TourneyBracketMetrics &m, int rounds);
