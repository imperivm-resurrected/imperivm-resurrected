// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net/leagueclient.h"
#include "addr/net.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/league.h"
#include "core/leaguestats.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "core/names.h"
#include "core/tournament.h"
#include "game/civs.h"
#include "game/gamestats.h"
#include "hooks/tweaks.h"
#include "net/leagueclient_internal.h"
#include "net/leagueclient_tourney.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

// CNetTransport: the current tick, and after AdvanceTick the state hash of the tick before it, which the game has just compared with
// the one every other player sent for that tick (a difference is a desync and ends the game).
const size_t kTransportTick = 0x54, kTransportPrevHash = 0x80, kTransportLocal = 0x4c, kTransportGate = 0x50;
// The gate's bitmask of the players whose ticks it still waits for: a player who drops leaves it, one who rejoins comes back.
const size_t kGateConnected = 0x108;
const DWORD kPollMs = 1000, kSendCheckMs = 5000;
// From the game being decided to its report: the states and the statistics of that moment land first.
const DWORD kDecidedSettleMs = 1500;

void ReportIfDecided();

typedef int(__cdecl *IsDemo_t)();

struct Seat {
	int index = 0;
	std::string name, civ;
	uint64_t steamId = 0;
	bool human = false, local = false, connected = true, left = false, surrendered = false;
	int team = 0, state = 0;
	// The game's time when they left.
	uint32_t leftAt = 0;
	// They quit on purpose (a 2v2), or they left for good (they quit, or did not come back in 5 minutes).
	bool quit = false, gone = false;
};

struct Match {
	bool active = false;
	LeagueMatchInfo info;
	int64_t localStartMs = 0;
	std::map<int, Seat> seats;
	std::set<int> surrendered;
	uint32_t clockMs = 0, decidedMs = 0;
	bool decided = false, desync = false;
	// When the game was decided (GetTickCount), and whether its report went then: the winner may stay in the game for a long time.
	DWORD decidedTick = 0;
	bool reported = false;
	DWORD lastPoll = 0;
	// A ranked match (the room's tag), a ranked 2v2 (`team`), and the drop rules of a ranked or a cup game (2-clasificatoria-sistema.md
	// §8.3, 4-torneos.md §6): the game goes on, the AI plays for whoever drops and they have 5 minutes of the game's clock to come back;
	// whoever does not is gone. Known from the room's tag, the same on every computer.
	bool ranked = false, team = false, dropRules = false;
	// Noted in the list of league and cup games (the statistics tell them by it).
	bool tagged = false;
	std::set<int> quitters;
	int64_t rankedId = 0;
	// The game's record of this player as last read, for the result screen: the game is gone when it is shown.
	GameRecordFacts facts;
	bool factsRead = false;
};

struct Outbox {
	bool loaded = false, sending = false, dirty = false;
	DWORD lastCheck = 0;
	std::vector<LeagueQueuedReport> items;
};

std::mutex g_mx;
Match g_match;
Outbox g_out;
// Read from the game's hooks without the lock.
std::atomic<bool> g_rankedNow{false}, g_teamNow{false}, g_dropNow{false};

// A name without the "(AI)" the game puts after a player the AI plays for, so the 2v2's seats keep their names.
std::string WithoutAiSuffix(const std::string &name)
{
	const size_t open = name.find_last_of('(');
	if (open == std::string::npos || open == 0 || name.back() != ')' || name.size() - open > 6) return name;
	std::string clean = name.substr(0, open);
	while (!clean.empty() && clean.back() == ' ') clean.pop_back();
	return clean.empty() ? name : clean;
}

std::mutex g_syncMx;
LeagueCheckpoints g_checkpoints;
// The players' values at ticks of a ranked or tournament game, read where the checkpoints are (LeagueSyncAfterAdvance).
LeagueTickStats g_tickStats;

std::string TagsPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\') + 1) + "imperivm-resurrected\\league\\games.txt";
}

// One line for each league or cup game this computer started: "<start, seconds since 1970> <league|cup>", the newest last; the
// last 500 are kept.
std::vector<MatchTag> ReadTags()
{
	std::vector<MatchTag> out;
	FILE *f = nullptr;
	if (fopen_s(&f, TagsPath().c_str(), "rb") != 0 || !f) return out;
	char line[96];
	while (fgets(line, sizeof(line), f)) {
		long long sec = 0;
		char kind[16] = {};
		if (sscanf_s(line, "%lld %15s", &sec, kind, static_cast<unsigned>(sizeof(kind))) != 2) continue;
		const std::string k = kind;
		if (k == "league" || k == "cup") out.push_back({static_cast<int64_t>(sec), k == "league" ? kMatchLeague : kMatchCup});
	}
	fclose(f);
	return out;
}

// Under g_mx: the first time the game is known to be a league or a cup one, it is noted.
void NoteTaggedGame(Match &m, LeagueMatchKind kind)
{
	if (m.tagged || (kind != LeagueMatchKind::Ranked && kind != LeagueMatchKind::Tournament)) return;
	m.tagged = true;
	std::vector<MatchTag> tags = ReadTags();
	tags.push_back({m.localStartMs / 1000, kind == LeagueMatchKind::Ranked ? kMatchLeague : kMatchCup});
	if (tags.size() > 500) tags.erase(tags.begin(), tags.end() - 500);
	const std::string path = TagsPath();
	CreateDirectoryA(path.substr(0, path.find_last_of('\\')).c_str(), nullptr);
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) {
		LogW("league", "could not note the league game in %s", path.c_str());
		return;
	}
	for (const MatchTag &t : tags) fprintf(f, "%lld %s\n", static_cast<long long>(t.startSec), t.league == kMatchLeague ? "league" : "cup");
	fclose(f);
}

std::string OutboxPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\') + 1) + "imperivm-resurrected\\league\\reports.json";
}

bool ReadAll(const std::string &path, std::string &out)
{
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
	char buf[8192];
	for (size_t n = fread(buf, 1, sizeof(buf), f); n; n = fread(buf, 1, sizeof(buf), f)) out.append(buf, n);
	fclose(f);
	return true;
}

// Under g_mx, from the sending thread.
void SaveOutbox()
{
	const std::string path = OutboxPath(), dir = path.substr(0, path.find_last_of('\\'));
	CreateDirectoryA(dir.substr(0, dir.find_last_of('\\')).c_str(), nullptr);
	CreateDirectoryA(dir.c_str(), nullptr);
	if (g_out.items.empty()) {
		DeleteFileA(path.c_str());
		g_out.dirty = false;
		return;
	}
	const std::string text = LeagueQueueEncode(g_out.items), tmp = path + ".tmp";
	FILE *f = nullptr;
	if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) {
		LogW("league", "could not write the reports waiting to be sent");
		return;
	}
	const bool written = fwrite(text.data(), 1, text.size(), f) == text.size();
	fclose(f);
	if (written && MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) g_out.dirty = false;
	else {
		DeleteFileA(tmp.c_str());
		LogW("league", "could not write the reports waiting to be sent");
	}
}

// Under g_mx.
void LoadOutbox()
{
	if (g_out.loaded) return;
	g_out.loaded = true;
	std::string text;
	if (!ReadAll(OutboxPath(), text)) return;
	g_out.items = LeagueQueueDecode(text);
	const size_t old = LeagueQueuePrune(g_out.items, NowMs());
	if (old) g_out.dirty = true;
	if (!g_out.items.empty() || old) LogI("league", "%u game reports waiting to be sent (%u too old dropped)", static_cast<unsigned>(g_out.items.size()), static_cast<unsigned>(old));
}

bool Retriable(int status)
{
	return status == 0 || status == 401 || status == 408 || status == 429 || status >= 500;
}

void SendDue()
{
	for (;;) {
		std::string token, steamId;
		const bool linked = AccountFor(token, steamId);
		LeagueQueuedReport item;
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(g_mx);
			if (g_out.dirty) SaveOutbox();
			const int64_t now = NowMs();
			for (const LeagueQueuedReport &q : g_out.items) {
				if (linked && q.steamId == steamId && q.nextAt <= now) {
					item = q;
					found = true;
					break;
				}
			}
			if (!found) {
				g_out.sending = false;
				return;
			}
		}
		const std::string key = item.body.GetString("key");
		const HttpReply r = Http("POST", "/v1/league/matches/report", item.body.Dump(), true);
		Json answer;
		const std::string status = r.Ok() && Json::Parse(r.body, answer) ? answer.GetString("status", "recorded") : std::string();
		if (r.Ok()) {
			LogI("league", "game report sent: %s", status.empty() ? "accepted" : status.c_str());
			RankedReportAnswered(item.body, r.body);
			if (item.body.GetString("kind") == "tournament") LeagueTourneyGameReported();
		}
		else if (Retriable(r.status)) LogW("league", "game report not sent (%s); trying again later", r.Describe().c_str());
		else LogE("league", "the server refused a game report (%s); it is dropped", r.Describe().c_str());
		std::lock_guard<std::mutex> lock(g_mx);
		auto it = std::find_if(g_out.items.begin(), g_out.items.end(),
			[&](const LeagueQueuedReport &q) { return q.steamId == item.steamId && q.body.GetString("key") == key; });
		if (it != g_out.items.end()) {
			if (r.Ok() || !Retriable(r.status)) g_out.items.erase(it);
			else {
				it->tries++;
				it->nextAt = NowMs() + LeagueRetryDelayMs(it->tries);
			}
		}
		SaveOutbox();
		if (!r.Ok() && Retriable(r.status)) {
			g_out.sending = false;
			return;
		}
	}
}

// Under g_mx.
void KickSender()
{
	if (g_out.sending) return;
	g_out.sending = true;
	SafeThread(SendDue).detach();
}

std::string CivOf(int index)
{
	const int race = CivsPlayerCiv(index + 1);
	if (race < 0) return std::string();
	if (const Civ *c = CivByIndex(race)) {
		if (!c->id.empty()) return c->id;
	}
	const char *name = GameCivName(race);
	return name && *name ? std::string(name) : std::to_string(race);
}

uint64_t SteamIdOf(const Match &m, const Seat &s)
{
	if (s.local) return SteamOwnId();
	for (const auto &member : m.info.members) {
		if (SamePlayerName(member.second, s.name)) return member.first;
	}
	for (const auto &member : m.info.members) {
		if (PlayerNameMatches(member.second, s.name)) return member.first;
	}
	return 0;
}

// The players whose ticks the lockstep still gets; -1 outside a network game.
int ConnectedMask(int &localIndex)
{
	__try {
		const char *t = *reinterpret_cast<char *const *>(addr::kTransport);
		if (!t) return -1;
		const char *gate = *reinterpret_cast<char *const *>(t + kTransportGate);
		if (!gate) return -1;
		localIndex = *reinterpret_cast<const int *>(t + kTransportLocal);
		return *reinterpret_cast<const uint8_t *>(gate + kGateConnected);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return -1;
	}
}


// On the game thread, under g_mx.
void Poll(Match &m)
{
	std::vector<StatsLivePlayer> live;
	uint32_t clock = 0;
	if (!GameStatsLive(live, clock)) return;
	int localIndex = -1;
	const int mask = ConnectedMask(localIndex);
	m.clockMs = clock;
	for (const StatsLivePlayer &p : live) {
		if (!p.playable || p.info.index >= 8) continue;
		const bool fresh = m.seats.find(p.info.index) == m.seats.end();
		Seat &s = m.seats[p.info.index];
		if (fresh) {
			s.index = p.info.index;
			s.human = p.info.human;
			s.local = p.info.local;
			s.civ = CivOf(s.index);
			s.state = p.state;
		}
		const std::string name = m.dropRules ? WithoutAiSuffix(p.info.name) : p.info.name;
		if (s.name != name || (s.human && !s.steamId)) {
			s.name = name;
			// In a 2v2 an account once told is kept, whatever the name of a player the AI plays for turns into.
			const uint64_t id = s.human ? SteamIdOf(m, s) : 0;
			if (id || !m.dropRules) s.steamId = id;
		}
		s.team = p.info.team;
		const int before = s.state;
		// The states only move forward: a reload for a rejoin or the game being torn down never undo a defeat or a victory. A drop may
		// defeat the player in the same second, so leaving is judged on the state before.
		if (p.state >= 1 && p.state <= 3 && s.state != 3 && !(s.state == 2 && p.state == 1)) s.state = p.state;
		if (m.surrendered.count(s.index)) s.surrendered = true;
		if (m.dropRules && m.quitters.count(s.index) && !s.quit) {
			s.quit = s.gone = true;
			if (!s.left) s.leftAt = clock;
			LogI("league", "player %d left the game on purpose; they are gone", s.index);
		}
		if (s.local && m.ranked) {
			GameRecordFacts facts;
			if (GameStatsRecordOf(s.index, facts)) {
				m.facts = facts;
				m.factsRead = true;
			}
		}
		if (!s.human || s.local || mask < 0 || s.index >= 8 || s.index == localIndex) continue;
		const bool connected = (mask & (1 << s.index)) != 0;
		if (s.connected && !connected && before == 1 && !m.decided) {
			s.left = true;
			if (!s.quit) s.leftAt = clock;
			LogI("league", "player %d left the game while playing", s.index);
		}
		else if (!s.connected && connected && s.left && !s.gone) {
			s.left = false;
			LogI("league", "player %d is back in the game", s.index);
		}
		s.connected = connected;
		// The five minutes to come back are over: they left the game for good, and the AI keeps their army.
		if (m.dropRules && s.left && !s.gone && !m.decided && LeagueTeamDropOver(s.leftAt, clock)) {
			s.gone = true;
			LogI("league", "player %d did not come back in %d minutes; they left the game", s.index, static_cast<int>(kLeagueTeamDropWaitMs / 60000));
		}
	}
	if (!m.decided) {
		for (const auto &kv : m.seats) {
			if (kv.second.state != 3) continue;
			m.decided = true;
			m.decidedMs = clock;
			m.decidedTick = GetTickCount();
			LogI("league", "the game has a winner at %u s", clock / 1000);
			break;
		}
	}
	// Under the drop rules, every other side's players gone for good decide it: the one who stayed won, and its report goes now, whether
	// they play on against the AI or leave.
	if (m.dropRules && !m.decided && !m.desync) {
		int ownTeam = -1;
		std::set<int> sides, standing;
		for (const auto &kv : m.seats) {
			const Seat &s = kv.second;
			if (!s.human) continue;
			sides.insert(s.team);
			if (s.local) ownTeam = s.team;
			if (!s.gone && !(s.local && s.state == 2)) standing.insert(s.team);
		}
		if (ownTeam >= 0 && sides.size() >= 2 && standing.size() == 1 && standing.count(ownTeam)) {
			m.decided = true;
			m.decidedMs = clock;
			m.decidedTick = GetTickCount();
			LogI("league", "the other side left the game for good at %u s: the game is this side's", clock / 1000);
		}
	}
}

// The game's time when the other side left and did not come back; -1 when nobody did. In a 2v2, only who left for good counts.
int64_t DropAtOf(const Match &m)
{
	int64_t at = -1;
	for (const auto &kv : m.seats) {
		const Seat &s = kv.second;
		if (s.local || !s.human || !s.left || (m.dropRules && !s.gone)) continue;
		if (at < 0 || s.leftAt < at) at = s.leftAt;
	}
	return at;
}

// What the game left, under g_mx. Empty when there is nothing to report.
bool BuildReport(const Match &m, LeagueReport &r, std::string &why)
{
	std::vector<LeagueSeat> seats;
	for (const auto &kv : m.seats) {
		const Seat &s = kv.second;
		LeagueSeat seat;
		seat.index = s.index;
		seat.steamId = s.human ? s.steamId : 0;
		seat.human = s.human;
		seat.name = s.name;
		seat.civ = s.civ;
		seat.team = s.team;
		seat.state = s.state;
		seat.local = s.local;
		seat.left = s.left;
		seat.surrendered = s.surrendered;
		// Under the drop rules a player who dropped is still in it while their five minutes last, but out when it ended: when they left
		// goes with them, and the league tells an abandon from it. The one closing the game leaves it now.
		if (m.dropRules) {
			seat.left = s.gone;
			// As signed numbers: mixed with the unsigned clocks, the -1 of a player who did not leave became 4294967295, and the league
			// refused the report.
			seat.leftAtMs = s.gone || s.left || s.quit ? static_cast<int64_t>(s.leftAt) : s.local ? static_cast<int64_t>(m.clockMs) : int64_t{-1};
		}
		seats.push_back(seat);
	}
	// The ticket of a ranked match knows both accounts: a report must go, or the server keeps the queue locked for hours.
	LeagueMatchTicket ticket;
	if (m.ranked && RankedTicketOf(m.rankedId, ticket)) LeagueSeatsFromTicket(seats, ticket, SteamOwnId());
	int unknown = 0;
	for (const LeagueSeat &s : seats) {
		if (s.human && !s.steamId) unknown++;
	}
	if (unknown) LogW("league", "%d human players of the game could not be matched to a Steam account", unknown);
	LeagueDropRules rules;
	rules.ranked = m.ranked;
	rules.team = m.dropRules;
	// A cup match against a staging test bot was played by the game's AI in its seat: that seat goes as the bot, or the league could not
	// match the game to its match.
	{
		LeagueMatchKind kind = LeagueMatchKind::Normal;
		int64_t matchId = 0;
		if (LeagueParseRoomTag(m.info.roomTag, kind, matchId) && kind == LeagueMatchKind::Tournament) {
			const auto mine = LeagueTourneyMine();
			const TourneyNext &next = mine->mine.next;
			if (next.present && next.match == matchId && TourneyIsTestBot(next.opponent.steamId)) {
				// The other side's test bots, one for each seat the AI played: a 1v1's bot, or a team's two.
				std::vector<uint64_t> bots;
				for (const TourneyMember &b : next.opponent.members) {
					if (b.steamId) bots.push_back(b.steamId);
				}
				if (bots.empty()) bots.push_back(next.opponent.steamId);
				size_t k = 0;
				for (LeagueSeat &s : seats) {
					if (s.human || k >= bots.size()) continue;
					s.human = true;
					s.steamId = bots[k++];
				}
				LogI("league", "the game's AI played %u test bot(s) of tournament match %lld", static_cast<unsigned>(k), static_cast<long long>(matchId));
			}
		}
	}
	const LeagueOutcome outcome = LeagueResolve(seats, m.desync, rules);
	r.players = LeagueReportPlayers(seats, outcome);
	// A 2v2's teams are the ticket's sides, whichever player has the lower index in the game.
	if (m.team && ticket.id) {
		const std::vector<LeagueMatchTeam> sides = LeagueTicketTeams(ticket);
		const bool known = std::all_of(r.players.begin(), r.players.end(), [&](const LeagueReportPlayer &p) { return LeagueSideOf(sides, p.steamId) != 0; });
		if (known) {
			for (LeagueReportPlayer &p : r.players) p.team = LeagueSideOf(sides, p.steamId);
		}
	}
	if (r.players.size() < 2) {
		why = "fewer than two players with a Steam account";
		return false;
	}
	r.startedAt = m.info.startedAtMs ? m.info.startedAtMs : m.localStartMs;
	if (!m.info.startedAtMs) LogW("league", "the room never said when the host started the game; the report uses this computer's start");
	r.key = LeagueMatchKey(m.info.lobby, r.startedAt);
	LeagueParseRoomTag(m.info.roomTag, r.kind, r.matchId);
	r.map = m.info.map;
	r.contentKey = m.info.contentKey;
	r.durationMs = m.decided ? m.decidedMs : m.clockMs;
	r.endedBy = outcome.endedBy;
	r.dropAtMs = DropAtOf(m);
	{
		std::lock_guard<std::mutex> lock(g_syncMx);
		r.hashes = g_checkpoints.Hashes();
		if (r.kind != LeagueMatchKind::Normal && !g_tickStats.Empty()) {
			std::vector<LeagueStatsSeat> statSeats;
			for (const LeagueSeat &s : seats) {
				LeagueStatsSeat x;
				x.index = s.index;
				x.steamId = s.steamId ? std::to_string(s.steamId) : std::string();
				x.name = s.name;
				x.team = s.team;
				statSeats.push_back(x);
			}
			r.stats = LeagueStatsJson(g_tickStats, statSeats);
		}
	}
	if (outcome.localLeft) LogI("league", "this player left the game before it was decided");
	return true;
}

}

namespace leagueclient_impl {

void MatchTick()
{
	bool ranked = false, over = false;
	int64_t rankedId = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		ranked = g_match.active && g_match.ranked;
		rankedId = g_match.rankedId;
		over = g_match.decided || g_match.desync;
	}
	PauseTick(ranked, rankedId, false, over);
	TeamTick();
	ReportIfDecided();
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_match.active && GetTickCount() - g_match.lastPoll >= kPollMs) {
		g_match.lastPoll = GetTickCount();
		Poll(g_match);
	}
	if (GetTickCount() - g_out.lastCheck < kSendCheckMs) return;
	g_out.lastCheck = GetTickCount();
	LoadOutbox();
	if (g_out.sending || (g_out.items.empty() && !g_out.dirty)) return;
	const int64_t now = NowMs();
	const std::string own = std::to_string(SteamOwnId());
	bool due = g_out.dirty;
	for (const LeagueQueuedReport &q : g_out.items) due = due || (q.steamId == own && q.nextAt <= now);
	if (due && LeagueLinked()) KickSender();
}

}

std::vector<MatchTag> LeagueGameTags()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return ReadTags();
}

void LeagueMatchPlaying(const LeagueMatchInfo &info)
{
	if (!info.lobby || SpectatorWatching() || SpectatorViewing() || reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) return;
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t matchId = 0;
	const bool ranked = LeagueParseRoomTag(info.roomTag, kind, matchId) && kind == LeagueMatchKind::Ranked;
	if (ranked) RankedGameStarted(matchId);
	// By the room's data only: whether the AI plays for a player who drops has to be the same on every computer.
	const bool team = ranked && info.team;
	const bool dropRules = ranked || kind == LeagueMatchKind::Tournament;
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || g_match.info.lobby != info.lobby) {
		g_match = Match();
		g_match.active = true;
		g_match.info = info;
		g_match.localStartMs = NowMs();
		g_match.ranked = ranked;
		g_match.team = team;
		g_match.dropRules = dropRules;
		g_match.rankedId = ranked ? matchId : 0;
		g_rankedNow = ranked;
		g_teamNow = team;
		g_dropNow = dropRules;
		LogI("league", "recording the online game%s (%s)", team ? ", a ranked 2v2" : "", LeagueLinked() ? "it will be reported" : "the account is not linked, so it won't be reported");
		NoteTaggedGame(g_match, kind);
		return;
	}
	Match &m = g_match;
	if (!m.info.startedAtMs) m.info.startedAtMs = info.startedAtMs;
	if (m.info.map.name.empty()) m.info.map = info.map;
	if (m.info.contentKey.empty()) m.info.contentKey = info.contentKey;
	if (m.info.roomTag.empty()) m.info.roomTag = info.roomTag;
	if (ranked && !m.ranked) {
		m.ranked = true;
		m.rankedId = matchId;
		g_rankedNow = true;
	}
	if (team && !m.team) {
		m.team = true;
		g_teamNow = true;
	}
	if (dropRules && !m.dropRules) {
		m.dropRules = true;
		g_dropNow = true;
	}
	NoteTaggedGame(m, kind);
	for (const auto &member : info.members) {
		const bool known = std::any_of(m.info.members.begin(), m.info.members.end(), [&](const std::pair<uint64_t, std::string> &k) { return k.first == member.first; });
		if (!known) m.info.members.push_back(member);
	}
}

namespace {

// Not under g_mx: the report goes to the outbox and the sender. The ranked queue learns its game was played.
void QueueReport(const LeagueReport &report, bool ranked, int64_t rankedId, const GameRecordFacts &facts, bool factsRead)
{
	unsigned wins = 0;
	for (const LeagueReportPlayer &p : report.players) wins += p.result == "win" ? 1 : 0;
	LogI("league", "online game over after %u s (%s, %s), %u players, %u winners, %u checkpoints", report.durationMs / 1000, report.endedBy.c_str(),
		LeagueMatchKindId(report.kind), static_cast<unsigned>(report.players.size()), wins, static_cast<unsigned>(report.hashes.size()));
	std::string token, steamId;
	if (!AccountFor(token, steamId)) {
		LogI("league", "the account is not linked: the game is not reported");
		if (ranked) RankedGameEnded(rankedId);
		return;
	}
	if (report.kind == LeagueMatchKind::Ranked) RankedGameReported(report, steamId, facts, factsRead);
	LeagueQueuedReport item;
	item.steamId = steamId;
	item.body = LeagueReportJson(report);
	item.createdAt = NowMs();
	std::lock_guard<std::mutex> lock(g_mx);
	LoadOutbox();
	LeagueQueueAdd(g_out.items, item);
	g_out.dirty = true;
	KickSender();
}

// A game is reported once it is decided, a moment later (for what is still settling to land): who won does not change after, and
// the result is not held up by whoever stays in the game to look at it.
void ReportIfDecided()
{
	LeagueReport report;
	bool ready = false, ranked = false, factsRead = false;
	int64_t rankedId = 0;
	GameRecordFacts facts;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		Match &m = g_match;
		if (!m.active || !m.decided || m.reported || m.desync || !m.decidedTick || GetTickCount() - m.decidedTick < kDecidedSettleMs) return;
		Poll(m);
		std::string why;
		ready = BuildReport(m, report, why);
		// Not buildable yet (an account still unknown): the game's end tries again.
		if (!ready) return;
		m.reported = true;
		ranked = m.ranked;
		rankedId = m.rankedId;
		facts = m.facts;
		factsRead = m.factsRead;
	}
	LogI("league", "the game is decided: its report goes now, while the game goes on");
	QueueReport(report, ranked, rankedId, facts, factsRead);
}

}

void LeagueMatchEnded()
{
	LeagueReport report;
	std::string why;
	bool ready = false, ranked = false, factsRead = false;
	int64_t rankedId = 0;
	GameRecordFacts facts;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_rankedNow = false;
		g_teamNow = false;
		g_dropNow = false;
		if (!g_match.active) {
			why = "it was not recorded";
		}
		else if (g_match.reported) {
			// Reported when it was decided.
			g_match = Match();
			return;
		}
		else {
			Poll(g_match);
			ranked = g_match.ranked;
			rankedId = g_match.rankedId;
			facts = g_match.facts;
			factsRead = g_match.factsRead;
			ready = BuildReport(g_match, report, why);
			g_match = Match();
		}
	}
	if (!ready) {
		if (!why.empty() && why != "it was not recorded") LogI("league", "the online game is not reported: %s", why.c_str());
		// Without a report the queue follows the server again instead of staying in the match.
		RankedGameEnded(rankedId);
		return;
	}
	QueueReport(report, ranked, rankedId, facts, factsRead);
}

void LeagueMatchSurrendered(int player)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_match.active) g_match.surrendered.insert(player);
}

void LeagueMatchDesync()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_match.active) g_match.desync = true;
}

bool LeagueMatchRanked()
{
	return g_rankedNow;
}

bool LeagueMatchDropRules()
{
	return g_dropNow;
}

bool LeagueMatchRankedTeam()
{
	return g_teamNow;
}


void LeagueMatchPlayerQuit(int player)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_match.active && g_match.dropRules) g_match.quitters.insert(player);
}

bool LeagueTeamDropOpen(int player)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || !g_match.dropRules) return true;
	auto it = g_match.seats.find(player);
	if (it == g_match.seats.end()) return true;
	const Seat &s = it->second;
	return !s.quit && !s.gone && !(s.left && LeagueTeamDropOver(s.leftAt, g_match.clockMs));
}

namespace leagueclient_impl {

bool MatchTeamGame(TeamGame &out)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || !g_match.dropRules) return false;
	out = TeamGame();
	out.team = g_match.team;
	out.matchId = g_match.rankedId;
	out.clockMs = g_match.clockMs;
	out.decided = g_match.decided || g_match.desync;
	for (const auto &kv : g_match.seats) {
		const Seat &s = kv.second;
		if (!s.human) continue;
		TeamSeat t;
		t.index = s.index;
		t.team = s.team;
		t.name = s.name;
		t.steamId = s.steamId;
		t.local = s.local;
		t.connected = s.local || !s.left;
		t.gone = s.gone;
		t.quit = s.quit;
		t.leftAt = s.leftAt;
		out.seats.push_back(t);
	}
	return true;
}

}

bool LeagueSurrenderLocked()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_match.active && g_match.ranked && !g_match.decided && !LeagueSurrenderAllowed(true, g_match.clockMs);
}

void LeagueSyncAfterAdvance(void *transport)
{
	if (!transport) return;
	const char *t = static_cast<const char *>(transport);
	const uint32_t tick = *reinterpret_cast<const uint32_t *>(t + kTransportTick);
	if (!tick) return;
	std::lock_guard<std::mutex> lock(g_syncMx);
	g_checkpoints.Add(tick - 1, *reinterpret_cast<const uint32_t *>(t + kTransportPrevHash));
	// The stats of a ranked or tournament game, at the same ticks on every computer: the league compares the two reports' numbers.
	if (!g_dropNow || !g_tickStats.Due(tick)) return;
	StatSample samples[kStatMaxPlayers];
	bool active[kStatMaxPlayers];
	if (!GameStatsReadAll(samples, active)) return;
	for (int i = 0; i < kStatMaxPlayers; i++) {
		if (!active[i]) continue;
		LeagueStatsPoint p;
		p.tick = tick;
		p.ms = samples[i].ms;
		std::copy(std::begin(samples[i].values), std::end(samples[i].values), std::begin(p.values));
		g_tickStats.Add(i, p);
	}
}
