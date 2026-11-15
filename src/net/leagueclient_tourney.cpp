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
#include "core/sha256.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "net/leagueclient_internal.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kListsKeepMs = 30000, kMineRetryMs = 60000, kWatchMs = 3000;
const size_t kPagesKept = 16;
// After a 429 the reads wait what the server asks, at least this long and at most an hour.
const DWORD kQuietMinMs = 5000, kQuietMaxMs = 3600000;

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
	// `gameAt`: the last tick seen during a game, after which a series' pick opens.
	DWORD at = 0, watched = 0, gameAt = 0;
	std::string steamId;
	TourneyMine mine;
};

struct Disputes {
	bool loaded = false, loading = false, failed = false;
	DWORD at = 0;
	std::vector<TourneyDispute> list;
};

std::mutex g_mx;
Lists g_lists;
std::map<std::string, Page> g_pages;
Mine g_mine;
std::map<int64_t, Disputes> g_disputes;
// The invitation codes the player found or made cups with, by slug and by id: their pages are read by the code and their sign-up
// sends it.
std::map<std::string, std::string> g_codes;
std::map<int64_t, std::string> g_codeIds;
// Until when the reads wait after the server said too many requests (0: they don't).
DWORD g_quietUntil = 0;
std::set<std::pair<int, int64_t>> g_busy;
TourneyUploadView g_upload;
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
	case TourneyOp::Dispute: return "dispute";
	case TourneyOp::Create: return "cup creation";
	case TourneyOp::FindCode: return "code lookup";
	case TourneyOp::Cancel: return "cancellation";
	case TourneyOp::CheckinNow: return "early check-in";
	case TourneyOp::StartNow: return "early start";
	case TourneyOp::RemoveEntry: return "entry removal";
	case TourneyOp::Edit: return "cup change";
	case TourneyOp::Pick: return "map pick";
	default: return "resolution";
	}
}

// Under g_mx.
bool Quiet()
{
	return g_quietUntil && static_cast<int>(g_quietUntil - GetTickCount()) > 0;
}

// A read the server refused with 429: the reads wait what it asks, without telling the player.
void WaitAfter(const HttpReply &r)
{
	if (r.status != 429) return;
	Json j;
	const int seconds = !r.body.empty() && Json::Parse(r.body, j) ? TourneyParseRefusal(j).retryAfter : 0;
	const DWORD wait = std::max(kQuietMinMs, std::min(kQuietMaxMs, static_cast<DWORD>(seconds) * 1000));
	LogI("league", "the league asked to wait %u s before reading the tournaments again", static_cast<unsigned>(wait / 1000));
	std::lock_guard<std::mutex> lock(g_mx);
	g_quietUntil = GetTickCount() + wait;
}

// Under g_mx.
void RememberCode(const std::string &slug, int64_t id, const std::string &code)
{
	if (code.empty()) return;
	if (!slug.empty()) g_codes[slug] = code;
	if (id) g_codeIds[id] = code;
}

void FetchLists()
{
	const HttpReply r = Http("GET", "/v1/tournaments", "", false);
	Json j;
	std::vector<TourneySummary> all;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseList(j, all);
	if (!ok && r.status != 429) LogW("league", "could not read the tournaments (%s)", r.Describe().c_str());
	WaitAfter(r);
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
	// The server sends the latest first; the open ones read better soonest first. The official cups go before the community's.
	std::reverse(g_lists.open.begin(), g_lists.open.end());
	const auto official = [](const TourneySummary &t) { return t.official; };
	std::stable_partition(g_lists.open.begin(), g_lists.open.end(), official);
	std::stable_partition(g_lists.live.begin(), g_lists.live.end(), official);
}

// A page found by its invitation code is read by it: the private cups have no other way.
void FetchPage(std::string slug, std::string code)
{
	const std::string path = code.empty() ? "/v1/tournaments/" + UrlEncode(slug) : "/v1/tournaments/code/" + UrlEncode(code);
	const HttpReply r = Http("GET", path, "", false);
	Json j;
	TourneyDetail d;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseDetail(j, d);
	if (!ok && r.status != 404 && r.status != 429) LogW("league", "could not read the tournament %s (%s)", slug.c_str(), r.Describe().c_str());
	WaitAfter(r);
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
		const char *kind = "over";
		if (n.kind == TourneyNotice::MatchReady) kind = "match ready";
		else if (n.kind == TourneyNotice::CheckinOpen) kind = "check-in open";
		else if (n.kind == TourneyNotice::PickOpen) kind = "map to pick";
		LogI("league", "tournament notice: %s, %s", kind, n.name.c_str());
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
	if (!ok && r.failure != "not linked" && r.status != 401 && r.status != 429) LogW("league", "could not read the player's tournaments (%s)", r.Describe().c_str());
	WaitAfter(r);
	std::lock_guard<std::mutex> lock(g_mx);
	g_mine.loading = false;
	g_mine.at = GetTickCount();
	if (!ok) return;
	if (g_mine.steamId != steamId) {
		g_told = TourneyTold();
		g_mine.steamId = steamId;
	}
	if (g_mine.loaded && g_mine.mine.next.match != mine.next.match && mine.next.present) LogI("league", "next tournament match: %lld", static_cast<long long>(mine.next.match));
	if (g_mine.mine.organize.activeId != mine.organize.activeId && mine.organize.activeId) {
		LogI("league", "the player organises the cup %lld", static_cast<long long>(mine.organize.activeId));
	}
	g_mine.loaded = true;
	g_mine.mine = mine;
	for (const TourneyOrganized &c : mine.organized) RememberCode(c.slug, c.id, c.inviteCode);
	Notify(mine);
}

void FetchDisputes(int64_t tournament)
{
	const HttpReply r = Http("GET", "/v1/tournaments/" + std::to_string(tournament) + "/disputes", "", true);
	Json j;
	std::vector<TourneyDispute> list;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseDisputes(j, list);
	if (!ok && r.status != 429) LogW("league", "could not read the disputes of the cup %lld (%s)", static_cast<long long>(tournament), r.Describe().c_str());
	WaitAfter(r);
	std::lock_guard<std::mutex> lock(g_mx);
	Disputes &d = g_disputes[tournament];
	d.loading = false;
	d.at = GetTickCount();
	d.failed = !ok;
	if (!ok) return;
	d.loaded = true;
	d.list = std::move(list);
}

// Under g_mx.
void StartMine()
{
	if (g_mine.loading) return;
	g_mine.loading = true;
	g_mine.tried = true;
	SafeThread(FetchMine).detach();
}

void Run(TourneyOp op, int64_t id, std::string method, std::string path, std::string body, std::string code)
{
	const HttpReply r = Http(method.c_str(), path, body, true);
	TourneyOpResult out;
	out.op = op;
	out.id = id;
	out.status = r.status;
	out.ok = r.Ok();
	Json j;
	const bool parsed = !r.body.empty() && Json::Parse(r.body, j);
	TourneyDetail found;
	TourneyCreated created;
	if (!out.ok) {
		const TourneyRefusal no = parsed ? TourneyParseRefusal(j) : TourneyRefusal();
		out.error = r.failure == "not linked" ? "not_linked" : no.code;
		if (out.error.empty() && r.status == 429) out.error = "rate_limited";
		out.message = parsed ? no.message : r.failure;
		out.until = no.until;
		out.contentName = no.contentName;
		out.gamesPlayed = no.gamesPlayed;
		out.gamesNeeded = no.gamesNeeded;
		out.checkedIn = no.checkedIn;
		out.needed = no.needed;
		out.retryAfter = no.retryAfter;
		out.slug = no.slug;
		out.requirement = no.requirement;
		out.have = no.have;
		out.need = no.need;
		out.hash = no.hash;
		out.noshowFrom = no.noshowFrom;
		if (r.status == 0 || r.status >= 500) LogW("league", "tournament %s failed (%s)", OpName(op), r.Describe().c_str());
		else LogI("league", "tournament %s refused (%s)", OpName(op), r.Describe().c_str());
	}
	else if ((op == TourneyOp::Create || op == TourneyOp::Edit) && !(parsed && TourneyParseCreated(j, created))) {
		out.ok = false;
		out.error = "bad_answer";
		LogW("league", "the league took the cup but its answer could not be read");
	}
	else if (op == TourneyOp::FindCode && !(parsed && TourneyParseDetail(j, found))) {
		out.ok = false;
		out.error = "bad_answer";
		LogW("league", "the cup of an invitation code could not be read");
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
	else if (op == TourneyOp::Pick && parsed) {
		out.map = TourneyMapFromJson(j.Get("map"));
		LogI("league", "map of the next game of match %lld chosen: %s", static_cast<long long>(id), out.map.Scenario() ? out.map.file.c_str() : out.map.name.c_str());
	}
	if (out.ok && (op == TourneyOp::Create || op == TourneyOp::Edit)) {
		out.tournament = created.id;
		out.slug = created.slug;
		out.inviteCode = created.inviteCode;
		LogI("league", "cup %s: %s (%lld)%s", op == TourneyOp::Create ? "made" : "changed", created.slug.c_str(), static_cast<long long>(created.id),
			created.inviteCode.empty() ? "" : ", private");
	}
	if (out.ok && op == TourneyOp::FindCode) {
		out.tournament = found.id;
		out.slug = found.slug;
		out.inviteCode = code;
	}
	if (out.ok && op != TourneyOp::Presence && op != TourneyOp::Create && op != TourneyOp::Edit && op != TourneyOp::FindCode) {
		LogI("league", "tournament %s done (%lld)", OpName(op), static_cast<long long>(id));
	}
	TourneyNext next;
	const bool fresh = out.ok && op == TourneyOp::Presence && parsed && TourneyParseNext(j, next);
	std::lock_guard<std::mutex> lock(g_mx);
	g_busy.erase({static_cast<int>(op), id});
	if (fresh && g_mine.mine.next.match == next.match) g_mine.mine.next = next;
	// The pick is over for this player as soon as the league takes it, before their matches are read again.
	if (out.ok && op == TourneyOp::Pick && g_mine.mine.next.match == id) {
		TourneyNext &n = g_mine.mine.next;
		n.pick = TourneyPick();
		if (out.map.Known()) {
			n.map = out.map;
			bool listed = false;
			for (TourneyGame &g : n.games) {
				if (g.no != n.game) continue;
				g.map = out.map;
				listed = true;
			}
			if (!listed) n.games.push_back({n.game, out.map, std::string()});
		}
	}
	// A room claimed while the next game's map is being chosen waits for it: the pick shows before the matches are read again.
	if (out.ok && op == TourneyOp::Claim && out.claim.kind == TourneyClaim::Wait && out.claim.pick.present && g_mine.mine.next.match == id) {
		g_mine.mine.next.pick = out.claim.pick;
	}
	if (out.ok && op == TourneyOp::Create) RememberCode(created.slug, created.id, created.inviteCode);
	if (out.ok && op == TourneyOp::Edit) {
		// A cup made open has no code any more, and its page is read by its slug again.
		if (created.inviteCode.empty()) {
			g_codes.erase(created.slug);
			g_codeIds.erase(created.id);
		}
		else RememberCode(created.slug, created.id, created.inviteCode);
	}
	if (out.ok && op == TourneyOp::FindCode) {
		RememberCode(found.slug, found.id, code);
		Page &p = g_pages[found.slug];
		p.loaded = true;
		p.failed = p.missing = false;
		p.at = p.used = GetTickCount();
		p.detail = found;
	}
	if (op != TourneyOp::Presence) g_results.push_back(out);
	// What the player did changes their tournaments and the pages: they are read again.
	if (op != TourneyOp::Presence && op != TourneyOp::Claim && op != TourneyOp::Register && op != TourneyOp::FindCode) {
		StartMine();
		g_lists.at = 0;
		for (auto &p : g_pages) p.second.at = 0;
		for (auto &d : g_disputes) d.second.at = 0;
	}
}

// `code`: the invitation code a lookup asks about.
void Start(TourneyOp op, int64_t id, const char *method, const std::string &path, const std::string &body = std::string(), const std::string &code = std::string())
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!g_busy.insert({static_cast<int>(op), id}).second) return;
	}
	SafeThread(Run, op, id, std::string(method), path, body, code).detach();
}

std::string TourneyPath(int64_t tournament, const char *what)
{
	return "/v1/tournaments/" + std::to_string(tournament) + what;
}

std::string MatchPath(int64_t match, const char *what)
{
	return "/v1/tournaments/matches/" + std::to_string(match) + what;
}

// A cup made or changed whose scenario could not go up: the answer comes now, and the cup is not sent.
void UploadFailed(TourneyOp op, int64_t id, const TourneyMap &m, const HttpReply &r, const std::string &error)
{
	TourneyOpResult out;
	out.op = op;
	out.id = id;
	out.status = r.status;
	out.mapTitle = m.title.empty() ? TourneyScenarioStem(m.file) : m.title;
	Json j;
	const TourneyRefusal no = !r.body.empty() && Json::Parse(r.body, j) ? TourneyParseRefusal(j) : TourneyRefusal();
	out.error = !error.empty() ? error : r.failure == "not linked" ? "not_linked" : r.status == 413 ? "too_large" : r.status == 429 ? "rate_limited" : no.code;
	if (out.error.empty()) out.error = r.status ? "upload_refused" : "upload_failed";
	out.message = no.message;
	out.retryAfter = no.retryAfter;
	if (r.status == 0 || r.status >= 500) LogW("league", "the scenario %s could not go up (%s)", m.file.c_str(), error.empty() ? r.Describe().c_str() : error.c_str());
	else LogI("league", "the scenario %s was refused (%s)", m.file.c_str(), error.empty() ? r.Describe().c_str() : error.c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_busy.erase({static_cast<int>(op), id});
	g_upload = TourneyUploadView();
	g_results.push_back(out);
}

// The organizer's scenarios go up one by one, each once, and then the cup.
void Publish(TourneyOp op, int64_t id, TourneyCupForm form, bool descriptionOnly)
{
	std::vector<TourneyMap> uploads, all = form.maps;
	if (TourneyCupHasSeries(form)) all.insert(all.end(), form.seriesPool.begin(), form.seriesPool.end());
	for (const TourneyMap &m : all) {
		if (descriptionOnly || m.kind != TourneyMapKind::Upload) continue;
		bool seen = false;
		for (const TourneyMap &u : uploads) seen = seen || u.hash == m.hash;
		if (!seen) uploads.push_back(m);
	}
	for (size_t i = 0; i < uploads.size(); i++) {
		const TourneyMap &m = uploads[i];
		{
			std::lock_guard<std::mutex> lock(g_mx);
			g_upload.active = true;
			g_upload.index = static_cast<int>(i) + 1;
			g_upload.count = static_cast<int>(uploads.size());
			g_upload.done = 0;
			g_upload.total = static_cast<uint64_t>(m.bytes);
			g_upload.title = m.title;
		}
		std::string bytes;
		if (!CupScenarioRead(m.file, bytes)) {
			UploadFailed(op, id, m, HttpReply(), "map_unreadable");
			return;
		}
		Sha256 h;
		h.Add(bytes);
		if (h.Hex() != m.hash) {
			UploadFailed(op, id, m, HttpReply(), "map_changed");
			return;
		}
		HttpOptions o;
		o.contentType = "application/octet-stream";
		o.sendMs = 300000;
		o.receiveMs = 60000;
		o.progress = [](uint64_t done, uint64_t total) {
			std::lock_guard<std::mutex> lock(g_mx);
			g_upload.done = done;
			g_upload.total = total;
		};
		const HttpReply r = HttpWith("PUT", "/v1/maps/" + m.hash, bytes, true, o);
		if (!r.Ok()) {
			UploadFailed(op, id, m, r, std::string());
			return;
		}
		LogI("league", "scenario %s up (%u bytes, %s)", m.file.c_str(), static_cast<unsigned>(bytes.size()), r.status == 201 ? "new" : "the league had it");
	}
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_upload = TourneyUploadView();
	}
	const bool edit = op == TourneyOp::Edit;
	const std::string body = descriptionOnly ? TourneyCupDescriptionJson(form.description).Dump() : TourneyCupJson(form, edit).Dump();
	Run(op, id, edit ? "PATCH" : "POST", edit ? TourneyPath(id, "") : std::string("/v1/tournaments"), body, std::string());
}

void StartPublish(TourneyOp op, int64_t id, const TourneyCupForm &form, bool descriptionOnly)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!g_busy.insert({static_cast<int>(op), id}).second) return;
	}
	SafeThread(Publish, op, id, form, descriptionOnly).detach();
}

}

namespace leagueclient_impl {

void TourneyTick()
{
	std::string token, steamId;
	if (!AccountFor(token, steamId)) return;
	const DWORD now = GetTickCount();
	std::lock_guard<std::mutex> lock(g_mx);
	// Never during a game: the poll waits for the menus.
	if (*reinterpret_cast<void *const *>(addr::kGame) != nullptr) {
		g_mine.gameAt = now;
		return;
	}
	if (g_mine.loading || Quiet()) return;
	const bool tab = g_mine.watched && now - g_mine.watched < kWatchMs;
	if (!g_mine.tried) {
		StartMine();
		return;
	}
	const bool active = g_mine.loaded && TourneyHasActiveEntry(g_mine.mine);
	DWORD every = static_cast<DWORD>(TourneyPollEveryMs(active, tab));
	if (!g_mine.loaded && !every) every = kMineRetryMs;
	const int64_t sinceGame = g_mine.gameAt ? static_cast<int64_t>(now - g_mine.gameAt) : -1;
	const DWORD series = g_mine.loaded ? static_cast<DWORD>(TourneySeriesPollMs(g_mine.mine.next, sinceGame)) : 0;
	if (series && (!every || series < every)) every = series;
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
	if (g_lists.loading || Quiet()) return;
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
	if (!p.loading && !Quiet() && (!p.at || now - p.at >= static_cast<DWORD>(maxAgeMs))) {
		p.loading = true;
		const auto code = g_codes.find(slug);
		SafeThread(FetchPage, slug, code == g_codes.end() ? std::string() : code->second).detach();
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
	Json body = Json::MakeObject();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const auto code = g_codeIds.find(tournament);
		if (code != g_codeIds.end()) body.Set("code", Json::MakeString(code->second));
	}
	Start(TourneyOp::SignUp, tournament, "POST", TourneyPath(tournament, "/entries"), body.Dump());
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

void LeagueTourneyClaimRoom(int64_t match, uint64_t staleLobby)
{
	Json body = Json::MakeObject();
	if (staleLobby) body.Set("stale", Json::MakeString(std::to_string(staleLobby)));
	Start(TourneyOp::Claim, match, "POST", MatchPath(match, "/room/claim"), staleLobby ? body.Dump() : std::string());
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

void LeagueTourneyPick(int64_t match, int index)
{
	Json body = Json::MakeObject();
	body.Set("index", Json::MakeNumber(index));
	Start(TourneyOp::Pick, match, "POST", MatchPath(match, "/pick"), body.Dump());
}

void LeagueTourneyCreate(const TourneyCupForm &form)
{
	StartPublish(TourneyOp::Create, 0, form, false);
}

void LeagueTourneyEdit(int64_t tournament, const TourneyCupForm &form, bool descriptionOnly)
{
	StartPublish(TourneyOp::Edit, tournament, form, descriptionOnly);
}

TourneyUploadView LeagueTourneyUploading()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return g_upload;
}

void LeagueTourneyFindByCode(const std::string &code)
{
	Start(TourneyOp::FindCode, 0, "GET", "/v1/tournaments/code/" + UrlEncode(code), std::string(), code);
}

std::string LeagueTourneyCodeOf(const std::string &slug)
{
	std::lock_guard<std::mutex> lock(g_mx);
	const auto code = g_codes.find(slug);
	return code == g_codes.end() ? std::string() : code->second;
}

void LeagueTourneyCancel(int64_t tournament)
{
	Start(TourneyOp::Cancel, tournament, "POST", TourneyPath(tournament, "/cancel"), "{}");
}

void LeagueTourneyCheckinNow(int64_t tournament)
{
	Start(TourneyOp::CheckinNow, tournament, "POST", TourneyPath(tournament, "/checkin-now"), "{}");
}

void LeagueTourneyStartNow(int64_t tournament)
{
	Start(TourneyOp::StartNow, tournament, "POST", TourneyPath(tournament, "/start"), "{}");
}

void LeagueTourneyRemoveEntry(int64_t tournament, int64_t entry)
{
	Start(TourneyOp::RemoveEntry, entry, "DELETE", TourneyPath(tournament, "/entries/") + std::to_string(entry));
}

void LeagueTourneyResolve(int64_t match, TourneyVerdict verdict)
{
	const bool walkover = verdict == TourneyVerdict::WalkoverToA || verdict == TourneyVerdict::WalkoverToB || verdict == TourneyVerdict::WalkoverNobody;
	Json body = Json::MakeObject();
	body.Set("action", Json::MakeString(verdict == TourneyVerdict::Replay ? "replay" : walkover ? "walkover" : "game"));
	if (verdict == TourneyVerdict::GameToA || verdict == TourneyVerdict::WalkoverToA) body.Set("winner", Json::MakeString("a"));
	else if (verdict == TourneyVerdict::GameToB || verdict == TourneyVerdict::WalkoverToB) body.Set("winner", Json::MakeString("b"));
	else body.Set("winner", Json());
	Start(TourneyOp::Resolve, match, "POST", MatchPath(match, "/resolve"), body.Dump());
}

TourneyDisputesView LeagueTourneyDisputes(int64_t tournament, int maxAgeMs)
{
	TourneyDisputesView v;
	if (!tournament) return v;
	std::lock_guard<std::mutex> lock(g_mx);
	Disputes &d = g_disputes[tournament];
	const DWORD now = GetTickCount();
	if (!d.loading && !Quiet() && (!d.at || now - d.at >= static_cast<DWORD>(maxAgeMs))) {
		d.loading = true;
		SafeThread(FetchDisputes, tournament).detach();
	}
	v.loaded = d.loaded;
	v.loading = d.loading;
	v.failed = d.failed;
	v.disputes = d.list;
	return v;
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
