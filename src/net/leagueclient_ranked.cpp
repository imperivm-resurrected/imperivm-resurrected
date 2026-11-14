// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/leagueclient.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "core/log.h"
#include "mods/mods.h"
#include "net/leagueclient_internal.h"
#include "net/patrons.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kQueuePollMs = 5000, kMatchPollMs = 3000, kPlayingPollMs = 10000, kResultPollMs = 10000, kRetryMs = 6000, kPresenceMs = 30000;
const DWORD kSeasonKeepMs = 10 * 60 * 1000, kSeasonRetryMs = 60 * 1000, kBoardKeepMs = 60 * 1000, kRecoverAfterMs = 3000;
const int kHubRows = 10, kAroundRows = 5;
const size_t kRecentMax = 20;
// A game only one side reported is settled by the server 15 minutes after the report; the screen stops asking a little later.
const int64_t kAwaitMaxMs = 20 * 60 * 1000;

struct Queue {
	LeagueRankedPhase phase = LeagueRankedPhase::Idle;
	// A request of the queue or of the match is on its way: one at a time.
	bool busy = false;
	bool recovered = false;
	DWORD startedAt = 0, nextPoll = 0;
	// Changes whenever the player starts or drops a search or a match: answers to an older one are dropped.
	unsigned gen = 1;
	int64_t since = 0;
	int searching = 0, estimate = 0;
	LeagueQueueAnswer found;
	int64_t matchId = 0;
	bool ticketLoaded = false;
	LeagueMatchTicket ticket;
	bool answering = false;
	// The host's room: the one to register, the one the server has, and when to try again.
	uint64_t wantLobby = 0, roomLobby = 0;
	bool roomRegistered = false, registering = false;
	DWORD registerAt = 0;
	int64_t banUntil = 0;
	std::string notice;
	bool noticeBad = false;
	unsigned foundSerial = 0;
	bool rejoined = false;
	bool abandoning = false;
	int64_t calledOff = 0;
	// The last "I am in the room", and the match whose start the server was told of.
	DWORD presenceAt = 0;
	int64_t startedSent = 0;
};

struct Board {
	bool loading = false, loaded = false;
	DWORD at = 0;
	LeagueBoard top, around;
	bool pageLoading = false, pageLoaded = false;
	unsigned pageGen = 0;
	LeagueBoard page;
	std::string pageError;
};

struct Season {
	bool loading = false, loaded = false, none = false;
	DWORD at = 0;
	std::string lang;
	LeagueSeason season;
};

struct Result {
	LeagueRankedOutcome view;
	DWORD nextPoll = 0;
	int64_t since = 0;
	bool polling = false;
};

struct Recent {
	bool loaded = false;
	std::vector<LeagueRankedGame> games;
};

struct Badge {
	bool busy = false;
	std::string error;
};

std::mutex g_mx;
Queue g_q;
Board g_board;
Season g_season;
Result g_result;
Recent g_recent;
Badge g_badge;

// A request and what came back.
struct Answer {
	HttpReply r;
	Json j;
	bool parsed = false;
	LeagueError error;
};

Answer Call(const char *method, const std::string &path, const std::string &body, bool auth)
{
	Answer a;
	a.r = Http(method, path, body, auth);
	a.parsed = !a.r.body.empty() && Json::Parse(a.r.body, a.j);
	if (!a.r.Ok() && a.parsed) a.error = LeagueParseError(a.j);
	return a;
}

std::string OwnId()
{
	const uint64_t me = SteamOwnId();
	return me ? std::to_string(me) : std::string();
}

std::string LangCode()
{
	return LeagueLangCode(ModsLanguage());
}

std::string MatchPath(int64_t id, const char *tail = "")
{
	return "/v1/league/matches/" + std::to_string(static_cast<long long>(id)) + tail;
}

// The text for a request that failed without an answer of the server, or with one it did not expect.
std::string FailureNotice(const Answer &a)
{
	if (!a.r.failure.empty() && a.r.failure == "not linked") return "Link your league account to play ranked.";
	if (!a.r.status) return "Couldn't reach the league server. Check your connection and try again.";
	if (a.r.status == 401) return "The league no longer recognises this computer. Link your account again.";
	return "The league server refused the request. Try again later.";
}

// Under g_mx: back to not searching, forgetting the match.
void ToIdle(Queue &q)
{
	q.phase = LeagueRankedPhase::Idle;
	q.matchId = 0;
	q.ticketLoaded = false;
	q.ticket = LeagueMatchTicket();
	q.found = LeagueQueueAnswer();
	q.answering = false;
	q.abandoning = false;
	q.wantLobby = q.roomLobby = 0;
	q.roomRegistered = q.registering = false;
	q.presenceAt = 0;
	q.gen++;
}

void SetNotice(Queue &q, const std::string &text, bool bad)
{
	q.notice = text;
	q.noticeBad = bad;
}

std::string RecentPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\') + 1) + "imperivm-resurrected\\league\\ranked.json";
}

// Under g_mx.
void LoadRecent()
{
	if (g_recent.loaded) return;
	g_recent.loaded = true;
	Json j;
	if (!ReadJsonFile(RecentPath(), j) || j.type != Json::Array) return;
	for (const Json &e : j.arr) {
		if (e.type != Json::Object) continue;
		LeagueRankedGame g;
		g.matchId = static_cast<int64_t>(e.GetNumber("matchId"));
		g.at = static_cast<int64_t>(e.GetNumber("at"));
		g.opponent = e.GetString("opponent");
		g.opponentId = e.GetString("opponentId");
		g.map = e.GetString("map");
		g.won = e.GetBool("won");
		g.delta = static_cast<int>(e.GetNumber("delta"));
		g.durationMs = static_cast<uint32_t>(e.GetNumber("durationMs"));
		g.status = e.GetString("status");
		g.tierAfter = LeagueTierFromId(e.GetString("tierAfter"));
		g.divisionAfter = static_cast<int>(e.GetNumber("divisionAfter"));
		if (g.matchId > 0) g_recent.games.push_back(g);
	}
}

// Under g_mx.
void SaveRecent()
{
	Json list = Json::MakeArray();
	for (const LeagueRankedGame &g : g_recent.games) {
		Json e = Json::MakeObject();
		e.Set("matchId", Json::MakeNumber(static_cast<double>(g.matchId)));
		e.Set("at", Json::MakeNumber(static_cast<double>(g.at)));
		e.Set("opponent", Json::MakeString(g.opponent));
		e.Set("opponentId", Json::MakeString(g.opponentId));
		e.Set("map", Json::MakeString(g.map));
		e.Set("won", Json::MakeBool(g.won));
		e.Set("delta", Json::MakeNumber(g.delta));
		e.Set("durationMs", Json::MakeNumber(g.durationMs));
		e.Set("status", Json::MakeString(g.status));
		e.Set("tierAfter", Json::MakeString(LeagueTierId(g.tierAfter)));
		e.Set("divisionAfter", Json::MakeNumber(g.divisionAfter));
		list.arr.push_back(e);
	}
	const std::string path = RecentPath(), dir = path.substr(0, path.find_last_of('\\'));
	CreateDirectoryA(dir.substr(0, dir.find_last_of('\\')).c_str(), nullptr);
	CreateDirectoryA(dir.c_str(), nullptr);
	if (!WriteJsonFile(path, list)) LogW("league", "could not keep the list of ranked games");
}

// Under g_mx: the game goes first, replacing an older entry of the same match.
void KeepRecent(const LeagueRankedGame &g)
{
	LoadRecent();
	auto &games = g_recent.games;
	games.erase(std::remove_if(games.begin(), games.end(), [&](const LeagueRankedGame &o) { return o.matchId == g.matchId; }), games.end());
	games.insert(games.begin(), g);
	if (games.size() > kRecentMax) games.resize(kRecentMax);
	SaveRecent();
}

// Under g_mx: what an answer of the server says of a ranked game this computer remembers.
void ApplyAnswer(int64_t matchId, const LeagueReportAnswer &answer)
{
	LoadRecent();
	for (LeagueRankedGame &g : g_recent.games) {
		if (g.matchId != matchId) continue;
		g.status = answer.status;
		if (answer.ranked.present) {
			g.delta = answer.ranked.delta;
			g.tierAfter = answer.ranked.tierAfter;
			g.divisionAfter = answer.ranked.divisionAfter;
		}
		SaveRecent();
		break;
	}
	LeagueRankedOutcome &v = g_result.view;
	if (v.state == LeagueResultState::None || v.game.matchId != matchId) return;
	v.answer = answer;
	v.game.status = answer.status;
	if (answer.ranked.present) v.game.delta = answer.ranked.delta;
	v.state = answer.status == "awaiting_other" ? LeagueResultState::Awaiting : LeagueResultState::Ready;
	v.serial++;
	g_result.nextPoll = GetTickCount() + kResultPollMs;
}

void FetchSeason(std::string lang)
{
	const Answer a = Call("GET", "/v1/league/season?lang=" + lang, "", false);
	LeagueSeason s;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseSeason(a.j, s);
	if (!ok && a.r.status != 404) LogW("league", "could not read the season (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_season.loading = false;
	g_season.at = GetTickCount();
	if (ok) {
		g_season.loaded = true;
		g_season.none = false;
		g_season.season = s;
		g_season.lang = lang;
	}
	else if (a.r.status == 404) {
		g_season.loaded = true;
		g_season.none = true;
	}
}

void FetchBoard(bool linked)
{
	const Answer top = Call("GET", "/v1/league/leaderboard?queue=1v1&offset=0&limit=" + std::to_string(kHubRows), "", false);
	Answer around;
	if (linked) around = Call("GET", "/v1/league/leaderboard?queue=1v1&around=me&limit=" + std::to_string(kAroundRows), "", true);
	LeagueBoard t, m;
	const bool okTop = top.r.Ok() && top.parsed && LeagueParseBoard(top.j, t);
	const bool okAround = linked && around.r.Ok() && around.parsed && LeagueParseBoard(around.j, m);
	if (!okTop) LogW("league", "could not read the leaderboard (%s)", top.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_board.loading = false;
	g_board.at = GetTickCount();
	if (okTop) {
		g_board.loaded = true;
		g_board.top = t;
	}
	if (okAround) g_board.around = m;
	else if (!linked) g_board.around = LeagueBoard();
}

void FetchPage(unsigned gen, int offset, int limit, bool aroundMe)
{
	std::string path = "/v1/league/leaderboard?queue=1v1&limit=" + std::to_string(limit);
	path += aroundMe ? "&around=me" : "&offset=" + std::to_string(offset);
	const Answer a = Call("GET", path, "", aroundMe);
	LeagueBoard b;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseBoard(a.j, b);
	if (!ok) LogW("league", "could not read a page of the leaderboard (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (gen != g_board.pageGen) return;
	g_board.pageLoading = false;
	if (ok) {
		g_board.pageLoaded = true;
		g_board.page = b;
		g_board.pageError.clear();
	}
	else g_board.pageError = !a.r.status ? "Couldn't reach the league server. Check your connection and try again." : "The league server refused the request. Try again later.";
}

void LeaveThread()
{
	const Answer a = Call("DELETE", "/v1/league/queue", "", true);
	if (a.r.Ok()) LogI("league", "ranked: left the queue");
	else LogW("league", "ranked: could not leave the queue (%s)", a.r.Describe().c_str());
}

// Under g_mx: the queue answers that this player is in a match already (a restarted mod, or a match whose room failed).
void ResumeMatch(int64_t matchId)
{
	g_q.phase = LeagueRankedPhase::Ready;
	g_q.matchId = matchId;
	g_q.ticketLoaded = false;
	g_q.nextPoll = 0;
	g_q.gen++;
	LogI("league", "ranked: match %lld is still on; following it", static_cast<long long>(matchId));
}

void JoinThread(unsigned gen, std::string contentKey)
{
	Json body = Json::MakeObject();
	body.Set("queue", Json::MakeString("1v1"));
	body.Set("contentKey", Json::MakeString(contentKey));
	const Answer a = Call("POST", "/v1/league/queue", body.Dump(), true);
	LeagueQueueAnswer q;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseQueue(a.j, q);
	bool leave = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.busy = false;
		if (gen != g_q.gen) leave = ok;
		else if (ok) {
			g_q.phase = LeagueRankedPhase::Searching;
			g_q.since = q.since ? q.since : NowMs();
			g_q.searching = q.searching;
			g_q.estimate = q.estimateSec;
			g_q.nextPoll = GetTickCount() + kQueuePollMs;
			LogI("league", "ranked: searching (%d in the queue, about %d s)", q.searching, q.estimateSec);
		}
		else {
			const std::string &code = a.error.code;
			g_q.phase = LeagueRankedPhase::Idle;
			if (code == "in_match" && a.error.matchId) ResumeMatch(a.error.matchId);
			else if (code == "banned") {
				g_q.banUntil = a.error.until;
				SetNotice(g_q, "You declined or let a match go: you can't search for a while.", true);
			}
			else if (code == "wrong_content") SetNotice(g_q, "This season is played with other gameplay content. Change your content profile to search.", true);
			else if (code == "no_season") SetNotice(g_q, "No ranked season is being played now.", true);
			else SetNotice(g_q, FailureNotice(a), true);
			LogW("league", "ranked: could not enter the queue (%s)", a.r.Describe().c_str());
		}
	}
	if (leave) LeaveThread();
}

void PollQueueThread(unsigned gen, bool recovering)
{
	const Answer a = Call("GET", "/v1/league/queue", "", true);
	LeagueQueueAnswer q;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseQueue(a.j, q);
	bool rejoin = false;
	std::string contentKey;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.busy = false;
		if (gen != g_q.gen) return;
		g_q.nextPoll = GetTickCount() + (ok ? kQueuePollMs : kRetryMs);
		if (!ok) {
			if (a.r.status == 401 || a.r.failure == "not linked") {
				ToIdle(g_q);
				SetNotice(g_q, FailureNotice(a), true);
			}
			else if (!recovering) LogD("league", "ranked: no answer from the queue (%s)", a.r.Describe().c_str());
			return;
		}
		switch (q.status) {
		case LeagueQueueStatus::Searching:
			if (recovering) LogI("league", "ranked: still in the queue from before");
			g_q.phase = LeagueRankedPhase::Searching;
			g_q.since = q.since ? q.since : g_q.since;
			g_q.searching = q.searching;
			g_q.estimate = q.estimateSec;
			g_q.rejoined = false;
			break;
		case LeagueQueueStatus::Found:
			if (q.matchId != g_q.matchId || g_q.phase == LeagueRankedPhase::Searching || g_q.phase == LeagueRankedPhase::Idle) {
				g_q.found = q;
				g_q.matchId = q.matchId;
				g_q.ticketLoaded = false;
				g_q.answering = false;
				g_q.phase = q.accepted ? LeagueRankedPhase::Accepted : LeagueRankedPhase::Found;
				g_q.foundSerial++;
				g_q.notice.clear();
				LogI("league", "ranked: match %lld found against %s", static_cast<long long>(q.matchId), q.opponent.name.c_str());
			}
			g_q.nextPoll = GetTickCount();
			break;
		case LeagueQueueStatus::Idle:
			if (q.matchId) {
				ResumeMatch(q.matchId);
				break;
			}
			if (g_q.phase == LeagueRankedPhase::Searching && !g_q.rejoined) {
				// The server drops whoever stops polling for 30 s (a long load, a sleeping computer): enter again once, quietly.
				g_q.rejoined = true;
				rejoin = true;
				contentKey = SteamOwnGameplayKey();
				g_q.busy = true;
				LogI("league", "ranked: the server dropped the search; entering the queue again");
			}
			else if (g_q.phase == LeagueRankedPhase::Searching) {
				ToIdle(g_q);
				SetNotice(g_q, "The search stopped. Search again when you are ready.", true);
			}
			break;
		default: break;
		}
	}
	if (rejoin) JoinThread(gen, contentKey);
}

// Under g_mx: the match was called off before its game (abandoned). Both are free to search again, unless this player was at fault.
void CalledOff(const LeagueMatchTicket &t)
{
	const std::string me = OwnId();
	LogI("league", "ranked: match %lld called off before its game by %s, %s at fault", static_cast<long long>(t.id),
		t.abandonedBy == me ? "this player" : t.abandonedBy == "system" ? "the server" : "the other player",
		t.fault.empty() ? "nobody" : t.fault == me ? "this player" : "the other player");
	ToIdle(g_q);
	g_q.calledOff = t.id;
	if (t.fault == me) {
		SetNotice(g_q, t.abandonedBy == me ? "You called off the match: you can't search for a few minutes."
										   : "Your room did not open in time: the match was called off and you can't search for a few minutes.",
			true);
	}
	else if (!t.fault.empty()) SetNotice(g_q, "The match was called off and it doesn't count against you. You can search again.", false);
	else SetNotice(g_q, "The match was called off before it started. Nobody is penalised: you can search again.", false);
}

// Under g_mx: the ticket says the match was called off. Whoever had accepted goes back to the head of the queue.
void Cancelled(const LeagueMatchTicket &t)
{
	if (t.outcome == "abandoned") {
		CalledOff(t);
		return;
	}
	const LeaguePlayerCard *mine = LeagueTicketPlayer(t, OwnId(), false);
	const bool accepted = mine && mine->accepted;
	const bool declinedByOther = t.outcome == "declined";
	LogI("league", "ranked: match %lld called off (%s)", static_cast<long long>(t.id), t.outcome.c_str());
	if (declinedByOther || accepted) {
		ToIdle(g_q);
		g_q.phase = LeagueRankedPhase::Searching;
		g_q.nextPoll = 0;
		SetNotice(g_q, declinedByOther ? "Your opponent declined the match. You are back at the head of the queue."
									   : "Your opponent did not accept in time. You are back at the head of the queue.",
			false);
		return;
	}
	ToIdle(g_q);
	SetNotice(g_q, "You did not accept the match in time: you can't search for a few minutes.", true);
}

void PollMatchThread(unsigned gen, int64_t id)
{
	const Answer a = Call("GET", MatchPath(id), "", true);
	LeagueMatchTicket t;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseMatch(a.j, t);
	bool refreshMe = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.busy = false;
		if (gen != g_q.gen || id != g_q.matchId) return;
		g_q.nextPoll = GetTickCount() + (ok ? kMatchPollMs : kRetryMs);
		if (!ok) {
			if (a.r.status == 404 || a.r.status == 401) {
				ToIdle(g_q);
				SetNotice(g_q, a.r.status == 404 ? "The match no longer exists." : FailureNotice(a), true);
			}
			return;
		}
		const LeagueRankedPhase before = g_q.phase;
		g_q.ticket = t;
		g_q.ticketLoaded = true;
		const LeaguePlayerCard *mine = LeagueTicketPlayer(t, OwnId(), false);
		switch (t.status) {
		case LeagueMatchStatus::Accepting:
			if (t.acceptBy) g_q.found.acceptBy = t.acceptBy;
			if (mine && mine->accepted) g_q.phase = LeagueRankedPhase::Accepted;
			else if (g_q.phase != LeagueRankedPhase::Accepted) g_q.phase = LeagueRankedPhase::Found;
			break;
		case LeagueMatchStatus::Ready: g_q.phase = LeagueRankedPhase::Ready; break;
		case LeagueMatchStatus::Playing:
			g_q.phase = LeagueRankedPhase::Playing;
			g_q.nextPoll = GetTickCount() + kPlayingPollMs;
			break;
		case LeagueMatchStatus::Done:
			LogI("league", "ranked: match %lld is over (%s)", static_cast<long long>(id), t.outcome.c_str());
			ToIdle(g_q);
			refreshMe = true;
			break;
		case LeagueMatchStatus::Cancelled:
			Cancelled(t);
			refreshMe = true;
			break;
		default: break;
		}
		if (before != g_q.phase && (g_q.phase == LeagueRankedPhase::Ready || g_q.phase == LeagueRankedPhase::Playing)) {
			LogI("league", "ranked: match %lld %s, host %s", static_cast<long long>(id), g_q.phase == LeagueRankedPhase::Ready ? "ready" : "has its room",
				t.host == OwnId() ? "this computer" : "the other player");
		}
	}
	if (refreshMe) LeagueMeRefresh(true);
}

void AnswerThread(unsigned gen, int64_t id, bool accept)
{
	const Answer a = Call("POST", MatchPath(id, accept ? "/accept" : "/decline"), "", true);
	bool refreshMe = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.answering = false;
		if (gen != g_q.gen || id != g_q.matchId) return;
		if (!a.r.Ok()) {
			LogW("league", "ranked: could not %s the match (%s)", accept ? "accept" : "decline", a.r.Describe().c_str());
			if (a.r.status == 409) g_q.nextPoll = 0;
			else SetNotice(g_q, FailureNotice(a), true);
			return;
		}
		if (accept) {
			LeagueMatchTicket t;
			if (a.parsed && LeagueParseMatch(a.j, t)) {
				g_q.ticket = t;
				g_q.ticketLoaded = true;
				g_q.phase = t.status == LeagueMatchStatus::Ready || t.status == LeagueMatchStatus::Playing ? LeagueRankedPhase::Ready : LeagueRankedPhase::Accepted;
			}
			else g_q.phase = LeagueRankedPhase::Accepted;
			g_q.nextPoll = 0;
			LogI("league", "ranked: match %lld accepted", static_cast<long long>(id));
			return;
		}
		ToIdle(g_q);
		g_q.banUntil = a.parsed ? static_cast<int64_t>(a.j.GetNumber("queueBanUntil")) : 0;
		SetNotice(g_q, "You declined the match: you can't search for a few minutes.", true);
		LogI("league", "ranked: match %lld declined", static_cast<long long>(id));
		refreshMe = true;
	}
	if (refreshMe) LeagueMeRefresh(true);
}

void RegisterThread(unsigned gen, int64_t id, uint64_t lobby)
{
	Json body = Json::MakeObject();
	body.Set("lobby", Json::MakeString(std::to_string(lobby)));
	const Answer a = Call("PUT", MatchPath(id, "/room"), body.Dump(), true);
	std::lock_guard<std::mutex> lock(g_mx);
	g_q.registering = false;
	if (gen != g_q.gen || id != g_q.matchId || lobby != g_q.wantLobby) return;
	if (!a.r.Ok()) {
		LogW("league", "ranked: the server did not take the room (%s)", a.r.Describe().c_str());
		g_q.registerAt = GetTickCount() + (a.r.status == 403 || a.r.status == 409 ? 30000 : 3000);
		return;
	}
	g_q.roomRegistered = true;
	g_q.roomLobby = lobby;
	LeagueMatchTicket t;
	if (a.parsed && LeagueParseMatch(a.j, t)) {
		g_q.ticket = t;
		g_q.ticketLoaded = true;
	}
	g_q.phase = LeagueRankedPhase::Playing;
	g_q.nextPoll = GetTickCount() + kPlayingPollMs;
	LogI("room", "ranked: room %llu registered for match %lld", lobby, static_cast<long long>(id));
}

void AbandonThread(unsigned gen, int64_t id, bool automatic)
{
	const Answer a = Call("POST", MatchPath(id, "/abandon"), "", true);
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.abandoning = false;
		if (gen != g_q.gen || id != g_q.matchId) return;
		if (!a.r.Ok()) {
			LogW("league", "ranked: could not call off match %lld (%s)", static_cast<long long>(id), a.r.Describe().c_str());
			if (a.error.code == "started") SetNotice(g_q, "The game has started: the match can't be called off.", true);
			else if (a.r.status == 404) {
				ToIdle(g_q);
				SetNotice(g_q, "The match no longer exists.", true);
			}
			else if (!automatic) SetNotice(g_q, FailureNotice(a), true);
			g_q.nextPoll = 0;
			return;
		}
		LeagueMatchTicket t;
		if (a.parsed && LeagueParseMatch(a.j, t) && t.outcome == "abandoned") CalledOff(t);
		else ToIdle(g_q);
		if (a.parsed && a.j.GetNumber("queueBanUntil") > 0) g_q.banUntil = static_cast<int64_t>(a.j.GetNumber("queueBanUntil"));
		LogI("league", "ranked: match %lld called off%s", static_cast<long long>(id), automatic ? " after its room failed" : "");
	}
	LeagueMeRefresh(true);
}

void PresenceThread(int64_t id, bool started)
{
	Json body = Json::MakeObject();
	if (started) body.Set("started", Json::MakeBool(true));
	const Answer a = Call("POST", MatchPath(id, "/presence"), body.Dump(), true);
	if (a.r.Ok()) {
		if (started) LogI("league", "ranked: the server knows the game of match %lld started", static_cast<long long>(id));
		return;
	}
	LogD("league", "ranked: the presence in match %lld was not taken (%s)", static_cast<long long>(id), a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	// The start is told again on the next try.
	if (started && g_q.startedSent == id) g_q.startedSent = 0;
}

void PollResultThread(int64_t id)
{
	const Answer a = Call("GET", MatchPath(id), "", true);
	LeagueMatchTicket t;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseMatch(a.j, t);
	bool refresh = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_result.polling = false;
		g_result.nextPoll = GetTickCount() + kResultPollMs;
		LeagueRankedOutcome &v = g_result.view;
		if (!ok || v.state != LeagueResultState::Awaiting || v.game.matchId != id || t.status != LeagueMatchStatus::Done) return;
		LeagueReportAnswer answer = v.answer;
		// The ticket does not say whether the other report came or the single report stood: only that it was settled.
		answer.status = t.outcome == "disputed" ? "disputed" : "settled";
		answer.outcome = t.outcome;
		answer.ranked = t.ranked;
		ApplyAnswer(id, answer);
		refresh = true;
		LogI("league", "ranked: the result of match %lld is settled (%s)", static_cast<long long>(id), t.outcome.c_str());
	}
	if (refresh) {
		LeagueMeRefresh(true);
		LeagueBoardRefresh(true);
	}
}

void BadgeThread(std::string kind, std::string id)
{
	Json body = Json::MakeObject();
	body.Set("kind", Json::MakeString(kind));
	if (!id.empty()) body.Set("id", Json::MakeNumber(static_cast<double>(_strtoi64(id.c_str(), nullptr, 10))));
	const Answer a = Call("PUT", "/v1/league/me/badge", body.Dump(), true);
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_badge.busy = false;
		g_badge.error = a.r.Ok() ? std::string() : a.r.status ? std::string("The league server refused the request. Try again later.")
															 : std::string("Couldn't reach the league server. Check your connection and try again.");
	}
	if (!a.r.Ok()) {
		LogW("league", "could not change the badge (%s)", a.r.Describe().c_str());
		return;
	}
	LogI("league", "badge next to the name: %s", kind.c_str());
	LeagueMeRefresh(true);
	PatronsForget(SteamOwnId());
	PatronsWatch({SteamOwnId()});
}

}

namespace leagueclient_impl {

void RankedTick()
{
	static DWORD lastLinkCheck = 0;
	static bool linked = false;
	const DWORD now = GetTickCount();
	if (now - lastLinkCheck > 500) {
		lastLinkCheck = now;
		linked = LeagueLinked();
	}
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_q.startedAt) g_q.startedAt = now;
	if (!linked) {
		if (g_q.phase != LeagueRankedPhase::Idle) ToIdle(g_q);
		g_q.recovered = false;
		return;
	}
	// A restarted mod asks once whether it was searching or had a match.
	if (!g_q.recovered && !g_q.busy && SteamOwnId() && now - g_q.startedAt > kRecoverAfterMs) {
		g_q.recovered = true;
		if (g_q.phase == LeagueRankedPhase::Idle) {
			g_q.busy = true;
			SafeThread(PollQueueThread, g_q.gen, true).detach();
		}
	}
	if (!g_q.busy && static_cast<int>(now - g_q.nextPoll) >= 0) {
		switch (g_q.phase) {
		case LeagueRankedPhase::Searching:
			g_q.busy = true;
			SafeThread(PollQueueThread, g_q.gen, false).detach();
			break;
		case LeagueRankedPhase::Found:
		case LeagueRankedPhase::Accepted:
		case LeagueRankedPhase::Ready:
		case LeagueRankedPhase::Playing:
			if (g_q.matchId) {
				g_q.busy = true;
				SafeThread(PollMatchThread, g_q.gen, g_q.matchId).detach();
			}
			break;
		default: break;
		}
	}
	const bool host = g_q.ticketLoaded && g_q.ticket.host == OwnId();
	if (host && g_q.wantLobby && (!g_q.roomRegistered || g_q.roomLobby != g_q.wantLobby) && !g_q.registering &&
		static_cast<int>(now - g_q.registerAt) >= 0 && (g_q.phase == LeagueRankedPhase::Ready || g_q.phase == LeagueRankedPhase::Playing)) {
		g_q.registering = true;
		SafeThread(RegisterThread, g_q.gen, g_q.matchId, g_q.wantLobby).detach();
	}
	LeagueRankedOutcome &v = g_result.view;
	if (v.state == LeagueResultState::Awaiting && !g_result.polling && static_cast<int>(now - g_result.nextPoll) >= 0) {
		if (NowMs() - g_result.since > kAwaitMaxMs) {
			g_result.nextPoll = now + 60000;
		}
		else {
			g_result.polling = true;
			SafeThread(PollResultThread, v.game.matchId).detach();
		}
	}
}

void RankedGameReported(const LeagueReport &report, const std::string &ownSteamId)
{
	LeagueRankedGame g;
	g.matchId = report.matchId;
	g.at = NowMs();
	g.durationMs = report.durationMs;
	g.map = report.map.name;
	for (const LeagueReportPlayer &p : report.players) {
		if (p.steamId == ownSteamId) g.won = p.result == "win";
		else {
			g.opponent = p.name;
			g.opponentId = p.steamId;
		}
	}
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_q.ticketLoaded && g_q.ticket.id == report.matchId) {
		if (!g_q.ticket.map.name.empty()) g.map = g_q.ticket.map.name;
		if (const LeaguePlayerCard *other = LeagueTicketPlayer(g_q.ticket, ownSteamId, true); other && !other->name.empty()) g.opponent = other->name;
	}
	g_result.view.state = LeagueResultState::Sending;
	g_result.view.game = g;
	g_result.view.answer = LeagueReportAnswer();
	g_result.view.serial++;
	g_result.since = NowMs();
	KeepRecent(g);
	if (g_q.matchId == report.matchId) ToIdle(g_q);
	LogI("league", "ranked: match %lld played (%s); waiting for the server", static_cast<long long>(report.matchId), g.won ? "won" : "lost");
}

void RankedReportAnswered(const Json &report, const std::string &answer)
{
	if (report.GetString("kind") != "ranked") return;
	const int64_t matchId = static_cast<int64_t>(report.GetNumber("matchId"));
	Json j;
	LeagueReportAnswer a;
	if (!matchId || !Json::Parse(answer, j) || !LeagueParseReportAnswer(j, a)) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		ApplyAnswer(matchId, a);
	}
	LogI("league", "ranked: the server answered the report of match %lld: %s%s", static_cast<long long>(matchId), a.status.c_str(),
		a.ranked.present ? (" (" + std::to_string(a.ranked.delta) + " Glory)").c_str() : "");
	LeagueMeRefresh(true);
	LeagueBoardRefresh(true);
}

void RankedGameStarted(int64_t matchId)
{
	if (matchId <= 0) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_q.startedSent == matchId) return;
		g_q.startedSent = matchId;
	}
	SafeThread(PresenceThread, matchId, true).detach();
}

}

LeagueRankedView LeagueRankedState()
{
	LeagueRankedView v;
	v.linked = LeagueLinked();
	const std::string own = OwnId();
	std::lock_guard<std::mutex> lock(g_mx);
	v.seasonLoaded = g_season.loaded && !g_season.none;
	v.seasonLoading = g_season.loading;
	v.noSeason = g_season.loaded && g_season.none;
	v.season = g_season.season;
	v.lang = g_season.lang.empty() ? LangCode() : g_season.lang;
	v.boardLoaded = g_board.loaded;
	v.top = g_board.top;
	v.around = g_board.around;
	v.pageLoading = g_board.pageLoading;
	v.pageLoaded = g_board.pageLoaded;
	v.page = g_board.page;
	v.pageError = g_board.pageError;
	v.phase = g_q.phase;
	v.since = g_q.since;
	v.searching = g_q.searching;
	v.estimateSec = g_q.estimate;
	v.matchId = g_q.matchId;
	v.found = g_q.found;
	v.ticketLoaded = g_q.ticketLoaded;
	v.ticket = g_q.ticket;
	if (g_q.ticketLoaded) {
		if (const LeaguePlayerCard *p = LeagueTicketPlayer(g_q.ticket, own, false)) v.me = *p;
		if (const LeaguePlayerCard *p = LeagueTicketPlayer(g_q.ticket, own, true)) v.opponent = *p;
		v.host = g_q.ticket.host == own;
	}
	else v.opponent = g_q.found.opponent;
	v.roomRegistered = g_q.roomRegistered;
	v.answering = g_q.answering;
	v.abandoning = g_q.abandoning;
	v.calledOff = g_q.calledOff;
	v.banUntil = g_q.banUntil;
	v.notice = g_q.notice;
	v.noticeBad = g_q.noticeBad;
	v.foundSerial = g_q.foundSerial;
	v.result = g_result.view;
	LoadRecent();
	v.recent = g_recent.games;
	v.badgeBusy = g_badge.busy;
	v.badgeError = g_badge.error;
	return v;
}

void LeagueSeasonRefresh(bool force)
{
	const std::string lang = LangCode();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_season.loading) return;
		const DWORD keep = g_season.loaded ? kSeasonKeepMs : kSeasonRetryMs;
		if (!force && g_season.at && GetTickCount() - g_season.at < keep && g_season.lang == lang) return;
		if (!force && g_season.at && !g_season.loaded && GetTickCount() - g_season.at < kSeasonRetryMs) return;
		g_season.loading = true;
	}
	SafeThread(FetchSeason, lang).detach();
}

void LeagueBoardRefresh(bool force)
{
	const bool linked = LeagueLinked();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_board.loading) return;
		if (!force && g_board.at && GetTickCount() - g_board.at < kBoardKeepMs) return;
		g_board.loading = true;
	}
	SafeThread(FetchBoard, linked).detach();
}

void LeagueBoardRequest(int offset, int limit, bool aroundMe)
{
	if (aroundMe && !LeagueLinked()) aroundMe = false;
	unsigned gen = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		gen = ++g_board.pageGen;
		g_board.pageLoading = true;
	}
	SafeThread(FetchPage, gen, std::max(0, offset), std::max(1, std::min(100, limit)), aroundMe).detach();
}

void LeagueQueueJoin()
{
	const bool linked = LeagueLinked();
	const LeagueMeView me = LeagueMeState();
	const std::string contentKey = SteamOwnGameplayKey();
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_q.phase != LeagueRankedPhase::Idle || g_q.busy) return;
	if (!linked) {
		SetNotice(g_q, "Link your league account to play ranked.", true);
		return;
	}
	if (!SteamOwnId()) {
		SetNotice(g_q, "Steam is not available. Open Steam and try again.", true);
		return;
	}
	const int64_t ban = std::max(g_q.banUntil, me.me.queueBanUntil);
	if (ban > NowMs()) {
		g_q.banUntil = ban;
		SetNotice(g_q, "You declined or let a match go: you can't search for a while.", true);
		return;
	}
	g_q.gen++;
	g_q.phase = LeagueRankedPhase::Joining;
	g_q.busy = true;
	g_q.rejoined = false;
	g_q.since = NowMs();
	g_q.notice.clear();
	LogI("league", "ranked: entering the 1v1 queue");
	SafeThread(JoinThread, g_q.gen, contentKey).detach();
}

void LeagueQueueLeave()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_q.phase != LeagueRankedPhase::Searching && g_q.phase != LeagueRankedPhase::Joining) return;
		ToIdle(g_q);
		g_q.notice.clear();
	}
	SafeThread(LeaveThread).detach();
}

void LeagueMatchAnswer(bool accept)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_q.phase != LeagueRankedPhase::Found || g_q.answering || !g_q.matchId) return;
	g_q.answering = true;
	SafeThread(AnswerThread, g_q.gen, g_q.matchId, accept).detach();
}

void LeagueRankedRoomOpened(uint64_t lobby)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!lobby || g_q.wantLobby == lobby) return;
	g_q.wantLobby = lobby;
	g_q.registerAt = 0;
	LogI("room", "ranked: room %llu opened for match %lld", lobby, static_cast<long long>(g_q.matchId));
}

void LeagueRankedRetryRoom()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_q.nextPoll = 0;
	g_q.registerAt = 0;
}

void LeagueMatchAbandon(bool automatic)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if ((g_q.phase != LeagueRankedPhase::Ready && g_q.phase != LeagueRankedPhase::Playing) || g_q.abandoning || !g_q.matchId) return;
	if (g_q.ticketLoaded && !LeagueTicketAbandonable(g_q.ticket)) return;
	g_q.abandoning = true;
	LogI("league", "ranked: calling off match %lld%s", static_cast<long long>(g_q.matchId), automatic ? " (its room failed)" : "");
	SafeThread(AbandonThread, g_q.gen, g_q.matchId, automatic).detach();
}

void LeagueRankedInRoom()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if ((g_q.phase != LeagueRankedPhase::Ready && g_q.phase != LeagueRankedPhase::Playing) || !g_q.matchId) return;
	const DWORD now = GetTickCount();
	if (g_q.presenceAt && now - g_q.presenceAt < kPresenceMs) return;
	g_q.presenceAt = now;
	SafeThread(PresenceThread, g_q.matchId, false).detach();
}

void LeagueRankedResultSeen()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_result.view.state = LeagueResultState::None;
	g_result.view.serial++;
}

void LeagueRankedClearNotice()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_q.notice.clear();
}

void LeagueBadgeChoose(const std::string &kind, const std::string &id)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_badge.busy) return;
		g_badge.busy = true;
		g_badge.error.clear();
	}
	SafeThread(BadgeThread, kind, id).detach();
}
