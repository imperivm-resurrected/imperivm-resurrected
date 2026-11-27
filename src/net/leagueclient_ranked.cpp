// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "net/leagueclient.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/modsleague.h"
#include "net/leagueclient_internal.h"
#include "net/patrons.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kQueuePollMs = 5000, kMatchPollMs = 3000, kPlayingPollMs = 10000, kResultPollMs = 10000, kRetryMs = 6000, kPresenceMs = 30000;
const DWORD kSeasonKeepMs = 10 * 60 * 1000, kSeasonRetryMs = 60 * 1000, kBoardKeepMs = 60 * 1000, kRecoverAfterMs = 3000;
const DWORD kRecentKeepMs = 60 * 1000, kRecentRetryMs = 20 * 1000, kStatsKeepMs = 15 * 1000;
// The most the queue and the match wait between polls after failures in a row; the start of a game and the room are told again up to
// these; the record of two players, after a failure, is asked again from a minute up to this.
const DWORD kPollCapMs = 60 * 1000, kResultCapMs = 5 * 60 * 1000, kStartedCapMs = 5 * 60 * 1000, kRegisterCapMs = 60 * 1000, kHeadToHeadCapMs = 30 * 60 * 1000;
const int kHubRows = 10, kAroundRows = 5;
// A game only one side reported is settled by the server 15 minutes after the report; the screen stops asking a little later.
const int64_t kAwaitMaxMs = 20 * 60 * 1000;
// How far ahead of this computer's clock the server's is, from the last answer that said its time.
std::atomic<int64_t> g_clockOffset{0};

struct Queue {
	LeagueRankedPhase phase = LeagueRankedPhase::Idle;
	// The queue of the search or the match ("1v1", "2v2"), and whether it is the leader of this player's duo who searches.
	std::string queue = "1v1";
	bool partnerSearch = false;
	// A request of the queue or of the match is on its way: one at a time.
	bool busy = false;
	bool recovered = false;
	DWORD startedAt = 0, nextPoll = 0;
	// Polls of the queue or the match that failed in a row; when the partner of a duo last asked whether the leader searches.
	int fails = 0;
	DWORD idleAt = 0;
	// Changes whenever the player starts or drops a search or a match: answers to an older one are dropped.
	unsigned gen = 1;
	int64_t since = 0;
	int searching = 0, estimate = 0;
	// The Glory of the opponents the server takes now.
	LeagueSearchWindow window;
	LeagueQueueAnswer found;
	int64_t matchId = 0;
	bool ticketLoaded = false;
	LeagueMatchTicket ticket;
	bool answering = false;
	// The host's room: the one to register, the one the server has, and when to try again.
	uint64_t wantLobby = 0, roomLobby = 0;
	bool roomRegistered = false, registering = false;
	DWORD registerAt = 0;
	// Tries that failed in a row; a refusal that will not change stops them until the room is opened or retried again.
	int registerFails = 0;
	bool registerStopped = false;
	int64_t banUntil = 0;
	std::string banKind, banReason;
	std::string notice;
	bool noticeBad = false;
	std::string contentKey, contentName;
	unsigned wrongContent = 0;
	// When the notice was set: a good one (a match called off with no harm) fades after a minute; a bad one stays until it is resolved.
	DWORD noticeAt = 0;
	unsigned foundSerial = 0;
	bool rejoined = false;
	bool abandoning = false;
	int64_t calledOff = 0;
	// When the next "I am in the room" is due.
	DWORD presenceNext = 0;
	// The match whose start the server is told of: told (or given up on a refusal that will not change), on its way, the failures in a
	// row and when to try again.
	int64_t startedId = 0;
	bool startedDone = false, startedBusy = false;
	int startedFails = 0;
	DWORD startedRetryAt = 0;
	// The match's game is being played here: presence said it started, and the match is not polled until it ends.
	bool inGame = false;
	// Ready or not as last told to the server for the match readyFor (-1: not told, or to tell again).
	int readyTold = -1;
	// The staging server offers its practice bot to this search; one was asked for.
	bool practiceBot = false, botAsked = false;
	int64_t readyFor = 0;
};

struct Board {
	bool loading = false, loaded = false, failed = false;
	DWORD at = 0;
	// The queue the first ten and the page around the player are of.
	std::string queue;
	LeagueBoard top, around;
	bool pageLoading = false, pageLoaded = false;
	unsigned pageGen = 0;
	LeagueBoard page;
	LeagueBoardQuery pageQuery;
	std::string pageError;
};

struct Season {
	bool loading = false, loaded = false, none = false, failed = false;
	DWORD at = 0;
	// The language of the season read, and of the last answer, a 404 or a failure too.
	std::string lang, asked;
	LeagueSeason season;
};

struct Result {
	LeagueRankedOutcome view;
	DWORD nextPoll = 0;
	int64_t since = 0;
	bool polling = false;
	int fails = 0;
};

// The player's ranked games as the server lists them: the hub's latest and a page of the full history.
struct History {
	bool recentLoading = false, recentLoaded = false, recentFailed = false;
	DWORD recentAt = 0;
	// The queues the latest games and the page are of.
	std::string recentQueue, pageQueue;
	std::vector<LeagueHistoryGame> recent;
	bool pageLoading = false, pageLoaded = false;
	unsigned pageGen = 0;
	int64_t pageBefore = 0;
	LeagueHistoryPage page;
	std::string pageError;
	unsigned detailGen = 0;
	bool detailLoading = false, detailLoaded = false;
	int64_t detailId = 0;
	LeagueMatchDetail detail;
	std::string detailError;
};

struct Stats {
	bool loading = false, loaded = false;
	DWORD at = 0;
	LeagueStats stats;
};

struct Dispute {
	bool busy = false;
	std::vector<int64_t> sent;
	std::string notice;
	bool bad = false;
	unsigned serial = 0;
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
History g_history;
Stats g_stats;
Dispute g_dispute;
Badge g_badge;
// The queue the hub shows and searches.
std::string g_sel = "1v1";

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
	const int64_t sent = NowMs();
	a.r = Http(method, path, body, auth);
	const int64_t received = NowMs();
	a.parsed = !a.r.body.empty() && Json::Parse(a.r.body, a.j);
	if (!a.r.Ok() && a.parsed) a.error = LeagueParseError(a.j);
	a.error.retryAfter = std::max(a.error.retryAfter, a.r.retryAfterSec);
	// The queue, the match and the stats say the server's time: the countdowns follow its clock, not this computer's.
	if (a.parsed && a.j.type == Json::Object) {
		const int64_t serverNow = a.j.GetInt64("serverNow");
		if (serverNow > 0) {
			const int64_t offset = LeagueClockOffsetMs(serverNow, sent, received);
			const int64_t before = g_clockOffset.exchange(offset);
			if (std::abs(offset - before) > 5000) LogI("league", "the server's clock is %lld s ahead of this computer's", static_cast<long long>(offset / 1000));
		}
	}
	return a;
}

std::string MatchPath(int64_t id, const char *tail = "")
{
	return "/v1/league/matches/" + std::to_string(static_cast<long long>(id)) + tail;
}

// The text for a request that failed without an answer of the server, or with one it did not expect.
std::string FailureNotice(const Answer &a)
{
	if (!a.r.failure.empty() && a.r.failure == "not linked") return "Link your league account to play ranked.";
	if (a.r.status == 401) return "The league no longer recognises this computer. Link your account again.";
	return FailureText(a.r);
}

// After `fails` polls in a row the server did not answer: longer each time from the usual retry, or as long as the server asks when
// polled too often (429), quietly.
DWORD RetryIn(const Answer &a, int fails, DWORD normalMs)
{
	return RetryDelayMs(a.r, fails, normalMs, kRetryMs, kPollCapMs);
}

// Why the queue is locked, in the player's language: a lock of the league's team says the team's reason.
std::string BanNotice(const std::string &kind, const std::string &reason)
{
	if (kind == "staff" && !reason.empty()) return TrFmt("The league's team has locked your search: %s", {reason});
	return Tr(LeagueBanText(kind));
}

// Under g_mx: back to not searching, forgetting the match.
void ToIdle(Queue &q)
{
	q.phase = LeagueRankedPhase::Idle;
	q.practiceBot = q.botAsked = false;
	q.matchId = 0;
	q.ticketLoaded = false;
	q.ticket = LeagueMatchTicket();
	q.found = LeagueQueueAnswer();
	q.answering = false;
	q.abandoning = false;
	q.wantLobby = q.roomLobby = 0;
	q.roomRegistered = q.registering = false;
	q.registerFails = 0;
	q.registerStopped = false;
	q.presenceNext = 0;
	q.inGame = false;
	q.window = LeagueSearchWindow();
	q.partnerSearch = false;
	q.gen++;
}

void SetNotice(Queue &q, const std::string &text, bool bad)
{
	q.notice = text;
	q.noticeBad = bad;
	q.noticeAt = GetTickCount();
}

// The ranked matches this computer reported the game of (the last 20, kept across restarts): the queue never follows one of them again,
// even while the server still waits to settle it, so no room nor second game is made for it.
std::vector<int64_t> g_reported;
bool g_reportedLoaded = false;

std::string ReportedPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string p = path;
	return p.substr(0, p.find_last_of('\\') + 1) + "imperivm-resurrected\\league\\ranked-reported.txt";
}

// Under g_mx.
void LoadReported()
{
	if (g_reportedLoaded) return;
	g_reportedLoaded = true;
	FILE *f = nullptr;
	if (fopen_s(&f, ReportedPath().c_str(), "rb") != 0 || !f) return;
	long long id = 0;
	while (fscanf_s(f, "%lld", &id) == 1) {
		if (id > 0) g_reported.push_back(id);
	}
	fclose(f);
}

// Under g_mx.
bool ReportedHere(int64_t matchId)
{
	LoadReported();
	return matchId && std::find(g_reported.begin(), g_reported.end(), matchId) != g_reported.end();
}

// Under g_mx.
void NoteReported(int64_t matchId)
{
	if (!matchId || ReportedHere(matchId)) return;
	g_reported.push_back(matchId);
	if (g_reported.size() > 20) g_reported.erase(g_reported.begin(), g_reported.end() - 20);
	const std::string path = ReportedPath(), dir = path.substr(0, path.find_last_of('\\'));
	CreateDirectoryA(dir.substr(0, dir.find_last_of('\\')).c_str(), nullptr);
	CreateDirectoryA(dir.c_str(), nullptr);
	FILE *f = nullptr;
	if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) {
		LogW("league", "ranked: could not note the reported match %lld", static_cast<long long>(matchId));
		return;
	}
	for (int64_t id : g_reported) fprintf(f, "%lld\n", static_cast<long long>(id));
	fclose(f);
}

// Under g_mx: a match whose game started settled without this computer's report (its game closed on the player, who did not go back): the
// result screen shows it from the ticket, as the league decided it.
void ResultFromTicket(const LeagueMatchTicket &t)
{
	const std::string own = OwnId();
	const LeaguePlayerCard *mine = LeagueTicketPlayer(t, own, false);
	const int side = mine ? mine->team : 0;
	const bool decided = t.outcome == "a" || t.outcome == "b";
	LeagueRankedGame g;
	g.matchId = t.id;
	g.at = NowMs();
	g.map = t.map.name;
	const bool team = LeagueQueueIsTeam(t.queue) || !t.teams.empty();
	g.queue = team ? kLeagueQueueTeam : kLeagueQueueDuel;
	g.won = decided && side == (t.outcome == "a" ? 1 : 2);
	// Settled without this player's report: their game closed and the league took them as gone.
	g.left = !g.won && (decided || t.outcome == "abandoned");
	if (team) {
		const std::vector<LeagueMatchTeam> sides = LeagueTicketTeams(t);
		if (const LeaguePlayerCard *partner = LeagueTeamPartner(sides, own)) {
			g.partner = partner->name;
			g.partnerId = partner->steamId;
		}
		for (const LeaguePlayerCard &o : LeagueTeamOpponents(sides, own)) g.opponents.push_back({o.steamId, o.name});
		if (!g.opponents.empty()) {
			g.opponentId = g.opponents.front().first;
			g.opponent = g.opponents.front().second;
		}
	}
	else if (const LeaguePlayerCard *other = LeagueTicketPlayer(t, own, true)) {
		g.opponent = other->name;
		g.opponentId = other->steamId;
		g.opponents.push_back({other->steamId, other->name});
	}
	if (t.ranked.present) {
		g.delta = t.ranked.delta;
		g.tierAfter = t.ranked.tierAfter;
		g.divisionAfter = t.ranked.divisionAfter;
	}
	g.status = t.outcome == "disputed" ? "disputed" : "settled";
	LeagueRankedOutcome &v = g_result.view;
	v.game = g;
	v.answer = LeagueReportAnswer();
	v.answer.status = g.status;
	v.answer.outcome = t.outcome;
	v.answer.ranked = t.ranked;
	v.state = LeagueResultState::Ready;
	v.serial++;
	g_result.since = NowMs();
	if (g_sel != g.queue) {
		g_sel = g.queue;
		g_board.at = 0;
		g_history.recentAt = 0;
	}
	LogI("league", "ranked: match %lld was settled without this computer's report (%s): %s", static_cast<long long>(t.id), t.outcome.c_str(),
		g.won ? "won" : g.left ? "left" : "not won");
}

// Under g_mx: what an answer of the server says of the ranked game the result screen shows.
void ApplyAnswer(int64_t matchId, const LeagueReportAnswer &answer)
{
	LeagueRankedOutcome &v = g_result.view;
	if (v.state == LeagueResultState::None || v.game.matchId != matchId) return;
	v.answer = answer;
	v.game.status = answer.status;
	if (answer.ranked.present) {
		v.game.delta = answer.ranked.delta;
		v.game.tierAfter = answer.ranked.tierAfter;
		v.game.divisionAfter = answer.ranked.divisionAfter;
	}
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
	g_season.asked = lang;
	g_season.failed = !ok && a.r.status != 404;
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

void FetchBoard(bool linked, std::string queue)
{
	const std::string base = LeagueBoardPath(queue);
	const Answer top = Call("GET", base + "&offset=0&limit=" + std::to_string(kHubRows), "", false);
	Answer around;
	if (linked) around = Call("GET", base + "&around=me&limit=" + std::to_string(kAroundRows), "", true);
	LeagueBoard t, m;
	const bool okTop = top.r.Ok() && top.parsed && LeagueParseBoard(top.j, t);
	const bool okAround = linked && around.r.Ok() && around.parsed && LeagueParseBoard(around.j, m);
	if (!okTop) LogW("league", "could not read the leaderboard (%s)", top.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_board.loading = false;
	g_board.at = GetTickCount();
	// The player picked the other queue meanwhile: its own answer is on its way.
	if (queue != g_sel) {
		g_board.at = 0;
		return;
	}
	g_board.failed = !okTop;
	if (okTop) {
		g_board.loaded = true;
		g_board.top = t;
		g_board.queue = queue;
	}
	if (okAround) g_board.around = m;
	else if (!linked || g_board.queue != queue) g_board.around = LeagueBoard();
}

void FetchPage(unsigned gen, LeagueBoardQuery q)
{
	std::string path = LeagueBoardPath(q.queue) + "&limit=" + std::to_string(q.limit);
	path += q.aroundMe ? "&around=me" : "&offset=" + std::to_string(q.offset);
	if (q.season >= 0) path += "&season=" + std::to_string(q.season);
	if (q.name.size() >= 2) path += "&q=" + UrlEncode(q.name);
	if (q.friends) {
		std::string ids;
		for (size_t i = 0; i < q.ids.size() && i < 200; i++) ids += (i ? "," : "") + q.ids[i];
		path += "&ids=" + ids;
	}
	const Answer a = Call("GET", path, "", q.aroundMe);
	LeagueBoard b;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseBoard(a.j, b);
	if (!ok) LogW("league", "could not read a page of the leaderboard (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (gen != g_board.pageGen) return;
	g_board.pageLoading = false;
	if (ok) {
		g_board.pageLoaded = true;
		g_board.page = b;
		g_board.pageQuery = q;
		g_board.pageError.clear();
	}
	else g_board.pageError = FailureText(a.r);
}

void FetchRecent(std::string queue)
{
	const Answer a = Call("GET", LeagueHistoryPath(0, kLeagueRecentGames, queue), "", true);
	LeagueHistoryPage page;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseHistory(a.j, page);
	if (!ok && a.r.status != 401) LogW("league", "could not read the player's ranked games (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_history.recentLoading = false;
	g_history.recentAt = GetTickCount();
	if (queue != g_sel) {
		g_history.recentAt = 0;
		return;
	}
	g_history.recentFailed = !ok;
	if (!ok) return;
	g_history.recentLoaded = true;
	g_history.recent = page.games;
	g_history.recentQueue = queue;
}

void FetchHistory(unsigned gen, int64_t before, std::string queue)
{
	const Answer a = Call("GET", LeagueHistoryPath(before, kLeagueHistoryPage, queue), "", true);
	LeagueHistoryPage page;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseHistory(a.j, page);
	if (!ok) LogW("league", "could not read a page of the ranked history (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (gen != g_history.pageGen) return;
	g_history.pageLoading = false;
	if (ok) {
		g_history.pageLoaded = true;
		g_history.page = page;
		g_history.pageBefore = before;
		g_history.pageQueue = queue;
		g_history.pageError.clear();
	}
	else g_history.pageError = FailureText(a.r);
}

void FetchDetail(unsigned gen, int64_t id)
{
	const Answer a = Call("GET", "/v1/league/matches/" + std::to_string(id) + "/detail", "", false);
	LeagueMatchDetail d;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseMatchDetail(a.j, d);
	if (!ok) LogW("league", "could not read the page of ranked game %lld (%s)", static_cast<long long>(id), a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (gen != g_history.detailGen) return;
	g_history.detailLoading = false;
	g_history.detailLoaded = ok;
	if (ok) {
		g_history.detail = d;
		g_history.detailError.clear();
	}
	else g_history.detailError = FailureText(a.r);
}

void FetchStats()
{
	const Answer a = Call("GET", "/v1/league/stats", "", false);
	LeagueStats s;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseStats(a.j, s);
	if (!ok && a.r.status != 404) LogD("league", "could not read who uses the queue (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_stats.loading = false;
	g_stats.at = GetTickCount();
	if (!ok) return;
	g_stats.loaded = true;
	g_stats.stats = s;
}

void DisputeThread(int64_t id, std::string text)
{
	Json body = Json::MakeObject();
	body.Set("text", Json::MakeString(text));
	const Answer a = Call("POST", MatchPath(id, "/dispute"), body.Dump(), true);
	std::lock_guard<std::mutex> lock(g_mx);
	g_dispute.busy = false;
	g_dispute.serial++;
	if (a.r.Ok()) {
		g_dispute.sent.push_back(id);
		g_dispute.notice = "Thanks: the league will review the game. The result stays as it is until then.";
		g_dispute.bad = false;
		LogI("league", "ranked: a problem with match %lld was reported", static_cast<long long>(id));
		return;
	}
	LogW("league", "ranked: the problem with match %lld was not taken (%s)", static_cast<long long>(id), a.r.Describe().c_str());
	g_dispute.bad = true;
	const std::string &code = a.error.code;
	if (code == "already_reported") {
		g_dispute.sent.push_back(id);
		g_dispute.notice = "A problem with this game was already reported.";
	}
	else if (code == "too_late") g_dispute.notice = "Problems can only be reported up to 24 hours after the game.";
	else if (a.r.status == 400) g_dispute.notice = "Say in a few words what went wrong.";
	else g_dispute.notice = FailureNotice(a);
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
	// This computer reported its game: the match is over here, whatever the server still waits for. Following it again would make its
	// room, and a second game of it, again.
	if (ReportedHere(matchId)) {
		if (g_q.phase != LeagueRankedPhase::Idle) ToIdle(g_q);
		SetNotice(g_q, "Your last ranked game is waiting for the league's result: you can search once it is settled.", false);
		LogI("league", "ranked: match %lld was reported from here; the server has not settled it yet, so it is not followed again", static_cast<long long>(matchId));
		return;
	}
	g_q.phase = LeagueRankedPhase::Ready;
	g_q.matchId = matchId;
	g_q.ticketLoaded = false;
	g_q.nextPoll = 0;
	g_q.gen++;
	LogI("league", "ranked: match %lld is still on; following it", static_cast<long long>(matchId));
}

void JoinThread(unsigned gen, std::string contentKey, std::string queue)
{
	Json body = Json::MakeObject();
	body.Set("queue", Json::MakeString(queue));
	body.Set("contentKey", Json::MakeString(contentKey));
	body.Set("mods", LeagueModsOwnJson(ModsOwnLeagueMods()));
	const Answer a = Call("POST", "/v1/league/queue", body.Dump(), true);
	LeagueQueueAnswer q;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseQueue(a.j, q);
	bool leave = false, refreshSeason = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.busy = false;
		if (gen != g_q.gen) leave = ok;
		else if (ok) {
			g_q.phase = LeagueRankedPhase::Searching;
			g_q.queue = queue;
			g_q.since = q.since ? q.since : NowMs() + g_clockOffset;
			g_q.searching = q.searching;
			g_q.estimate = q.estimateSec;
			g_q.window = q.window;
			g_q.nextPoll = GetTickCount() + kQueuePollMs;
			LogI("league", "ranked: searching %s (%d in the queue, about %d s)", queue.c_str(), q.searching, q.estimateSec);
		}
		else {
			const std::string &code = a.error.code;
			g_q.phase = LeagueRankedPhase::Idle;
			// The duo partner's lock or match is theirs: this player is told, and neither locked nor sent to it.
			if (a.error.who == "partner" && code == "banned") SetNotice(g_q, "Your partner can't search for a while: the duo can't search until then.", true);
			else if (a.error.who == "partner" && code == "in_match") SetNotice(g_q, "Your partner already has a match: search when it is over.", true);
			else if (code == "party_not_ready" && a.error.reason == "partner_busy") SetNotice(g_q, "Your partner is searching on their own: they have to stop first.", true);
			else if (code == "in_match" && a.error.matchId) ResumeMatch(a.error.matchId);
			else if (code == "banned") {
				g_q.banUntil = a.error.until;
				g_q.banKind = a.error.kind;
				g_q.banReason = a.error.reason;
				SetNotice(g_q, BanNotice(a.error.kind, a.error.reason), true);
			}
			else if (code == "new_account") {
				// A Steam account made to start over waits before the ranked queue (two weeks by default).
				const int64_t left = std::max<int64_t>(1, (a.error.until - LeagueServerNowMs() + 86399999) / 86400000);
				SetNotice(g_q, TrFmt("Your Steam account is new: the ranked queue opens for it in %s days.", {std::to_string(left)}), true);
			}
			else if (code == "wrong_content") {
				SetNotice(g_q, "This season is played with other gameplay content. Change your content profile to search.", true);
				g_q.contentKey = a.error.contentKey;
				g_q.contentName = a.error.contentName;
				g_q.wrongContent++;
			}
			else if (code == "wrong_mods") {
				SetNotice(g_q, a.error.mods.BaseGame() ? "This season is played without gameplay mods: prepare your mods to search."
													 : "This season is played with other gameplay mods: prepare your mods to search.",
					true);
				refreshSeason = true;
			}
			else if (code == "no_season") SetNotice(g_q, "No ranked season is being played now.", true);
			else if (code == "queue_closed") SetNotice(g_q, "This season does not play that queue.", true);
			else if (code == "not_leader") SetNotice(g_q, "The leader of your duo searches for both of you.", true);
			else if (code == "party_not_ready") SetNotice(g_q, "Your duo is not ready: your friend has to accept the invitation first.", true);
			else if (code == "rank_gap" && a.error.max > 0) {
				SetNotice(g_q, TrFmt("You are %s Glory apart in 2 vs 2: a duo can be at most %s apart.", {std::to_string(a.error.gap), std::to_string(a.error.max)}),
					true);
			}
			else if (code == "rank_gap") SetNotice(g_q, "You are too far apart in 2 vs 2 to play as a duo.", true);
			else if (a.r.status == 429) SetNotice(g_q, "You searched too often: wait a few seconds and search again.", false);
			else SetNotice(g_q, FailureNotice(a), true);
			LogW("league", "ranked: could not enter the queue (%s)", a.r.Describe().c_str());
		}
	}
	if (leave) LeaveThread();
	if (refreshSeason) LeagueSeasonRefresh(true);
}

// `watching`: this player is the partner of a duo and follows the leader's search, which is theirs too.
void PollQueueThread(unsigned gen, bool recovering, bool watching)
{
	const Answer a = Call("GET", "/v1/league/queue", "", true);
	LeagueQueueAnswer q;
	const bool ok = a.r.Ok() && a.parsed && LeagueParseQueue(a.j, q);
	bool rejoin = false;
	std::string contentKey, joinQueue;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_q.busy = false;
		if (gen != g_q.gen) return;
		g_q.fails = ok ? 0 : g_q.fails + 1;
		g_q.nextPoll = GetTickCount() + (ok ? kQueuePollMs : RetryIn(a, g_q.fails, kQueuePollMs));
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
			// A search does not outlive the game: closed while searching, the player is taken out of the queue when the game comes back,
			// instead of finding it still searching. A match already found or played is still resumed (Found, Playing).
			if (recovering && !watching) {
				LogI("league", "ranked: a search left from before the game was closed is cancelled");
				ToIdle(g_q);
				SafeThread(LeaveThread).detach();
				return;
			}
			if (watching && g_q.phase == LeagueRankedPhase::Idle) {
				g_q.partnerSearch = true;
				g_q.notice.clear();
				LogI("league", "ranked: the leader of the duo is searching 2v2");
			}
			g_q.phase = LeagueRankedPhase::Searching;
			if (!q.queue.empty()) g_q.queue = LeagueQueueId(q.queue);
			g_q.since = q.since ? q.since : g_q.since;
			g_q.searching = q.searching;
			g_q.estimate = q.estimateSec;
			g_q.practiceBot = q.practiceBot;
			if (q.window.present) g_q.window = q.window;
			g_q.rejoined = false;
			break;
		case LeagueQueueStatus::Found:
			if (q.matchId != g_q.matchId || g_q.phase == LeagueRankedPhase::Searching || g_q.phase == LeagueRankedPhase::Idle) {
				g_q.found = q;
				g_q.matchId = q.matchId;
				g_q.ticketLoaded = false;
				g_q.answering = false;
				g_q.phase = q.accepted ? LeagueRankedPhase::Accepted : LeagueRankedPhase::Found;
				if (!q.queue.empty()) g_q.queue = LeagueQueueId(q.queue);
				else if (!q.teams.empty()) g_q.queue = kLeagueQueueTeam;
				g_q.foundSerial++;
				g_q.notice.clear();
				if (q.teams.empty()) LogI("league", "ranked: match %lld found against %s", static_cast<long long>(q.matchId), q.opponent.name.c_str());
				else LogI("league", "ranked: 2v2 match %lld found", static_cast<long long>(q.matchId));
			}
			g_q.nextPoll = GetTickCount();
			break;
		case LeagueQueueStatus::Idle:
			if (q.matchId) {
				ResumeMatch(q.matchId);
				break;
			}
			// The leader stopped the search, or the duo is over: the partner follows quietly.
			if (g_q.partnerSearch) {
				if (g_q.phase == LeagueRankedPhase::Searching) {
					ToIdle(g_q);
					LogI("league", "ranked: the leader of the duo is no longer searching");
				}
				break;
			}
			if (watching) break;
			if (g_q.phase == LeagueRankedPhase::Searching && !g_q.rejoined) {
				// The server drops whoever stops polling for 30 s (a long load, a sleeping computer): enter again once, quietly.
				g_q.rejoined = true;
				rejoin = true;
				contentKey = SteamOwnGameplayKey();
				joinQueue = g_q.queue;
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
	if (rejoin) JoinThread(gen, contentKey, joinQueue);
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
	// The room's minute to start ran out (cancelUnstarted): whoever was not ready lost the match.
	if (t.outcome == "timeout" && t.abandonedBy == "system") {
		const std::string me = OwnId();
		const LeaguePlayerCard *mine = LeagueTicketPlayer(t, me, false);
		bool anyReady = false;
		for (const LeaguePlayerCard &p : t.players) anyReady = anyReady || p.ready;
		const bool meReady = mine && mine->ready;
		LogI("league", "ranked: match %lld called off: its room did not start in a minute (%s)", static_cast<long long>(t.id),
			meReady ? "this player was ready" : anyReady ? "this player was not ready" : "nobody was ready");
		ToIdle(g_q);
		g_q.calledOff = t.id;
		if (meReady) SetNotice(g_q, "The match was called off: not everyone was ready in time. It doesn't count against you.", false);
		else if (anyReady) SetNotice(g_q, "You weren't ready in time: the match was called off, you lose 10 Glory and can't search for a few minutes.", true);
		else SetNotice(g_q, "Nobody was ready in time: the match was called off and you can't search for a few minutes.", true);
		return;
	}
	const std::string me = OwnId();
	const LeaguePlayerCard *mine = LeagueTicketPlayer(t, me, false);
	const bool accepted = mine && mine->accepted;
	const bool declinedByOther = t.outcome == "declined";
	LogI("league", "ranked: match %lld called off (%s)", static_cast<long long>(t.id), t.outcome.c_str());
	if (LeagueQueueIsTeam(t.queue) || !t.teams.empty()) {
		// A duo whose partner did not accept leaves the queue with them; the others go back to its head (the partner of a duo follows
		// its leader there).
		const std::vector<LeagueMatchTeam> teams = LeagueTicketTeams(t);
		const LeaguePlayerCard *partner = LeagueTeamPartner(teams, me);
		const LeaguePlayerCard *partnerCard = partner ? LeagueTicketCard(t, partner->steamId) : nullptr;
		const bool duo = partner && partner->duo;
		const bool partnerLate = duo && partnerCard && !partnerCard->accepted;
		const bool follow = g_q.partnerSearch;
		ToIdle(g_q);
		if (!accepted) {
			SetNotice(g_q, "You did not accept in time.", true);
			return;
		}
		if (partnerLate) {
			SetNotice(g_q, TrFmt("%s did not accept.", {partner->name.empty() ? Tr("Your partner") : partner->name}), false);
			return;
		}
		g_q.phase = LeagueRankedPhase::Searching;
		g_q.queue = kLeagueQueueTeam;
		g_q.partnerSearch = follow;
		g_q.nextPoll = 0;
		SetNotice(g_q, declinedByOther ? "A player declined. Still searching."
									   : "A player did not accept. Still searching.",
			false);
		return;
	}
	if (declinedByOther || accepted) {
		ToIdle(g_q);
		g_q.phase = LeagueRankedPhase::Searching;
		g_q.nextPoll = 0;
		SetNotice(g_q, declinedByOther ? "Your opponent declined. Still searching."
									   : "Your opponent did not accept. Still searching.",
			false);
		return;
	}
	ToIdle(g_q);
	SetNotice(g_q, "You did not accept in time.", true);
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
		g_q.fails = ok ? 0 : g_q.fails + 1;
		g_q.nextPoll = GetTickCount() + (ok ? kMatchPollMs : RetryIn(a, g_q.fails, kMatchPollMs));
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
			// Its game was played (here or not) and this computer never reported it: the result is shown all the same, and the card of a
			// game still on goes.
			if ((t.startedAt || g_q.startedId == id) && !ReportedHere(id) && g_result.view.game.matchId != id) ResultFromTicket(t);
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
		g_q.banUntil = a.parsed ? a.j.GetInt64("queueBanUntil") : 0;
		g_q.banKind = "declined";
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
		g_q.registerFails++;
		// Not the host, or a match no longer ready: the match's poll tells what became of it.
		g_q.registerStopped = !LeagueRetriable(a.r.status);
		g_q.registerAt = GetTickCount() + RetryDelayMs(a.r, g_q.registerFails, 0, 3000, kRegisterCapMs);
		LogW("league", "ranked: the server did not take the room (%s)%s", a.r.Describe().c_str(), g_q.registerStopped ? "; not asked again" : "");
		return;
	}
	g_q.registerFails = 0;
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
		if (a.parsed && a.j.GetNumber("queueBanUntil") > 0) {
			g_q.banUntil = a.j.GetInt64("queueBanUntil");
			g_q.banKind = "declined";
		}
		LogI("league", "ranked: match %lld called off%s", static_cast<long long>(id), automatic ? " after its room failed" : "");
	}
	LeagueMeRefresh(true);
}

// `ready`: -1 says nothing of it, 0 not ready, 1 ready in the match room (the room's minute to start goes by it, cancelUnstarted).
void PresenceThread(int64_t id, bool started, int ready)
{
	Json body = Json::MakeObject();
	if (started) body.Set("started", Json::MakeBool(true));
	if (ready >= 0) body.Set("ready", Json::MakeBool(ready == 1));
	const Answer a = Call("POST", MatchPath(id, "/presence"), body.Dump(), true);
	const bool ok = a.r.Ok();
	if (ok && started) LogI("league", "ranked: the server knows the game of match %lld started", static_cast<long long>(id));
	if (ok && ready >= 0) LogI("league", "ranked: the server knows this player is %s in match %lld", ready ? "ready" : "not ready", static_cast<long long>(id));
	if (!ok && ready >= 0) {
		std::lock_guard<std::mutex> lock(g_mx);
		// Told again at the next change or check.
		if (id == g_q.matchId) g_q.readyTold = -1;
	}
	if (!ok) LogD("league", "ranked: the presence in match %lld was not taken (%s)", static_cast<long long>(id), a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	const DWORD now = GetTickCount();
	if (!started) {
		// Too many requests: the next "I am in the room" waits what the server asks.
		if (a.r.status == 429 && id == g_q.matchId && g_q.presenceNext) {
			const DWORD next = now + RetryDelayMs(a.r, 1, kPresenceMs, kPresenceMs, kPresenceMs);
			if (static_cast<int>(next - g_q.presenceNext) > 0) g_q.presenceNext = next;
		}
		return;
	}
	if (g_q.startedId != id) return;
	g_q.startedBusy = false;
	// A refusal that will not change (not this player's match, an unofficial build) is not told again.
	if (ok || !LeagueRetriable(a.r.status) || a.r.failure == "not linked") {
		g_q.startedDone = true;
		if (!ok) LogW("league", "ranked: the start of match %lld was refused (%s); not told again", static_cast<long long>(id), a.r.Describe().c_str());
		return;
	}
	g_q.startedFails++;
	g_q.startedRetryAt = now + RetryDelayMs(a.r, g_q.startedFails, 0, 5000, kStartedCapMs);
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
		g_result.fails = a.r.Ok() ? 0 : g_result.fails + 1;
		g_result.nextPoll = GetTickCount() + RetryDelayMs(a.r, g_result.fails, kResultPollMs, kResultPollMs, kResultCapMs);
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
		LeagueRecentRefresh(true);
	}
}

void BadgeThread(std::string kind, std::string id)
{
	Json body = Json::MakeObject();
	body.Set("kind", Json::MakeString(kind));
	// A rank names its queue ("2v2"); a season or a trophy is a number.
	if (!id.empty()) body.Set("id", kind == "rank" ? Json::MakeString(id) : Json::MakeNumber(static_cast<double>(_strtoi64(id.c_str(), nullptr, 10))));
	const Answer a = Call("PUT", "/v1/league/me/badge", body.Dump(), true);
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_badge.busy = false;
		g_badge.error = a.r.Ok() ? std::string() : std::string(FailureText(a.r));
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
	PartyTick(linked);
	const bool member = linked && LeagueDuoOwnRole() == LeagueDuoRole::Member;
	const DWORD memberEvery = member ? static_cast<DWORD>(LeagueDuoQueuePollMs(PartyScreenOpen(), GameInFront())) : 0;
	const bool inGame = InGame();
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
			SafeThread(PollQueueThread, g_q.gen, true, false).detach();
		}
	}
	if (!g_q.busy && static_cast<int>(now - g_q.nextPoll) >= 0) {
		switch (g_q.phase) {
		case LeagueRankedPhase::Idle:
			// The partner of a duo follows the leader's search, which they did not start.
			if (member && g_q.recovered && !inGame && now - g_q.idleAt >= memberEvery) {
				g_q.busy = true;
				g_q.idleAt = now;
				SafeThread(PollQueueThread, g_q.gen, false, true).detach();
			}
			break;
		case LeagueRankedPhase::Searching:
			g_q.busy = true;
			SafeThread(PollQueueThread, g_q.gen, false, g_q.partnerSearch).detach();
			break;
		case LeagueRankedPhase::Found:
		case LeagueRankedPhase::Accepted:
		case LeagueRankedPhase::Ready:
		case LeagueRankedPhase::Playing:
			// During the game the presence with `started` is all the server needs; the report ends the match.
			if (g_q.matchId && !g_q.inGame) {
				g_q.busy = true;
				SafeThread(PollMatchThread, g_q.gen, g_q.matchId).detach();
			}
			break;
		default: break;
		}
	}
	const bool host = g_q.ticketLoaded && g_q.ticket.host == OwnId();
	if (host && g_q.wantLobby && (!g_q.roomRegistered || g_q.roomLobby != g_q.wantLobby) && !g_q.registering && !g_q.registerStopped &&
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

void RankedGameReported(const LeagueReport &report, const std::string &ownSteamId, const GameRecordFacts &facts, bool read)
{
	LeagueRankedGame g;
	g.matchId = report.matchId;
	g.at = NowMs();
	g.durationMs = report.durationMs;
	g.map = report.map.name;
	g.stats = read;
	g.killed = facts.killed;
	g.lost = facts.lost;
	g.armyMax = facts.armyMax;
	g.gold = facts.gold;
	g.bestLevel = facts.bestLevel;
	g.bestUnit = facts.bestUnit;
	g.produced = facts.produced;
	int ownTeam = 0;
	for (const LeagueReportPlayer &p : report.players) {
		if (p.steamId == ownSteamId) ownTeam = p.team;
	}
	std::lock_guard<std::mutex> lock(g_mx);
	const bool ticket = g_q.ticketLoaded && g_q.ticket.id == report.matchId;
	const bool team = ticket ? LeagueQueueIsTeam(g_q.ticket.queue) || !g_q.ticket.teams.empty() : report.players.size() >= 4;
	g.queue = team ? kLeagueQueueTeam : kLeagueQueueDuel;
	// The names the league knows the players by, from the ticket.
	auto nameOf = [&](const LeagueReportPlayer &p) {
		const LeaguePlayerCard *c = ticket ? LeagueTicketCard(g_q.ticket, p.steamId) : nullptr;
		return c && !c->name.empty() ? c->name : p.name;
	};
	for (const LeagueReportPlayer &p : report.players) {
		if (!p.civ.empty()) g.civs.push_back({p.steamId, p.civ});
		if (p.steamId == ownSteamId) {
			g.won = p.result == "win";
			g.left = p.result == "left";
		}
		else if (team && p.team == ownTeam) {
			g.partner = nameOf(p);
			g.partnerId = p.steamId;
			g.partnerLeft = p.result == "left";
		}
		else {
			if (g.opponentId.empty()) {
				g.opponent = nameOf(p);
				g.opponentId = p.steamId;
			}
			g.opponents.push_back({p.steamId, nameOf(p)});
		}
	}
	if (ticket && !g_q.ticket.map.name.empty()) g.map = g_q.ticket.map.name;
	// The result's place on the leaderboard is the queue's just played.
	if (g_sel != g.queue) {
		g_sel = g.queue;
		g_board.at = 0;
		g_history.recentAt = 0;
	}
	g_result.view.state = LeagueResultState::Sending;
	g_result.view.game = g;
	g_result.view.answer = LeagueReportAnswer();
	g_result.view.serial++;
	g_result.since = NowMs();
	g_q.inGame = false;
	NoteReported(report.matchId);
	if (g_q.matchId == report.matchId) ToIdle(g_q);
	LogI("league", "ranked: %s match %lld played (%s); waiting for the server", g.queue.c_str(), static_cast<long long>(report.matchId),
		g.won ? "won" : g.left ? "left" : "lost");
}

void RankedGameEnded(int64_t matchId)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!g_q.inGame || (matchId && matchId != g_q.matchId)) return;
	g_q.inGame = false;
	g_q.nextPoll = 0;
	LogW("league", "ranked: the game of match %lld ended without a report; following the server", static_cast<long long>(g_q.matchId));
}

bool RankedTicketOf(int64_t matchId, LeagueMatchTicket &out)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (!matchId || !g_q.ticketLoaded || g_q.ticket.id != matchId) return false;
	out = g_q.ticket;
	return true;
}

void RankedReportAnswered(const Json &report, const std::string &answer)
{
	if (report.GetString("kind") != "ranked") return;
	const int64_t matchId = report.GetInt64("matchId");
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
	LeagueRecentRefresh(true);
}

void RankedGameStarted(int64_t matchId)
{
	if (matchId <= 0) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_q.matchId == matchId) g_q.inGame = true;
		if (g_q.startedId != matchId) {
			g_q.startedId = matchId;
			g_q.startedDone = g_q.startedBusy = false;
			g_q.startedFails = 0;
			g_q.startedRetryAt = 0;
		}
		if (g_q.startedDone || g_q.startedBusy || (g_q.startedFails && static_cast<int>(GetTickCount() - g_q.startedRetryAt) < 0)) return;
		g_q.startedBusy = true;
	}
	SafeThread(PresenceThread, matchId, true, -1).detach();
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
	v.seasonFailed = g_season.failed;
	v.noSeason = g_season.loaded && g_season.none;
	v.season = g_season.season;
	v.lang = g_season.lang.empty() ? LangCode() : g_season.lang;
	v.queue = g_sel;
	v.searchQueue = g_q.queue;
	v.partnerSearch = g_q.partnerSearch;
	// What was read of the other queue is not shown: the picked one's comes.
	const bool board = g_board.queue == g_sel;
	v.boardLoaded = g_board.loaded && board;
	v.boardFailed = g_board.failed && !v.boardLoaded;
	if (board) {
		v.top = g_board.top;
		v.around = g_board.around;
	}
	v.pageLoading = g_board.pageLoading;
	v.pageLoaded = g_board.pageLoaded && LeagueQueueId(g_board.pageQuery.queue) == g_sel;
	v.page = g_board.page;
	v.pageQuery = g_board.pageQuery;
	v.pageError = g_board.pageError;
	v.phase = g_q.phase;
	v.since = g_q.since;
	v.searching = g_q.searching;
	v.estimateSec = g_q.estimate;
	v.practiceBot = g_q.practiceBot && g_q.phase == LeagueRankedPhase::Searching;
	v.window = g_q.window;
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
	v.startedHere = g_q.matchId && g_q.startedId == g_q.matchId;
	v.answering = g_q.answering;
	v.abandoning = g_q.abandoning;
	v.calledOff = g_q.calledOff;
	v.banUntil = g_q.banUntil;
	v.banKind = g_q.banKind;
	v.banReason = g_q.banReason;
	v.notice = g_q.noticeBad || GetTickCount() - g_q.noticeAt < 60000 ? g_q.notice : std::string();
	v.noticeBad = g_q.noticeBad;
	v.seasonContentKey = g_q.contentKey;
	v.seasonContentName = g_q.contentName;
	v.wrongContentSerial = g_q.wrongContent;
	v.foundSerial = g_q.foundSerial;
	v.result = g_result.view;
	const bool recent = g_history.recentQueue == g_sel;
	v.recentLoaded = g_history.recentLoaded && recent;
	v.recentFailed = g_history.recentFailed;
	if (recent) v.recent = g_history.recent;
	v.historyLoading = g_history.pageLoading;
	v.historyLoaded = g_history.pageLoaded && g_history.pageQueue == g_sel;
	v.historyBefore = g_history.pageBefore;
	v.history = g_history.page;
	v.historyError = g_history.pageError;
	v.detailLoading = g_history.detailLoading;
	v.detailLoaded = g_history.detailLoaded;
	v.detailId = g_history.detailId;
	v.detail = g_history.detail;
	v.detailError = g_history.detailError;
	v.statsLoaded = g_stats.loaded;
	v.stats = g_stats.stats;
	v.disputeBusy = g_dispute.busy;
	v.disputed = g_dispute.sent;
	v.disputeNotice = g_dispute.notice;
	v.disputeBad = g_dispute.bad;
	v.disputeSerial = g_dispute.serial;
	v.badgeBusy = g_badge.busy;
	v.badgeError = g_badge.error;
	return v;
}

void LeagueRecentRefresh(bool force)
{
	if (!LeagueLinked()) return;
	std::string queue;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_history.recentLoading) return;
		const DWORD keep = g_history.recentFailed ? kRecentRetryMs : kRecentKeepMs;
		if (!force && g_history.recentAt && (GetTickCount() - g_history.recentAt < keep || PlayerAway())) return;
		g_history.recentLoading = true;
		queue = g_sel;
	}
	SafeThread(FetchRecent, queue).detach();
}

void LeagueHistoryRequest(int64_t before)
{
	if (!LeagueLinked()) return;
	unsigned gen = 0;
	std::string queue;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		gen = ++g_history.pageGen;
		g_history.pageLoading = true;
		queue = g_sel;
	}
	SafeThread(FetchHistory, gen, std::max<int64_t>(0, before), queue).detach();
}

namespace {

const DWORD kHeadToHeadKeepMs = 15 * 60 * 1000, kHeadToHeadRetryMs = 60 * 1000;
const size_t kHeadToHeadKept = 32;

// The record of a pair, asked once while it is kept (the cards ask every frame): a failure waits longer each time, a pair the server
// does not know is a record with no games.
struct HeadToHead {
	LeagueHeadToHeadView view;
	DWORD at = 0, used = 0, wait = 0;
	int fails = 0;
};

std::mutex g_h2hMx;
std::map<std::pair<std::string, std::string>, HeadToHead> g_h2h;
std::pair<std::string, std::string> g_h2hShown;

// The server's route takes SteamIDs only: anything else would be a 404 asked for nothing.
bool SteamIdText(const std::string &s)
{
	if (s.size() != 17 || s.compare(0, 4, "7656") != 0) return false;
	for (char c : s) {
		if (c < '0' || c > '9') return false;
	}
	return true;
}

void FetchHeadToHead(std::string a, std::string b)
{
	const Answer r = Call("GET", "/v1/league/players/" + a + "/vs/" + b, "", false);
	LeagueHeadToHead h;
	const bool ok = r.r.Ok() && r.parsed && LeagueParseHeadToHead(r.j, h);
	const bool unknown = r.r.status == 404 || r.r.status == 400;
	if (!ok && !unknown) LogD("league", "could not read the record of two players (%s)", r.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_h2hMx);
	const auto it = g_h2h.find({a, b});
	if (it == g_h2h.end()) return;
	HeadToHead &e = it->second;
	e.at = GetTickCount();
	e.view.loading = false;
	// A failure keeps the record read before, if any.
	if (ok || unknown) e.view.h2h = ok ? h : LeagueHeadToHead();
	e.view.loaded = e.view.loaded || ok || unknown;
	e.fails = ok ? 0 : e.fails + 1;
	e.wait = ok ? kHeadToHeadKeepMs : RetryDelayMs(r.r, e.fails, 0, kHeadToHeadRetryMs, kHeadToHeadCapMs);
}

}

void LeagueHeadToHeadRequest(const std::string &a, const std::string &b)
{
	if (a.empty() || b.empty() || a == b || !SteamIdText(a) || !SteamIdText(b)) return;
	{
		std::lock_guard<std::mutex> lock(g_h2hMx);
		const DWORD now = GetTickCount();
		const auto key = std::make_pair(a, b);
		g_h2hShown = key;
		auto it = g_h2h.find(key);
		if (it == g_h2h.end() && g_h2h.size() >= kHeadToHeadKept) {
			auto oldest = g_h2h.end();
			for (auto i = g_h2h.begin(); i != g_h2h.end(); ++i) {
				if (!i->second.view.loading && (oldest == g_h2h.end() || static_cast<int>(i->second.used - oldest->second.used) < 0)) oldest = i;
			}
			if (oldest != g_h2h.end()) g_h2h.erase(oldest);
		}
		HeadToHead &e = g_h2h[key];
		e.used = now;
		if (e.view.loading || (e.at && now - e.at < e.wait)) return;
		e.view.a = a;
		e.view.b = b;
		e.view.loading = true;
	}
	SafeThread(FetchHeadToHead, a, b).detach();
}

LeagueHeadToHeadView LeagueHeadToHeadState()
{
	std::lock_guard<std::mutex> lock(g_h2hMx);
	const auto it = g_h2h.find(g_h2hShown);
	if (it == g_h2h.end()) return LeagueHeadToHeadView();
	LeagueHeadToHeadView v = it->second.view;
	v.a = g_h2hShown.first;
	v.b = g_h2hShown.second;
	return v;
}

void LeagueMatchDetailRequest(int64_t matchId)
{
	unsigned gen = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		gen = ++g_history.detailGen;
		g_history.detailLoading = true;
		g_history.detailLoaded = false;
		g_history.detailId = matchId;
		g_history.detailError.clear();
	}
	SafeThread(FetchDetail, gen, matchId).detach();
}

void LeagueStatsRefresh()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_stats.loading || (g_stats.at && (GetTickCount() - g_stats.at < kStatsKeepMs || PlayerAway()))) return;
		g_stats.loading = true;
	}
	SafeThread(FetchStats).detach();
}

void LeagueMatchDispute(int64_t matchId, const std::string &text)
{
	if (matchId <= 0) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_dispute.busy) return;
		g_dispute.busy = true;
	}
	LogI("league", "ranked: reporting a problem with match %lld", static_cast<long long>(matchId));
	SafeThread(DisputeThread, matchId, text).detach();
}

int64_t LeagueServerClockOffsetMs()
{
	return g_clockOffset;
}

int64_t LeagueServerNowMs()
{
	return NowMs() + g_clockOffset;
}

void LeagueSeasonRefresh(bool force)
{
	const std::string lang = LangCode();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_season.loading) return;
		const DWORD keep = g_season.loaded ? kSeasonKeepMs : kSeasonRetryMs;
		if (!force && g_season.at && GetTickCount() - g_season.at < keep && g_season.asked == lang) return;
		if (!force && g_season.at && !g_season.loaded && GetTickCount() - g_season.at < kSeasonRetryMs) return;
		g_season.loading = true;
	}
	SafeThread(FetchSeason, lang).detach();
}

void LeagueBoardRefresh(bool force)
{
	const bool linked = LeagueLinked();
	std::string queue;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_board.loading) return;
		if (!force && g_board.at && (GetTickCount() - g_board.at < kBoardKeepMs || PlayerAway())) return;
		g_board.loading = true;
		queue = g_sel;
	}
	SafeThread(FetchBoard, linked, queue).detach();
}

void LeagueBoardRequest(const LeagueBoardQuery &query)
{
	LeagueBoardQuery q = query;
	if (q.aroundMe && !LeagueLinked()) q.aroundMe = false;
	q.offset = std::max(0, q.offset);
	q.limit = std::max(1, std::min(100, q.limit));
	unsigned gen = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		gen = ++g_board.pageGen;
		g_board.pageLoading = true;
		q.queue = g_sel;
	}
	SafeThread(FetchPage, gen, q).detach();
}

void LeagueQueueJoin()
{
	const bool linked = LeagueLinked();
	const LeagueMeView me = LeagueMeState();
	const std::string contentKey = SteamOwnGameplayKey();
	const LeagueDuoRole role = LeagueDuoOwnRole();
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_q.phase != LeagueRankedPhase::Idle || g_q.busy) return;
	const std::string queue = g_sel;
	const bool team = LeagueQueueIsTeam(queue);
	if (team && role == LeagueDuoRole::Member) {
		SetNotice(g_q, "The leader of your duo searches for both of you.", true);
		return;
	}
	if (team && role == LeagueDuoRole::Waiting) {
		SetNotice(g_q, "Your duo is not ready: your friend has to accept the invitation first.", true);
		return;
	}
	if (!linked) {
		SetNotice(g_q, "Link your league account to play ranked.", true);
		return;
	}
	if (!SteamOwnId()) {
		SetNotice(g_q, "Steam is not available. Open Steam and try again.", true);
		return;
	}
	const int64_t ban = std::max(g_q.banUntil, me.me.ban.until);
	if (ban > NowMs() + g_clockOffset) {
		if (me.me.ban.until >= g_q.banUntil) {
			g_q.banKind = me.me.ban.kind;
			g_q.banReason = me.me.ban.reason;
		}
		g_q.banUntil = ban;
		SetNotice(g_q, BanNotice(g_q.banKind, g_q.banReason), true);
		return;
	}
	g_q.gen++;
	g_q.phase = LeagueRankedPhase::Joining;
	g_q.queue = queue;
	g_q.partnerSearch = false;
	g_q.busy = true;
	g_q.rejoined = false;
	g_q.since = NowMs() + g_clockOffset;
	g_q.notice.clear();
	LogI("league", "ranked: entering the %s queue%s", queue.c_str(), team && role == LeagueDuoRole::Leader ? " with the duo" : "");
	SafeThread(JoinThread, g_q.gen, contentKey, queue).detach();
}

void LeagueRankedSelectQueue(const std::string &queue)
{
	const std::string q = LeagueQueueId(queue);
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_sel == q) return;
		g_sel = q;
		g_board.at = 0;
		g_history.recentAt = 0;
		g_history.recentFailed = false;
		g_board.pageGen++;
		g_board.pageLoading = false;
		g_history.pageGen++;
		g_history.pageLoading = false;
	}
	LogI("league", "ranked: the hub shows the %s queue", q.c_str());
	LeagueBoardRefresh(true);
	LeagueRecentRefresh(true);
}

void LeagueQueueLeave()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_q.phase != LeagueRankedPhase::Searching && g_q.phase != LeagueRankedPhase::Joining) return;
		if (g_q.partnerSearch) return;
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
	g_q.registerFails = 0;
	g_q.registerStopped = false;
	LogI("room", "ranked: room %llu opened for match %lld", lobby, static_cast<long long>(g_q.matchId));
}

void LeagueRankedRetryRoom()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_q.nextPoll = 0;
	g_q.registerAt = 0;
	g_q.registerStopped = false;
}

// The player's own report of a 1v1 they give up after its game closed on them: they left, the other player won. The server takes it at once
// (ownSurrender in reports.ts): nobody gains from claiming a loss.
void SurrenderThread(unsigned gen, int64_t id, std::string body)
{
	const Answer a = Call("POST", "/v1/league/matches/report", body, true);
	std::lock_guard<std::mutex> lock(g_mx);
	g_q.abandoning = false;
	if (gen != g_q.gen || id != g_q.matchId) return;
	if (!a.r.Ok()) {
		LogW("league", "ranked: the surrender of match %lld was not taken (%s)", static_cast<long long>(id), a.r.Describe().c_str());
		SetNotice(g_q, FailureNotice(a), true);
		return;
	}
	LogI("league", "ranked: this player gave up match %lld; it counts as a loss", static_cast<long long>(id));
	NoteReported(id);
	ToIdle(g_q);
	SetNotice(g_q, "You gave up the match: it counts as a loss and your opponent wins.", true);
	g_q.nextPoll = 0;
	LeagueMeRefresh(true);
}

void LeagueRankedSurrender()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if ((g_q.phase != LeagueRankedPhase::Ready && g_q.phase != LeagueRankedPhase::Playing) || g_q.abandoning || !g_q.matchId || !g_q.ticketLoaded || !g_q.ticket.startedAt) return;
	const LeaguePlayerCard *other = LeagueTicketPlayer(g_q.ticket, OwnId(), true);
	if (LeagueQueueIsTeam(g_q.ticket.queue) || !other || other->steamId.empty()) return;
	const int64_t played = std::max<int64_t>(0, std::min<int64_t>(NowMs() + g_clockOffset - g_q.ticket.startedAt, 24LL * 3600 * 1000));
	Json body = Json::MakeObject();
	body.Set("key", Json::MakeString("surrender:" + std::to_string(g_q.matchId)));
	body.Set("kind", Json::MakeString("ranked"));
	body.Set("matchId", Json::MakeNumber(static_cast<double>(g_q.matchId)));
	Json map = Json::MakeObject();
	map.Set("name", Json::MakeString(g_q.ticket.map.name));
	body.Set("map", map);
	body.Set("contentKey", Json::MakeString(g_q.ticket.contentKey));
	body.Set("startedAt", Json::MakeNumber(static_cast<double>(g_q.ticket.startedAt)));
	body.Set("durationMs", Json::MakeNumber(static_cast<double>(played)));
	body.Set("endedBy", Json::MakeString("surrender"));
	Json players = Json::MakeArray();
	auto player = [](const std::string &id, int team, const char *result) {
		Json p = Json::MakeObject();
		p.Set("steamId", Json::MakeString(id));
		p.Set("team", Json::MakeNumber(team));
		p.Set("result", Json::MakeString(result));
		return p;
	};
	players.arr.push_back(player(OwnId(), 1, "left"));
	players.arr.push_back(player(other->steamId, 2, "win"));
	body.Set("players", players);
	body.Set("hashes", Json::MakeArray());
	g_q.abandoning = true;
	LogI("league", "ranked: giving up match %lld after %lld s of its game", static_cast<long long>(g_q.matchId), static_cast<long long>(played / 1000));
	SafeThread(SurrenderThread, g_q.gen, g_q.matchId, body.Dump()).detach();
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
	if (g_q.presenceNext && static_cast<int>(now - g_q.presenceNext) < 0) return;
	g_q.presenceNext = now + kPresenceMs;
	SafeThread(PresenceThread, g_q.matchId, false, -1).detach();
}

void LeagueRankedReady(bool ready)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if ((g_q.phase != LeagueRankedPhase::Ready && g_q.phase != LeagueRankedPhase::Playing) || !g_q.matchId) return;
	const int value = ready ? 1 : 0;
	if (g_q.readyTold == value && g_q.readyFor == g_q.matchId) return;
	g_q.readyTold = value;
	g_q.readyFor = g_q.matchId;
	SafeThread(PresenceThread, g_q.matchId, false, value).detach();
}

void BotThread()
{
	const Answer a = Call("POST", "/v1/league/queue/bot", "{}", true);
	if (a.r.Ok()) LogI("league", "ranked: the staging server's practice bot joins the search");
	else LogW("league", "ranked: the practice bot was refused (%s)", a.r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (!a.r.Ok()) g_q.botAsked = false;
	// The next poll pairs the search with it.
	else g_q.nextPoll = GetTickCount();
}

void LeagueRankedPracticeBot()
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_q.phase != LeagueRankedPhase::Searching || !g_q.practiceBot || g_q.botAsked) return;
	g_q.botAsked = true;
	SafeThread(BotThread).detach();
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
