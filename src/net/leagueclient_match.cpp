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
#include "core/leagueranked.h"
#include "core/log.h"
#include "core/names.h"
#include "game/civs.h"
#include "game/gamestats.h"
#include "hooks/tweaks.h"
#include "net/leagueclient_internal.h"
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

typedef int(__cdecl *IsDemo_t)();

struct Seat {
	int index = 0;
	std::string name, civ;
	uint64_t steamId = 0;
	bool human = false, local = false, connected = true, left = false, surrendered = false;
	int team = 0, state = 0;
	// The game's time when they left.
	uint32_t leftAt = 0;
};

struct Match {
	bool active = false;
	LeagueMatchInfo info;
	int64_t localStartMs = 0;
	std::map<int, Seat> seats;
	std::set<int> surrendered;
	uint32_t clockMs = 0, decidedMs = 0;
	bool decided = false, desync = false;
	DWORD lastPoll = 0;
	// A ranked match (the room's tag) and the wait for an opponent who dropped, which holds the game's pause.
	bool ranked = false;
	int64_t rankedId = 0;
	LeagueDropWait drop;
	std::string dropName;
	bool pauseHeld = false;
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
std::atomic<bool> g_rankedNow{false};

int64_t TickMs()
{
	return static_cast<int64_t>(GetTickCount64());
}

std::mutex g_syncMx;
LeagueCheckpoints g_checkpoints;

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

void DropStep(Match &m);

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
		if (s.name != p.info.name || (s.human && !s.steamId)) {
			s.name = p.info.name;
			if (s.human) s.steamId = SteamIdOf(m, s);
		}
		s.team = p.info.team;
		const int before = s.state;
		// The states only move forward: a reload for a rejoin or the game being torn down never undo a defeat or a victory. A drop may
		// defeat the player in the same second, so leaving is judged on the state before.
		if (p.state >= 1 && p.state <= 3 && s.state != 3 && !(s.state == 2 && p.state == 1)) s.state = p.state;
		if (m.surrendered.count(s.index)) s.surrendered = true;
		if (!s.human || s.local || mask < 0 || s.index >= 8 || s.index == localIndex) continue;
		const bool connected = (mask & (1 << s.index)) != 0;
		if (s.connected && !connected && before == 1 && !m.decided) {
			s.left = true;
			s.leftAt = clock;
			LogI("league", "player %d left the game while playing", s.index);
		}
		else if (!s.connected && connected && s.left) {
			s.left = false;
			LogI("league", "player %d is back in the game", s.index);
		}
		s.connected = connected;
	}
	if (!m.decided) {
		for (const auto &kv : m.seats) {
			if (kv.second.state != 3) continue;
			m.decided = true;
			m.decidedMs = clock;
			LogI("league", "the game has a winner at %u s", clock / 1000);
			break;
		}
	}
	if (m.ranked) DropStep(m);
}

// Under g_mx, on the game thread. The opponent is gone and nobody else plays this game, so pausing it here holds it for both.
void HoldPause(Match &m, bool hold)
{
	void *game = *reinterpret_cast<void **>(addr::kGame);
	if (hold) {
		if (!m.pauseHeld) {
			m.pauseHeld = true;
			TweaksHoldPause(true);
			LogI("league", "ranked: the game is paused while it waits for the opponent");
		}
		if (game && !TweaksGamePaused(game)) TweaksSetPausedQuietly(game, true);
		return;
	}
	if (!m.pauseHeld) return;
	m.pauseHeld = false;
	TweaksHoldPause(false);
	// A rejoin lets the pause go itself, when the player who comes back is in the room.
	if (game && TweaksGamePaused(game) && !RejoinHosting()) {
		TweaksSetPausedQuietly(game, false);
		LogI("league", "ranked: the game goes on");
	}
}

// Under g_mx, on the game thread: a ranked 1v1 waits, paused, for the opponent who dropped (2-clasificatoria-sistema.md §8.3).
void DropStep(Match &m)
{
	const Seat *other = nullptr;
	int humans = 0;
	for (const auto &kv : m.seats) {
		if (!kv.second.human) continue;
		humans++;
		if (!kv.second.local) other = &kv.second;
	}
	if (humans != 2 || !other) return;
	if (other->left && !m.drop.gone && !m.drop.claimed && !m.decided && !m.desync) {
		LeagueDropGone(m.drop, TickMs(), other->leftAt);
		m.dropName = other->name;
		LogI("league", "ranked: the opponent dropped at %u s of the game; waiting %d s for them", other->leftAt / 1000,
			static_cast<int>(kLeagueDropWaitMs / 1000));
	}
	else if (!other->left && m.drop.gone && !m.drop.claimed) {
		LeagueDropBack(m.drop);
		LogI("league", "ranked: the opponent is back in the game");
	}
	const bool waiting = (m.drop.gone || m.drop.claimed) && !m.decided && !m.desync;
	HoldPause(m, waiting && !RejoinHosting());
}

// The game's time when the other side left and did not come back; -1 when nobody did.
int64_t DropAtOf(const Match &m)
{
	int64_t at = -1;
	for (const auto &kv : m.seats) {
		const Seat &s = kv.second;
		if (s.local || !s.human || !s.left) continue;
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
	const LeagueDropPhase drop = LeagueDropPhaseAt(m.drop, TickMs());
	rules.claimable = drop == LeagueDropPhase::Claimable || drop == LeagueDropPhase::Claimed;
	const LeagueOutcome outcome = LeagueResolve(seats, m.desync, rules);
	r.players = LeagueReportPlayers(seats, outcome);
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
	}
	if (outcome.localLeft) LogI("league", "this player left the game before it was decided");
	if (drop == LeagueDropPhase::Claimed) LogI("league", "ranked: the win was claimed after the opponent did not come back");
	return true;
}

}

namespace leagueclient_impl {

void MatchTick()
{
	bool ranked = false, held = false, over = false;
	int64_t rankedId = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		ranked = g_match.active && g_match.ranked;
		rankedId = g_match.rankedId;
		held = g_match.pauseHeld;
		over = g_match.decided || g_match.desync;
	}
	PauseTick(ranked, rankedId, held, over);
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

void LeagueMatchPlaying(const LeagueMatchInfo &info)
{
	if (!info.lobby || SpectatorWatching() || SpectatorViewing() || reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) return;
	LeagueMatchKind kind = LeagueMatchKind::Normal;
	int64_t matchId = 0;
	const bool ranked = LeagueParseRoomTag(info.roomTag, kind, matchId) && kind == LeagueMatchKind::Ranked;
	if (ranked) RankedGameStarted(matchId);
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || g_match.info.lobby != info.lobby) {
		if (g_match.pauseHeld) TweaksHoldPause(false);
		g_match = Match();
		g_match.active = true;
		g_match.info = info;
		g_match.localStartMs = NowMs();
		g_match.ranked = ranked;
		g_match.rankedId = ranked ? matchId : 0;
		g_rankedNow = ranked;
		LogI("league", "recording the online game (%s)", LeagueLinked() ? "it will be reported" : "the account is not linked, so it won't be reported");
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
	for (const auto &member : info.members) {
		const bool known = std::any_of(m.info.members.begin(), m.info.members.end(), [&](const std::pair<uint64_t, std::string> &k) { return k.first == member.first; });
		if (!known) m.info.members.push_back(member);
	}
}

void LeagueMatchEnded()
{
	LeagueReport report;
	std::string why;
	bool ready = false, ranked = false;
	int64_t rankedId = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_rankedNow = false;
		if (!g_match.active) {
			why = "it was not recorded";
		}
		else {
			Poll(g_match);
			ranked = g_match.ranked;
			rankedId = g_match.rankedId;
			if (g_match.pauseHeld) TweaksHoldPause(false);
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
	if (report.kind == LeagueMatchKind::Ranked) RankedGameReported(report, steamId);
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

bool LeagueSurrenderLocked()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_match.active && g_match.ranked && !g_match.decided && !LeagueSurrenderAllowed(true, g_match.clockMs);
}

LeagueDropView LeagueDropState()
{
	LeagueDropView v;
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || !g_match.ranked) return v;
	const int64_t now = TickMs();
	v.phase = LeagueDropPhaseAt(g_match.drop, now);
	if (v.phase == LeagueDropPhase::None || g_match.decided || g_match.desync) {
		v.phase = LeagueDropPhase::None;
		return v;
	}
	v.opponent = g_match.dropName;
	v.leftMs = LeagueDropLeftMs(g_match.drop, now);
	v.returning = RejoinHosting();
	return v;
}

bool LeagueDropClaimWin()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || !g_match.ranked || RejoinHosting() || !LeagueDropClaim(g_match.drop, TickMs())) return false;
	LogI("league", "ranked: the win is claimed after waiting for the opponent; leaving the game");
	return true;
}

bool LeagueDropHoldsPause()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_match.active && g_match.pauseHeld && !g_match.drop.claimed;
}

void LeagueDropReleasePause()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_match.active || !g_match.ranked || (!g_match.drop.gone && !g_match.pauseHeld)) return;
	if (g_match.pauseHeld) {
		g_match.pauseHeld = false;
		TweaksHoldPause(false);
	}
	void *game = *reinterpret_cast<void **>(addr::kGame);
	if (game && TweaksGamePaused(game)) TweaksSetPausedQuietly(game, false);
	LogI("league", "ranked: the opponent is coming back; the game runs again to bring them in");
}

void LeagueSyncAfterAdvance(void *transport)
{
	if (!transport) return;
	const char *t = static_cast<const char *>(transport);
	const uint32_t tick = *reinterpret_cast<const uint32_t *>(t + kTransportTick);
	if (!tick) return;
	std::lock_guard<std::mutex> lock(g_syncMx);
	g_checkpoints.Add(tick - 1, *reinterpret_cast<const uint32_t *>(t + kTransportPrevHash));
}
