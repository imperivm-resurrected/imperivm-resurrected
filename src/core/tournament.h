// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
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

// PendingPartner: a team whose partner has not accepted the invitation yet; it takes no place.
enum class TourneyEntryStatus { Unknown, Signed, Waitlist, Playing, Out, Withdrawn, Disqualified, MissedCheckin, PendingPartner };
TourneyEntryStatus TourneyEntryStatusFromId(const std::string &id);
// Still in the tournament: signed up, on the waiting list or playing.
bool TourneyEntryActive(TourneyEntryStatus s);

// How many play on each side of a match: one against one, or teams of two, a captain and the partner they invited.
enum class TourneyMode { Solo, Teams };
// "1v1" or "2v2"; anything else is one against one, as the league's first tournaments.
TourneyMode TourneyModeFromId(const std::string &id);
const char *TourneyModeId(TourneyMode m);
// The players of a side: 1, or 2 in a cup of teams.
int TourneySidePlayers(TourneyMode m);
// The minutes a game takes in the estimates: 40 one against one and 45 two against two, whose games are longer (4-torneos.md §2).
int TourneyGameMinutes(TourneyMode m);

// A player of a side of a cup: the one of a 1 vs 1 entry, or one of a team's two. In 2 vs 2 the entry's `name` is the team's and its
// `steamId` the captain's, and the players are its members.
struct TourneyMember {
	uint64_t steamId = 0;
	std::string name;
	// Whether the league saw them in the match's room lately: 1 or 0, -1 when it does not say.
	int inRoom = -1;
};
// [{steamId, name, inRoom?}]; a member without a SteamID is left out.
std::vector<TourneyMember> TourneyMembersFromJson(const Json *v);
const TourneyMember *TourneyMemberOf(const std::vector<TourneyMember> &members, uint64_t steamId);
// The team's name was not chosen: it is the league's own, its players' names joined ("Varo y Tulia"), so they need not be repeated under it.
bool TourneyNameIsMembers(const std::string &name, const std::vector<TourneyMember> &members);

enum class TourneyStage { Unknown, Final, Semifinal, Quarterfinal, RoundOf16, RoundOf32 };
TourneyStage TourneyStageFromId(const std::string &id);
// The stage of a round counted from the final, as the server names it.
TourneyStage TourneyStageOf(int round, int rounds);
// "Final", "Semifinal"...: the English name, the key of its translation. Empty for Unknown.
const char *TourneyStageName(TourneyStage s);

// The part of a tournament a match is played in: a single elimination only has the winners' bracket; a double elimination adds the
// losers' bracket, the grand final and its reset; a Swiss tournament has rounds instead. A match's round counts within its part.
enum class TourneyBracket { Winners, Losers, Final, Reset, Swiss };
// "winners", "losers", "final", "reset" or "swiss"; anything else is the winners' bracket.
TourneyBracket TourneyBracketFromId(const std::string &id);

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

// How a tournament is played: single or double elimination, or Swiss rounds against rivals with the same points.
enum class TourneyFormat { Single, Double, Swiss };
// "single", "double" or "swiss"; unknown ids are single elimination, as the league's first tournaments.
TourneyFormat TourneyFormatFromId(const std::string &id);
const char *TourneyFormatId(TourneyFormat f);
// Live, in one evening; or by rounds, each with a deadline some days away.
enum class TourneyPace { Live, Rounds };
// "live" or "rounds"; unknown ids are live.
TourneyPace TourneyPaceFromId(const std::string &id);
const char *TourneyPaceId(TourneyPace p);
// The days a round can last with the by-rounds pace: 1, 2, 3 or 7.
bool TourneyRoundDaysValid(int days);
// The Swiss rounds a cup can choose instead of the automatic count.
constexpr int kTourneySwissRoundsMin = 3, kTourneySwissRoundsMax = 6;
// The rounds of a Swiss: the ones chosen (3 to 6), otherwise ceil(log2(players)).
int TourneySwissRounds(int chosen, int players);

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
	// The Swiss rounds chosen; 0 for the automatic count.
	int swissRounds = 0;
	TourneyPace pace = TourneyPace::Live;
	// The days each round lasts with the by-rounds pace.
	int roundDays = 0;
	// The maps the loser of a game of a series chooses the next one from (3 to 5); none when every game repeats the round's map.
	std::vector<TourneyMap> seriesPool;
	// With the by-rounds pace: the round being played (0 before the first) and when its deadline ends.
	int currentRound = 0;
	int64_t roundEndsAt = 0;
	// One against one or teams of two: in a cup of teams its places, entries and sides are teams.
	TourneyMode mode = TourneyMode::Solo;
	bool Teams() const { return mode == TourneyMode::Teams; }
};
bool TourneyParseSummary(const Json &j, TourneySummary &out);
bool TourneyParseList(const Json &j, std::vector<TourneySummary> &out);
// The format of `format`'s id.
TourneyFormat TourneyFormatOf(const TourneySummary &t);
// The rounds the tournament plays one after the other, for "Round 2 of 4": a Swiss its rounds, single elimination its bracket's; 0 for
// a double elimination, whose two brackets count their rounds apart.
int TourneyRoundCount(const TourneySummary &t);
// The rounds of a full bracket for this many places: 4 -> 2, 8 -> 3, 16 -> 4, 32 -> 5; never more than 16.
int TourneyFullRounds(int maxEntries);
// The map of a round of a bracket of rounds: a list that covers every round counts from the final backwards (with fewer players
// than places the final keeps its map); a shorter one runs from the first round and its last map repeats.
TourneyMap TourneyMapOfRound(const TourneySummary &t, int round, int rounds);

// A side of a match in the bracket. In a cup of teams `name` is the team's, `steamId` its captain's and `members` its two players. A
// team without a name of its own is named after its players in the player's language ("Varo y Tulia"; the league sends "Varo & Tulia"),
// in every view of a cup.
struct TourneyCard {
	int64_t entryId = 0;
	uint64_t steamId = 0;
	std::string name;
	int seed = 0;
	std::vector<TourneyMember> members;
	bool Present() const { return entryId != 0; }
};

struct TourneyEntrant {
	int64_t entryId = 0;
	uint64_t steamId = 0;
	std::string name;
	TourneyEntryStatus status = TourneyEntryStatus::Unknown;
	int seed = 0, placement = 0;
	bool checkedIn = false;
	std::vector<TourneyMember> members;
	// A team: the name its captain gave (empty: the league names it after its players), and the partner, invited or in the team; a
	// team waiting for its partner lists only its captain among its members.
	std::string teamName;
	TourneyMember partner;
};
// The entry's players: its members, or the entry's own player when the league lists none.
std::vector<TourneyMember> TourneyPlayersOf(const TourneyEntrant &e);
std::vector<TourneyMember> TourneyPlayersOf(const TourneyCard &c);

// A game of a series: its number from 1, its map (not Known() while it is being chosen) and its winner, "a", "b" or empty.
struct TourneyGame {
	int no = 0;
	TourneyMap map;
	std::string winner;
};
// The map of a series' next game, which the loser of the last one chooses: who ("a" or "b"), among which maps and until when. Not
// present while there is nothing to choose.
struct TourneyPick {
	bool present = false;
	std::string by;
	std::vector<TourneyMap> options;
	int64_t until = 0;
};
// [{no, map, winner: "a"|"b"|null}], by number.
std::vector<TourneyGame> TourneyGamesFromJson(const Json *v);
// {by: "a"|"b", options: [map...], until} or null.
TourneyPick TourneyPickFromJson(const Json *v);
// The game `no` of a series; null when the league lists none.
const TourneyGame *TourneyGameOf(const std::vector<TourneyGame> &games, int no);
// The games of a series of `bestOf` won by each side, and whether one of them has won it.
void TourneySeriesScore(const std::vector<TourneyGame> &games, int &a, int &b);
bool TourneySeriesOver(const std::vector<TourneyGame> &games, int bestOf);

struct TourneyMatch {
	int64_t id = 0;
	int round = 0, position = 0, bestOf = 1, scoreA = 0, scoreB = 0;
	TourneyStage stage = TourneyStage::Unknown;
	TourneyBracket bracket = TourneyBracket::Winners;
	TourneyCard a, b;
	// The entry of the winner, 0 while nobody won.
	int64_t winner = 0;
	TourneyMatchStatus status = TourneyMatchStatus::Unknown;
	uint64_t lobby = 0, specLobby = 0;
	int64_t readyAt = 0, deadlineAt = 0, startedAt = 0, finishedAt = 0;
	std::vector<TourneyGame> games;
	TourneyPick pick;
	// With the by-rounds pace: from when not coming to the room counts as a no-show (the last day of the round); 0 otherwise.
	int64_t noshowFrom = 0;
	bool Has(int64_t entryId) const { return entryId && (a.entryId == entryId || b.entryId == entryId); }
};

struct TourneyLogLine {
	int64_t at = 0;
	std::string actor, action, detail;
};

// A line of a Swiss tournament's standings: a point for each win and each bye, and the Buchholz, the sum of the points of the opponents
// met. `place` is final once the tournament is over.
struct TourneyStanding {
	int64_t entryId = 0;
	uint64_t steamId = 0;
	std::string name;
	int points = 0, wins = 0, losses = 0, buchholz = 0, byes = 0, place = 0;
	// A team's players, from the standing or else from its entry.
	std::vector<TourneyMember> members;
};

// GET /v1/tournaments/:slug.
struct TourneyDetail : TourneySummary {
	// The rounds of the bracket: of the winners' bracket in a double elimination, every round planned in a Swiss tournament.
	int rounds = 0;
	// A double elimination's losers' bracket: twice the winners' rounds less two.
	int losersRounds = 0;
	// The Swiss rounds, the pace, its days a round, the round open now and its deadline are the summary's.
	std::vector<TourneyEntrant> entrants;
	std::vector<TourneyMatch> matches;
	std::vector<TourneyLogLine> log;
	// A Swiss tournament's standings, by place.
	std::vector<TourneyStanding> standings;
};
bool TourneyParseDetail(const Json &j, TourneyDetail &out);
// The match at a round and a position (both counted within its part of the tournament).
const TourneyMatch *TourneyMatchAt(const TourneyDetail &d, int round, int position, TourneyBracket bracket = TourneyBracket::Winners);
const TourneyEntrant *TourneyEntrantOf(const TourneyDetail &d, int64_t entryId);
// The entry of a player: theirs, or the team they are a member of.
const TourneyEntrant *TourneyEntrantBySteamId(const TourneyDetail &d, uint64_t steamId);
// The matches a player played or plays, in the order they come: by round, the winners' bracket before the losers' and the grand final
// before its reset.
std::vector<const TourneyMatch *> TourneyPathOf(const TourneyDetail &d, int64_t entryId);
// The series to play (byes left out) and how many of them are over. A Swiss tournament counts the rounds not paired yet too.
void TourneyProgress(const TourneyDetail &d, int &done, int &total);
// The champion: the entry placed first, else the winner of the match that decides it; 0 while it is not decided.
int64_t TourneyChampion(const TourneyDetail &d);
// The entrants that finished in `place` (1, 2, 3...), by seed; in a Swiss tournament, from its standings when the entries have no place.
std::vector<const TourneyEntrant *> TourneyPlaced(const TourneyDetail &d, int place);

// The formats of the tournament's `format`: anything that is not one of these is single elimination.
bool TourneyIsDouble(const TourneySummary &t);
bool TourneyIsSwiss(const TourneySummary &t);
// The rounds have a deadline of some days each instead of being played in one evening.
bool TourneyByRounds(const TourneyDetail &d);
// The rounds a Swiss tournament plays: as it was made, or as many as its players need, and never fewer than the ones paired.
int TourneySwissTotalRounds(const TourneyDetail &d);
// The round being played: the server's with the pace by rounds; otherwise, in a Swiss tournament, the latest one paired. 0 before any.
int TourneyCurrentRound(const TourneyDetail &d);
// The matches of a round of one part of the tournament, by position.
std::vector<const TourneyMatch *> TourneyRoundMatches(const TourneyDetail &d, int round, TourneyBracket bracket);
// A player's points in a Swiss tournament before a round: one for each win and each bye in the rounds before it.
int TourneySwissPointsBefore(const TourneyDetail &d, int64_t entryId, int round);
const TourneyStanding *TourneyStandingOf(const TourneyDetail &d, int64_t entryId);
// The match whose winner is the champion: the final of a single elimination; the grand final of a double elimination, or its reset once
// there is one. None in a Swiss tournament.
const TourneyMatch *TourneyDecider(const TourneyDetail &d);
// The player from the losers' bracket won the grand final and the reset that decides the champion is still to come.
bool TourneyResetDue(const TourneyDetail &d);
// When the last match finished; 0 before any.
int64_t TourneyFinishedAt(const TourneyDetail &d);
// The last place a double elimination's placement shares: 1, 2, 3 and 4 alone, then 5-6, 7-8, 9-12, 13-16, 17-24 and 25-32.
int TourneyDoublePlaceEnd(int placement);

// A side of the player's next match. In a cup of teams the opponent is a team (its name, its captain's SteamID and its `members`), and a side
// is in the room once both its players are.
struct TourneyNextSide {
	uint64_t steamId = 0;
	std::string name, side;
	int seed = 0;
	bool inRoom = false;
	std::vector<TourneyMember> members;
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
	// The part of the tournament the match is in; `round` counts within it.
	TourneyBracket bracket = TourneyBracket::Winners;
	TourneyMatchStatus status = TourneyMatchStatus::Unknown;
	int64_t readyAt = 0, deadlineAt = 0;
	TourneyNextSide me, opponent;
	bool hasOpponent = false, canClaimNoShow = false;
	// The map of the game to play; none while it is being chosen (TourneyGameMap).
	TourneyMap map;
	TourneySettings settings;
	uint64_t lobby = 0, specLobby = 0, roomClaimBy = 0;
	std::vector<TourneyGame> games;
	TourneyPick pick;
	TourneyFormat format = TourneyFormat::Single;
	// With the by-rounds pace the deadline is the end of the round, and the no-show only counts from `noshowFrom`.
	TourneyPace pace = TourneyPace::Live;
	int64_t noshowFrom = 0;
	// A cup of teams: the player's own team (its name, its captain in `steamId` and its two players); `opponent` is the other team.
	TourneyMode mode = TourneyMode::Solo;
	TourneyNextSide team;
	// The map of the next game is this player's to choose.
	bool MyPick() const { return pick.present && !me.side.empty() && pick.by == me.side; }
	bool Teams() const { return mode == TourneyMode::Teams; }
};
bool TourneyParseNext(const Json &j, TourneyNext &out);
// The players of the match the room lets in: the player, their team's and the opponent's.
std::vector<uint64_t> TourneyMatchPlayers(const TourneyNext &n, uint64_t me);
// The map of the game to play now: the one chosen or drawn for it, otherwise the round's. Not Known() while it is being chosen.
TourneyMap TourneyGameMap(const TourneyNext &n);

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
	// A team: its entry, its name ({team: {name}}, its players' when it has none), the partner and its players, when the league says them.
	int64_t entryId = 0;
	std::string teamName;
	TourneyMember partner;
	std::vector<TourneyMember> members;
	bool Pending() const { return status == TourneyEntryStatus::PendingPartner; }
};
// The partner of the player's team in a cup: the one the league names, else the member who is not the player.
TourneyMember TourneyPartnerOf(const TourneyMyEntry &e, uint64_t me);

// An invitation to play a cup of teams in someone's team, which the player accepts or declines from the game.
struct TourneyInvite {
	int64_t tournamentId = 0, entryId = 0, expiresAt = 0;
	std::string slug, name, teamName;
	TourneyMember from;
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
	// The invitations to other players' teams waiting for an answer, soonest to expire first.
	std::vector<TourneyInvite> invites;
};
bool TourneyParseMine(const Json &j, TourneyMine &out);
// The player's entry in a cup: the one still in it or over with a place first, then a team waiting for its partner, then any.
const TourneyMyEntry *TourneyMyEntryOf(const TourneyMine &m, int64_t tournamentId);
const TourneyOrganized *TourneyOrganizedOf(const TourneyMine &m, int64_t tournamentId);
// The invitation to a team of this cup; null when there is none.
const TourneyInvite *TourneyInviteOf(const TourneyMine &m, int64_t tournamentId);
// A tournament not over yet that the player is still in or waits for a partner in, an invitation to answer, or a cup of theirs not over
// yet: it is what makes the mod ask every 30 s.
bool TourneyHasActiveEntry(const TourneyMine &m);
// How often to ask for the player's tournaments: 30 s with one of their own on, 60 s while the Tournaments tab is open, otherwise every
// 2 minutes, which is how long an invitation to a team can take to show in another menu.
constexpr int kTourneyIdlePollMs = 120000;
int TourneyPollEveryMs(bool activeEntry, bool tabOpen);
// Sooner while a series waits for its next map, which has a minute to be chosen: every 5 s while the choice is open, and in the two
// minutes after a game of a series (`msSinceGame`, negative when no game ended), when it opens; 0 otherwise.
int TourneySeriesPollMs(const TourneyNext &n, int64_t msSinceGame);

// POST /v1/tournaments/matches/:id/room/claim.
struct TourneyClaim {
	enum Kind { None, Create, Join, Wait } kind = None;
	uint64_t lobby = 0, by = 0;
	std::string lobbyKey, contentKey;
	TourneyMap map;
	TourneySettings settings;
	// Wait while the map of a series' next game is being chosen: the room opens once it is.
	TourneyPick pick;
	// A cup of teams: the SteamIDs of each side, side A's first, which the room's teams 1 and 2 are set from.
	std::vector<std::vector<uint64_t>> teams;
};
bool TourneyParseClaim(const Json &j, TourneyClaim &out);
// The room's team of a player, from a claim's sides: 1 for side A, 2 for side B, 0 when not in them.
int TourneyTeamOf(const std::vector<std::vector<uint64_t>> &teams, uint64_t steamId);

// The tag of a tournament room in its lobby data ("tournament:<match>") and the match of a tag; 0 for any other room.
std::string TourneyRoomTag(int64_t match);
int64_t TourneyMatchOfTag(const std::string &tag);

// What the player is told in any menu once.
struct TourneyNotice {
	// PickOpen: the map of the next game of the player's series is theirs to choose (`game`, until `deadlineAt`). Invite: `from` invites the
	// player to their team `teamName` (the entry `entry`), until `deadlineAt`.
	enum Kind { MatchReady, CheckinOpen, Finished, PickOpen, Invite } kind = MatchReady;
	int64_t tournamentId = 0, match = 0, entry = 0;
	std::string slug, name, opponent, teamName;
	TourneyMember from;
	TourneyStage stage = TourneyStage::Unknown;
	TourneyBracket bracket = TourneyBracket::Winners;
	int round = 0, game = 0;
	int64_t deadlineAt = 0;
};
// What was told already, so each thing is told once.
struct TourneyTold {
	// The invitations by entry.
	std::set<int64_t> ready, checkin, finished, invites;
	// The picks by match and game.
	std::set<std::pair<int64_t, int>> picks;
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
	// requirement: "games" or "tier", and what the player has and the cup asks (a number of games, or a rank's id); `who` is "partner" when
	// it is the partner of a team's sign-up who misses it.
	std::string requirement, have, need, who;
	// map_missing: the SHA-256 of the uploaded scenario the league does not have.
	std::string hash;
	// too_early with the by-rounds pace: from when the no-show counts.
	int64_t noshowFrom = 0;
};
TourneyRefusal TourneyParseRefusal(const Json &j);

// POST /v1/tournaments: the cup made, with its code when it is private.
struct TourneyCreated {
	int64_t id = 0;
	std::string slug, inviteCode;
};
bool TourneyParseCreated(const Json &j, TourneyCreated &out);

// A team's name, which is optional: the league names a team after its players when it has none.
constexpr int kTourneyTeamNameMax = 24;
// POST /v1/tournaments/:id/entries of a cup of teams: {partner, teamName?, code?}, the name trimmed and left out when empty, as the code.
Json TourneyTeamEntryJson(uint64_t partner, const std::string &teamName, const std::string &code);

// GET /v1/league/maps: a random map type with the sizes that let two players in, and `sizes4`, the ones that let four in.
struct TourneyMapType {
	std::string id;
	std::vector<std::string> sizes, sizes4;
	bool hasSizes4 = false;
};
bool TourneyParseMapTypes(const Json &j, std::vector<TourneyMapType> &out);
// The sizes a random map type is played on: one against one, the league's list (core/leaguemaps.h); two against two, the league's sizes4
// when it sent them, otherwise every size of that list but the small one, which no template opens to more than two players.
std::vector<std::string> TourneySizesFor(const std::vector<TourneyMapType> &types, const std::string &type, TourneyMode mode);
// A round's map is playable in the mode: a random map in one of those sizes, a scenario with a human seat for each player of a match.
bool TourneyMapFits(const TourneyMap &m, TourneyMode mode, const std::vector<TourneyMapType> &types);
// A map turned into one that fits the mode: a random map to the nearest size it has for it, anything else to a normal Mediterranean.
TourneyMap TourneyMapForMode(const TourneyMap &m, TourneyMode mode, const std::vector<TourneyMapType> &types);

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
	TourneyFormat format = TourneyFormat::Single;
	// The Swiss rounds, 3 to 6; 0 for the automatic count.
	int swissRounds = 0;
	TourneyPace pace = TourneyPace::Live;
	int roundDays = 2;
	// The maps the loser of a game chooses the next one from, 3 to 5, or none; only sent when the cup has series.
	std::vector<TourneyMap> seriesPool;
	// Teams of two: the places count teams, and every map needs room for four. The organizer signs their own team up from the cup's page,
	// with their partner, so "I play too" is not sent.
	TourneyMode mode = TourneyMode::Solo;
};
constexpr int kTourneyCupNameMin = 3, kTourneyCupNameMax = 40, kTourneyCupDescriptionMax = 200, kTourneyCupMapsMax = 5;
constexpr int kTourneyPoolMin = 3, kTourneyPoolMax = 5;
constexpr int64_t kTourneyCupLeadMinMs = 20 * 60000LL, kTourneyCupLeadMaxMs = 14 * 86400000LL;
// MapSeats: a map without room for the four players of a match of teams.
enum class TourneyCupProblem {
	NameShort, NameLong, DescriptionLong, Places, Minimum, Format, Series, Settings, NoMaps, TooManyMaps, BadMap, MapTooLarge, MapSeats, PoolCount, PoolMap,
	StartSoon, StartLate, Pace, Checkin, Noshow, Requirements,
};
// Some game of the cup is a series (best of 3, or 5 in a final): the rounds', or the final's outside a Swiss, which has no final.
bool TourneyCupHasSeries(const TourneyCupForm &f);
// The rounds a cup's form gives a map to: a Swiss's rounds, the rounds of an elimination's bracket (its winners' in a double one).
int TourneyCupMapRounds(const TourneyCupForm &f);
// What the server would refuse in the form, in the order of the form; none when it can be sent. `types` gives the sizes of a cup of teams.
std::vector<TourneyCupProblem> TourneyCheckCup(const TourneyCupForm &f, int64_t nowMs, const std::vector<TourneyMapType> &types = {});
// The form turned to its mode: its rounds' maps to ones that fit (TourneyMapForMode), and the series' maps that don't fit taken out.
void TourneyFitCupToMode(TourneyCupForm &f, const std::vector<TourneyMapType> &types);
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

// The Summary's figures: the rounds played one after the other, the series (one per pair of players or teams) and the games at most, and
// the minutes it lasts: `gameMinutes` a game (TourneyGameMinutes) and 10 between rounds, each round as long as its longest series. With the
// by-rounds pace, the days at most: each round its days.
struct TourneyEstimate {
	int rounds = 0, series = 0, maxGames = 0, minutes = 0, days = 0;
};
// Single elimination.
TourneyEstimate TourneyEstimateFor(int places, int bestOf, int finalBestOf, int gameMinutes = 40);
// A Swiss is its rounds, each a series; a double elimination waits for its losers' bracket, which starts after the first round and
// plays twice as many, then the grand final and its reset.
TourneyEstimate TourneyEstimateFor(TourneyFormat format, int places, int bestOf, int finalBestOf, int swissRounds, int gameMinutes = 40);
TourneyEstimate TourneyEstimateOf(const TourneyCupForm &f);
// When a cup played in one evening should end: its start and the estimate for those signed up (its places before anyone is). 0 for a cup
// by rounds or without a start.
int64_t TourneyEstimatedEnd(const TourneyDetail &d);
// Past this a live cup is long for one evening: the form says so for a double elimination.
constexpr int kTourneyLiveWarnMinutes = 300;
bool TourneyCupLongLive(const TourneyCupForm &f);

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
// The text in `maxBytes` at most, without the end of a character cut off.
std::string TourneyCutText(const std::string &text, size_t maxBytes);

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

// A double elimination on screen: the winners' bracket on top and the losers' under it, each under its title and laid out as the single
// bracket, and to the right of both the grand final, halfway between the two finals, its reset and the champion.
// The losers' rounds of a winners' bracket of `winnersRounds`: 2 with 4 places, 4 with 8, 6 with 16, 8 with 32.
int TourneyLosersRounds(int winnersRounds);
// The matches of a losers' round: its odd rounds pair the players of the round before, its even ones meet those dropping from the
// winners' bracket, so two rounds in a row have the same matches (8 places: 2, 2, 1, 1).
int TourneyLosersMatches(int winnersRounds, int round);
struct TourneyDoubleLayout {
	int winnersRounds = 0, losersRounds = 0;
	// What each bracket's TourneyBoxY is added to; its title goes in the `titleH` above.
	int winnersY = 0, losersY = 0;
	// The grand final's box, its reset's (with `reset`) and where the champion starts.
	int finalX = 0, finalY = 0, resetX = 0, champX = 0;
	bool reset = false;
	// The size of all the boxes, without the champion.
	int width = 0, height = 0;
};
// `losersRounds` 0 takes the ones of a full bracket; `reset` keeps a column for the reset; `gap` goes between the two brackets.
TourneyDoubleLayout TourneyDoubleLayoutOf(const TourneyBracketMetrics &m, int winnersRounds, int losersRounds, bool reset, int titleH, int gap);
int TourneyWinnersBoxY(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position);
int TourneyLosersBoxY(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position);
// The line from a losers' match to the next one: straight along into an even round, where it meets a player from the winners' bracket;
// joined with its neighbour's into an odd one. None from the losers' final, which goes to the grand final.
std::vector<TourneySegment> TourneyLosersConnector(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l, int round, int position);
// The lines into the grand final, in this order: out of the winners' final, out of the losers' final, the upright that joins them, into the
// grand final; and with `reset`, a fifth from the grand final into the reset.
std::vector<TourneySegment> TourneyGrandFinalConnector(const TourneyBracketMetrics &m, const TourneyDoubleLayout &l);

// A Swiss tournament on screen, measured from the left of its tab. The strip over it: the standings and each round, as wide as `width`
// lets each of `cells`, up to `maxW`.
int TourneyStripCellWidth(int width, int cells, int gap, int maxW);
// The standings' columns: the place and the player from the left; from the right the byes, the Buchholz (a little wider), the wins and
// losses and the points; and between them each round's result, when the player's name keeps room enough.
struct TourneyStandingsColumns {
	int name = 0, nameW = 0, rounds = 0, roundsW = 0, points = 0, record = 0, buchholz = 0, buchholzW = 0, byes = 0, figureW = 0;
	bool showRounds = false;
};
TourneyStandingsColumns TourneyStandingsColumnsFor(int width, int rounds);
// Where the square of each round's result goes in its column.
int TourneyRoundSquareX(const TourneyStandingsColumns &c, int round);
// A pairing's row: the table's number, the two players either side of the score in the middle, and what is happening in it on the right.
struct TourneyPairingRow {
	int tableW = 44, left = 0, scoreX = 0, scoreW = 96, right = 0, stateX = 0, stateW = 0;
};
TourneyPairingRow TourneyPairingRowFor(int width);

int TourneyBracketWidth(const TourneyBracketMetrics &m, int rounds);
int TourneyBracketHeight(const TourneyBracketMetrics &m, int rounds);
