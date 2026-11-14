// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "net/leagueclient_tourney.h"
#include "addr/state.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/log.h"
#include "core/tournament.h"
#include "net/leagueclient_internal.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kListsKeepMs = 30000, kMineRetryMs = 60000, kWatchMs = 3000;
const size_t kPagesKept = 16;

struct Lists {
	bool loaded = false, loading = false, failed = false;
	DWORD at = 0;
	std::vector<TourneySummary> open, live, finished;
};

struct Page {
	bool loaded = false, loading = false, failed = false, missing = false;
	DWORD at = 0, used = 0;
	TourneyDetail detail;
};

struct Mine {
	bool loaded = false, loading = false, tried = false;
	DWORD at = 0, watched = 0;
	std::string steamId;
	TourneyMine mine;
};

std::mutex g_mx;
Lists g_lists;
std::map<std::string, Page> g_pages;
Mine g_mine;
std::set<std::pair<int, int64_t>> g_busy;
std::deque<TourneyOpResult> g_results;
std::deque<TourneyNotice> g_notices;
TourneyTold g_told;

const char *OpName(TourneyOp op)
{
	switch (op) {
	case TourneyOp::SignUp: return "sign-up";
	case TourneyOp::Withdraw: return "withdrawal";
	case TourneyOp::CheckIn: return "check-in";
	case TourneyOp::Claim: return "room claim";
	case TourneyOp::Register: return "room registration";
	case TourneyOp::Presence: return "presence";
	case TourneyOp::NoShow: return "no-show claim";
	default: return "dispute";
	}
}

void FetchLists()
{
	const HttpReply r = Http("GET", "/v1/tournaments", "", false);
	Json j;
	std::vector<TourneySummary> all;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseList(j, all);
	if (!ok) LogW("league", "could not read the tournaments (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_lists.loading = false;
	g_lists.at = GetTickCount();
	g_lists.failed = !ok;
	if (!ok) return;
	g_lists.loaded = true;
	g_lists.open.clear();
	g_lists.live.clear();
	g_lists.finished.clear();
	for (const TourneySummary &t : all) {
		if (t.status == TourneyStatus::Signup || t.status == TourneyStatus::Checkin) g_lists.open.push_back(t);
		else if (t.status == TourneyStatus::Running) g_lists.live.push_back(t);
		else if (t.status == TourneyStatus::Finished) g_lists.finished.push_back(t);
	}
	// The server sends the latest first; the open ones read better soonest first.
	std::reverse(g_lists.open.begin(), g_lists.open.end());
}

void FetchPage(std::string slug)
{
	const HttpReply r = Http("GET", "/v1/tournaments/" + UrlEncode(slug), "", false);
	Json j;
	TourneyDetail d;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseDetail(j, d);
	if (!ok && r.status != 404) LogW("league", "could not read the tournament %s (%s)", slug.c_str(), r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	Page &p = g_pages[slug];
	p.loading = false;
	p.at = GetTickCount();
	p.failed = !ok;
	p.missing = r.status == 404;
	if (!ok) return;
	p.loaded = true;
	p.detail = std::move(d);
}

// Under g_mx.
void Notify(const TourneyMine &mine)
{
	for (const TourneyNotice &n : TourneyNewNotices(mine, g_told)) {
		LogI("league", "tournament notice: %s, %s", n.kind == TourneyNotice::MatchReady ? "match ready" : n.kind == TourneyNotice::CheckinOpen ? "check-in open" : "over",
			n.name.c_str());
		g_notices.push_back(n);
	}
}

void FetchMine()
{
	const std::string steamId = AccountSteamId();
	const HttpReply r = Http("GET", "/v1/me/tournaments", "", true);
	Json j;
	TourneyMine mine;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseMine(j, mine);
	if (!ok && r.failure != "not linked" && r.status != 401) LogW("league", "could not read the player's tournaments (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_mine.loading = false;
	g_mine.at = GetTickCount();
	if (!ok) return;
	if (g_mine.steamId != steamId) {
		g_told = TourneyTold();
		g_mine.steamId = steamId;
	}
	if (g_mine.loaded && g_mine.mine.next.match != mine.next.match && mine.next.present) LogI("league", "next tournament match: %lld", static_cast<long long>(mine.next.match));
	g_mine.loaded = true;
	g_mine.mine = mine;
	Notify(mine);
}

// Under g_mx.
void StartMine()
{
	if (g_mine.loading) return;
	g_mine.loading = true;
	g_mine.tried = true;
	SafeThread(FetchMine).detach();
}

void Run(TourneyOp op, int64_t id, std::string method, std::string path, std::string body)
{
	const HttpReply r = Http(method.c_str(), path, body, true);
	TourneyOpResult out;
	out.op = op;
	out.id = id;
	out.status = r.status;
	out.ok = r.Ok();
	Json j;
	const bool parsed = !r.body.empty() && Json::Parse(r.body, j);
	if (!out.ok) {
		out.error = r.failure == "not linked" ? "not_linked" : parsed ? j.GetString("error") : std::string();
		out.message = parsed ? j.GetString("message") : r.failure;
		out.until = parsed ? static_cast<int64_t>(j.GetNumber("until")) : 0;
		out.contentName = parsed ? j.GetString("contentName") : std::string();
		if (r.status == 0 || r.status >= 500) LogW("league", "tournament %s failed (%s)", OpName(op), r.Describe().c_str());
		else LogI("league", "tournament %s refused (%s)", OpName(op), r.Describe().c_str());
	}
	else if (op == TourneyOp::Claim && !(parsed && TourneyParseClaim(j, out.claim))) {
		out.ok = false;
		out.error = "bad_answer";
		LogW("league", "the room claim of match %lld had no usable answer", static_cast<long long>(id));
	}
	else if (op == TourneyOp::SignUp && parsed) {
		const Json *entry = j.Get("entry");
		out.waitlist = entry && entry->GetString("status") == "waitlist";
	}
	if (out.ok && op != TourneyOp::Presence) LogI("league", "tournament %s done (%lld)", OpName(op), static_cast<long long>(id));
	TourneyNext next;
	const bool fresh = out.ok && op == TourneyOp::Presence && parsed && TourneyParseNext(j, next);
	std::lock_guard<std::mutex> lock(g_mx);
	g_busy.erase({static_cast<int>(op), id});
	if (fresh && g_mine.mine.next.match == next.match) g_mine.mine.next = next;
	if (op != TourneyOp::Presence) g_results.push_back(out);
	// What the player did changes their tournaments and the pages: they are read again.
	if (op != TourneyOp::Presence && op != TourneyOp::Claim && op != TourneyOp::Register) {
		StartMine();
		g_lists.at = 0;
		for (auto &p : g_pages) p.second.at = 0;
	}
}

void Start(TourneyOp op, int64_t id, const char *method, const std::string &path, const std::string &body = std::string())
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!g_busy.insert({static_cast<int>(op), id}).second) return;
	}
	SafeThread(Run, op, id, std::string(method), path, body).detach();
}

std::string MatchPath(int64_t match, const char *what)
{
	return "/v1/tournaments/matches/" + std::to_string(match) + what;
}

}

namespace leagueclient_impl {

void TourneyTick()
{
	std::string token, steamId;
	if (!AccountFor(token, steamId)) return;
	// Never during a game: the poll waits for the menus.
	if (*reinterpret_cast<void *const *>(addr::kGame) != nullptr) return;
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_mine.loading) return;
	const DWORD now = GetTickCount();
	const bool tab = g_mine.watched && now - g_mine.watched < kWatchMs;
	if (!g_mine.tried) {
		StartMine();
		return;
	}
	const bool active = g_mine.loaded && TourneyHasActiveEntry(g_mine.mine);
	DWORD every = static_cast<DWORD>(TourneyPollEveryMs(active, tab));
	if (!g_mine.loaded && !every) every = kMineRetryMs;
	if (every && now - g_mine.at >= every) StartMine();
}

}

TourneyListsView LeagueTourneyLists()
{
	std::lock_guard<std::mutex> lock(g_mx);
	TourneyListsView v;
	v.loaded = g_lists.loaded;
	v.loading = g_lists.loading;
	v.failed = g_lists.failed;
	v.open = g_lists.open;
	v.live = g_lists.live;
	v.finished = g_lists.finished;
	return v;
}

void LeagueTourneyListsRefresh(bool force)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_lists.loading) return;
	if (!force && g_lists.at && GetTickCount() - g_lists.at < kListsKeepMs) return;
	g_lists.loading = true;
	SafeThread(FetchLists).detach();
}

TourneyPageView LeagueTourneyPage(const std::string &slug, int maxAgeMs)
{
	TourneyPageView v;
	if (slug.empty()) return v;
	std::lock_guard<std::mutex> lock(g_mx);
	const DWORD now = GetTickCount();
	if (!g_pages.count(slug) && g_pages.size() >= kPagesKept) {
		auto oldest = g_pages.begin();
		for (auto it = g_pages.begin(); it != g_pages.end(); ++it) {
			if (!it->second.loading && it->second.used < oldest->second.used) oldest = it;
		}
		if (!oldest->second.loading) g_pages.erase(oldest);
	}
	Page &p = g_pages[slug];
	p.used = now;
	if (!p.loading && (!p.at || now - p.at >= static_cast<DWORD>(maxAgeMs))) {
		p.loading = true;
		SafeThread(FetchPage, slug).detach();
	}
	v.loaded = p.loaded;
	v.loading = p.loading;
	v.failed = p.failed;
	v.missing = p.missing;
	v.detail = p.detail;
	return v;
}

TourneyMineView LeagueTourneyMine()
{
	TourneyMineView v;
	std::string token, steamId;
	if (!AccountFor(token, steamId)) return v;
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_mine.steamId != steamId) return v;
	v.loaded = g_mine.loaded;
	v.loading = g_mine.loading;
	v.mine = g_mine.mine;
	return v;
}

void LeagueTourneyMineRefresh()
{
	std::lock_guard<std::mutex> lock(g_mx);
	StartMine();
}

void LeagueTourneyWatching()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_mine.watched = GetTickCount();
}

void LeagueTourneySignUp(int64_t tournament)
{
	Start(TourneyOp::SignUp, tournament, "POST", "/v1/tournaments/" + std::to_string(tournament) + "/entries");
}

void LeagueTourneyWithdraw(int64_t tournament)
{
	Start(TourneyOp::Withdraw, tournament, "DELETE", "/v1/tournaments/" + std::to_string(tournament) + "/entries/me");
}

void LeagueTourneyCheckIn(int64_t tournament)
{
	Json body = Json::MakeObject();
	body.Set("contentKey", Json::MakeString(SteamOwnGameplayKey()));
	Start(TourneyOp::CheckIn, tournament, "POST", "/v1/tournaments/" + std::to_string(tournament) + "/checkin", body.Dump());
}

void LeagueTourneyClaimRoom(int64_t match)
{
	Start(TourneyOp::Claim, match, "POST", MatchPath(match, "/room/claim"));
}

void LeagueTourneyRegisterRoom(int64_t match, uint64_t lobby, uint64_t specLobby)
{
	Json body = Json::MakeObject();
	body.Set("lobby", Json::MakeString(std::to_string(lobby)));
	if (specLobby) body.Set("specLobby", Json::MakeString(std::to_string(specLobby)));
	Start(TourneyOp::Register, match, "PUT", MatchPath(match, "/room"), body.Dump());
}

void LeagueTourneyPresence(int64_t match, bool started)
{
	Json body = Json::MakeObject();
	if (started) body.Set("started", Json::MakeBool(true));
	Start(TourneyOp::Presence, match, "POST", MatchPath(match, "/presence"), body.Dump());
}

void LeagueTourneyClaimNoShow(int64_t match)
{
	Start(TourneyOp::NoShow, match, "POST", MatchPath(match, "/claim-noshow"));
}

void LeagueTourneyDispute(int64_t match, const std::string &text)
{
	Json body = Json::MakeObject();
	body.Set("text", Json::MakeString(text.substr(0, 500)));
	Start(TourneyOp::Dispute, match, "POST", MatchPath(match, "/dispute"), body.Dump());
}

bool LeagueTourneyBusy(TourneyOp op, int64_t id)
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_busy.count({static_cast<int>(op), id}) != 0;
}

bool LeagueTourneyTakeResult(TourneyOpResult &out)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_results.empty()) return false;
	out = g_results.front();
	g_results.pop_front();
	return true;
}

bool LeagueTourneyTakeNotice(TourneyNotice &out)
{
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_notices.empty()) return false;
	out = g_notices.front();
	g_notices.pop_front();
	return true;
}
