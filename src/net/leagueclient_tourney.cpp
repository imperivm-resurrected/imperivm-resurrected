// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "net/leagueclient_tourney.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/log.h"
#include "core/sha256.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "mods/modsleague.h"
#include "net/leagueclient_internal.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kListsKeepMs = 30000, kMineRetryMs = 60000, kWatchMs = 3000;
const size_t kPagesKept = 16;
// After a 429 the reads wait what the server asks, at least this long and at most an hour.
const DWORD kQuietMinMs = 5000, kQuietMaxMs = 3600000, kQuietUnsaidMs = 5 * 60 * 1000;
// After reads of the player's tournaments that failed in a row: from this long, twice as long each time, up to the cap, never sooner
// than the poll's pace.
const DWORD kMineFirstRetryMs = 30000, kMineRetryCapMs = 30 * 60 * 1000;
// The same for a page of a cup, which a screen asks for every frame.
const DWORD kPageFirstRetryMs = 15000, kPageRetryCapMs = 10 * 60 * 1000;

struct Lists {
	bool loaded = false, loading = false, failed = false;
	DWORD at = 0;
	// Counts the player's changes: a read that started before one brings the lists as they were, and is read again.
	unsigned changes = 0;
	std::vector<TourneySummary> open, live, finished;
};

struct Page {
	DWORD at = 0, used = 0;
	// Reads that failed in a row, and how long the next waits at least after the last.
	int fails = 0;
	DWORD wait = 0;
	// What the screens are given: never changed once given, only replaced (ChangeView).
	std::shared_ptr<const TourneyPageView> view = std::make_shared<const TourneyPageView>();
};

struct Mine {
	bool tried = false;
	// `gameAt`: the last tick seen during a game, after which a series' pick opens.
	DWORD at = 0, watched = 0, gameAt = 0;
	// Reads that failed in a row, and how long after the last one the next waits at least.
	int fails = 0;
	DWORD retryWait = 0;
	// Asked for again while a read was on its way (a result came in meanwhile, say): read once more when it lands.
	bool again = false;
	std::string steamId;
	// As a page's view.
	std::shared_ptr<const TourneyMineView> view = std::make_shared<const TourneyMineView>();
};

struct Disputes {
	bool loaded = false, loading = false, failed = false;
	DWORD at = 0;
	std::vector<TourneyDispute> list;
};

// The league's map types, which change with its releases only: read again after an hour, or a minute after a read that failed.
struct MapTypes {
	bool loaded = false, loading = false;
	DWORD at = 0;
	std::vector<TourneyMapType> types;
};
const DWORD kMapTypesKeepMs = 3600000, kMapTypesRetryMs = 60000;

std::mutex g_mx;
Lists g_lists;
std::map<std::string, Page> g_pages;
Mine g_mine;
std::map<int64_t, Disputes> g_disputes;
MapTypes g_mapTypes;
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
	case TourneyOp::Accept: return "team invitation acceptance";
	case TourneyOp::Decline: return "team invitation refusal";
	case TourneyOp::CancelTeam: return "team cancellation";
	case TourneyOp::Concede: return "game given up";
	case TourneyOp::Bots: return "fill with test bots";
	case TourneyOp::Seeds: return "first round pairing";
	default: return "resolution";
	}
}

// Under g_mx: nobody looks and no cup of the player's is on, so the screens' reads wait for them to come back.
bool Idle()
{
	return PlayerAway() && !(g_mine.view->loaded && TourneyHasActiveEntry(g_mine.view->mine));
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
	// A 429 that does not say how long is Cloudflare's own, the limit of the day: minutes, not seconds.
	const int64_t asked = r.retryAfterSec > 0 ? static_cast<int64_t>(r.retryAfterSec) * 1000 : kQuietUnsaidMs;
	const DWORD wait = static_cast<DWORD>(std::max<int64_t>(kQuietMinMs, std::min<int64_t>(kQuietMaxMs, asked)));
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
	unsigned changes = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		changes = g_lists.changes;
	}
	const HttpReply r = Http("GET", "/v1/tournaments", "", false);
	Json j;
	std::vector<TourneySummary> all;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseList(j, all);
	if (!ok && r.status != 429) LogW("league", "could not read the tournaments (%s)", r.Describe().c_str());
	WaitAfter(r);
	std::lock_guard<std::mutex> lock(g_mx);
	g_lists.loading = false;
	g_lists.at = changes == g_lists.changes ? GetTickCount() : 0;
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
	p.at = GetTickCount();
	p.fails = ok ? 0 : p.fails + 1;
	p.wait = RetryDelayMs(r, p.fails, 0, kPageFirstRetryMs, kPageRetryCapMs);
	if (!ok) {
		ChangeView(p.view, [&r](TourneyPageView &v) {
			v.loading = false;
			v.failed = true;
			v.missing = r.status == 404;
		});
		return;
	}
	auto v = std::make_shared<TourneyPageView>();
	v->loaded = true;
	v->detail = std::move(d);
	p.view = std::move(v);
}

// Under g_mx.
void Notify(const TourneyMine &mine)
{
	for (const TourneyNotice &n : TourneyNewNotices(mine, g_told)) {
		const char *kind = "over";
		if (n.kind == TourneyNotice::MatchReady) kind = "match ready";
		else if (n.kind == TourneyNotice::CheckinOpen) kind = "check-in open";
		else if (n.kind == TourneyNotice::PickOpen) kind = "map to pick";
		else if (n.kind == TourneyNotice::Invite) kind = "invitation to a team";
		LogI("league", "tournament notice: %s, %s", kind, n.name.c_str());
		g_notices.push_back(n);
	}
}

void StartMine();

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
	g_mine.at = GetTickCount();
	const bool again = g_mine.again;
	g_mine.again = false;
	g_mine.fails = ok || r.status == 401 || r.failure == "not linked" ? 0 : g_mine.fails + 1;
	g_mine.retryWait = RetryDelayMs(r, g_mine.fails, 0, kMineFirstRetryMs, kMineRetryCapMs);
	if (!ok) {
		// Asked for again meanwhile or not, the next read waits for the retry.
		ChangeView(g_mine.view, [](TourneyMineView &v) { v.loading = false; });
		return;
	}
	if (g_mine.steamId != steamId) {
		g_told = TourneyTold();
		g_mine.steamId = steamId;
	}
	const TourneyMineView &was = *g_mine.view;
	if (was.loaded && was.mine.next.match != mine.next.match && mine.next.present) LogI("league", "next tournament match: %lld", static_cast<long long>(mine.next.match));
	if (was.mine.organize.activeId != mine.organize.activeId && mine.organize.activeId) {
		LogI("league", "the player organises the cup %lld", static_cast<long long>(mine.organize.activeId));
	}
	for (const TourneyOrganized &c : mine.organized) RememberCode(c.slug, c.id, c.inviteCode);
	Notify(mine);
	auto v = std::make_shared<TourneyMineView>();
	v->loaded = true;
	v->mine = std::move(mine);
	g_mine.view = std::move(v);
	if (again) StartMine();
}

void FetchMapTypes()
{
	const HttpReply r = Http("GET", "/v1/league/maps", "", false);
	Json j;
	std::vector<TourneyMapType> types;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && TourneyParseMapTypes(j, types);
	if (!ok && r.status != 429) LogW("league", "could not read the league's map types (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_mapTypes.loading = false;
	g_mapTypes.at = GetTickCount();
	if (!ok) return;
	if (!g_mapTypes.loaded) {
		int four = 0;
		for (const TourneyMapType &t : types) four += t.hasSizes4 ? 1 : 0;
		LogI("league", "%d map types read, %d with their sizes for four players", static_cast<int>(types.size()), four);
	}
	g_mapTypes.loaded = true;
	g_mapTypes.types = std::move(types);
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
	if (g_mine.view->loading) {
		g_mine.again = true;
		return;
	}
	ChangeView(g_mine.view, [](TourneyMineView &v) { v.loading = true; });
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
	std::vector<TourneyPreviewMatch> firstRound;
	if (!out.ok) {
		const TourneyRefusal no = parsed ? TourneyParseRefusal(j) : TourneyRefusal();
		out.error = r.failure == "not linked" ? "not_linked" : no.code;
		// The player's own account is checked before asking; the league's not_linked on a sign-up is about the partner of a team.
		if (op == TourneyOp::SignUp && r.status == 404 && no.code == "not_linked") out.error = "partner_not_linked";
		if (out.error.empty() && r.status == 429) out.error = "rate_limited";
		out.message = parsed ? no.message : r.failure;
		out.until = no.until;
		out.contentName = no.contentName;
		out.mods = no.mods;
		out.gamesPlayed = no.gamesPlayed;
		out.gamesNeeded = no.gamesNeeded;
		out.checkedIn = no.checkedIn;
		out.needed = no.needed;
		out.retryAfter = no.retryAfter;
		out.slug = no.slug;
		out.requirement = no.requirement;
		out.have = no.have;
		out.need = no.need;
		out.who = no.who;
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
		const TourneyEntryStatus status = entry ? TourneyEntryStatusFromId(entry->GetString("status")) : TourneyEntryStatus::Unknown;
		out.waitlist = status == TourneyEntryStatus::Waitlist;
		out.pendingPartner = status == TourneyEntryStatus::PendingPartner;
	}
	else if (op == TourneyOp::Bots && parsed) {
		out.added = j.GetInt("added");
		LogI("league", "%d test bots joined the cup %lld", out.added, static_cast<long long>(id));
	}
	else if (op == TourneyOp::Seeds && !(parsed && TourneyParseFirstRound(j.Get("firstRound"), firstRound))) {
		out.ok = false;
		out.error = "bad_answer";
		LogW("league", "the league swapped the players of cup %lld but its answer could not be read", static_cast<long long>(id));
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
	if (fresh && g_mine.view->mine.next.match == next.match) ChangeView(g_mine.view, [&next](TourneyMineView &v) { v.mine.next = next; });
	// The pick is over for this player as soon as the league takes it, before their matches are read again.
	if (out.ok && op == TourneyOp::Pick && g_mine.view->mine.next.match == id) {
		ChangeView(g_mine.view, [&out](TourneyMineView &v) {
			TourneyNext &n = v.mine.next;
			n.pick = TourneyPick();
			if (!out.map.Known()) return;
			n.map = out.map;
			bool listed = false;
			for (TourneyGame &g : n.games) {
				if (g.no != n.game) continue;
				g.map = out.map;
				listed = true;
			}
			if (!listed) n.games.push_back({n.game, out.map, std::string()});
		});
	}
	// A room claimed while the next game's map is being chosen waits for it: the pick shows before the matches are read again.
	if (out.ok && op == TourneyOp::Claim && out.claim.kind == TourneyClaim::Wait && out.claim.pick.present && g_mine.view->mine.next.match == id) {
		ChangeView(g_mine.view, [&out](TourneyMineView &v) { v.mine.next.pick = out.claim.pick; });
	}
	// An invitation answered, or one the league no longer has, leaves the player's list before it is read again.
	if ((op == TourneyOp::Accept || op == TourneyOp::Decline) && (out.ok || r.status == 404 || r.status == 410)) {
		ChangeView(g_mine.view, [id](TourneyMineView &v) {
			std::vector<TourneyInvite> &invites = v.mine.invites;
			invites.erase(std::remove_if(invites.begin(), invites.end(), [id](const TourneyInvite &i) { return i.tournamentId == id; }), invites.end());
		});
	}
	// The first round as the league has it now shows at once, before the page is read again.
	if (out.ok && op == TourneyOp::Seeds) {
		for (auto &p : g_pages) {
			if (p.second.view->detail.id != id) continue;
			ChangeView(p.second.view, [&firstRound](TourneyPageView &v) { v.detail.firstRound = firstRound; });
		}
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
		p.at = p.used = GetTickCount();
		auto v = std::make_shared<TourneyPageView>();
		v->loaded = true;
		v->loading = p.view->loading;
		v->detail = found;
		p.view = std::move(v);
	}
	// A sign-up the league took is the player's at once: the read of their tournaments that follows can be refused for its pace
	// (one each 5 s) and would leave the cup offering "Sign up" again until the next one.
	// "Already signed up" says the same, when this computer had not read it yet.
	if ((out.ok || out.error == "already_signed") && op == TourneyOp::SignUp && !TourneyMyEntryOf(g_mine.view->mine, id)) {
		const TourneySummary *cup = nullptr;
		for (const std::vector<TourneySummary> *list : {&g_lists.open, &g_lists.live}) {
			for (const TourneySummary &t : *list) cup = t.id == id ? &t : cup;
		}
		TourneyMyEntry e;
		e.tournamentId = id;
		e.status = out.waitlist ? TourneyEntryStatus::Waitlist : out.pendingPartner ? TourneyEntryStatus::PendingPartner : TourneyEntryStatus::Signed;
		if (cup) {
			e.slug = cup->slug;
			e.name = cup->name;
			e.tournamentStatus = cup->status;
			e.startsAt = cup->startsAt;
		}
		ChangeView(g_mine.view, [&e](TourneyMineView &v) {
			v.loaded = true;
			v.mine.entries.push_back(e);
		});
	}
	if (op != TourneyOp::Presence) g_results.push_back(out);
	// What the player did changes their tournaments and the pages: they are read again.
	if (op != TourneyOp::Presence && op != TourneyOp::Claim && op != TourneyOp::Register && op != TourneyOp::FindCode) {
		StartMine();
		g_lists.at = 0;
		g_lists.changes++;
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

// An organizer's scenario to go up, as read on the game thread (the game's file system is not made for two threads).
struct Upload {
	TourneyMap map;
	bool read = false;
	std::string bytes;
};

// The organizer's scenarios go up one by one, each once, and then the cup.
void Publish(TourneyOp op, int64_t id, TourneyCupForm form, bool descriptionOnly, std::vector<Upload> uploads)
{
	for (size_t i = 0; i < uploads.size(); i++) {
		const TourneyMap &m = uploads[i].map;
		const std::string &bytes = uploads[i].bytes;
		{
			std::lock_guard<std::mutex> lock(g_mx);
			g_upload.active = true;
			g_upload.index = static_cast<int>(i) + 1;
			g_upload.count = static_cast<int>(uploads.size());
			g_upload.done = 0;
			g_upload.total = static_cast<uint64_t>(m.bytes);
			g_upload.title = m.title;
		}
		if (!uploads[i].read) {
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
	std::vector<Upload> uploads;
	std::vector<TourneyMap> all = form.maps;
	if (TourneyCupHasSeries(form)) all.insert(all.end(), form.seriesPool.begin(), form.seriesPool.end());
	for (const TourneyMap &m : all) {
		if (descriptionOnly || m.kind != TourneyMapKind::Upload) continue;
		bool seen = false;
		for (const Upload &u : uploads) seen = seen || u.map.hash == m.hash;
		if (seen) continue;
		Upload u;
		u.map = m;
		u.read = CupScenarioRead(m.file, u.bytes);
		uploads.push_back(std::move(u));
	}
	SafeThread(Publish, op, id, form, descriptionOnly, std::move(uploads)).detach();
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
	if (InGame()) {
		g_mine.gameAt = now;
		return;
	}
	const std::shared_ptr<const TourneyMineView> v = g_mine.view;
	if (v->loading || Quiet()) return;
	const bool tab = g_mine.watched && now - g_mine.watched < kWatchMs;
	if (!g_mine.tried) {
		StartMine();
		return;
	}
	const bool active = v->loaded && TourneyHasActiveEntry(v->mine);
	DWORD every = static_cast<DWORD>(TourneyPollEveryMs(active, tab, PlayerAway()));
	if (!v->loaded && !every) every = kMineRetryMs;
	const int64_t sinceGame = g_mine.gameAt ? static_cast<int64_t>(now - g_mine.gameAt) : -1;
	const DWORD series = v->loaded ? static_cast<DWORD>(TourneySeriesPollMs(v->mine.next, sinceGame)) : 0;
	if (series && (!every || series < every)) every = series;
	// After failures the reads slow down, never sooner than the pace.
	if (g_mine.fails) every = std::max(every, g_mine.retryWait);
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
	if (!force && g_lists.at && (GetTickCount() - g_lists.at < kListsKeepMs || Idle())) return;
	g_lists.loading = true;
	SafeThread(FetchLists).detach();
}

std::shared_ptr<const TourneyPageView> LeagueTourneyPage(const std::string &slug, int maxAgeMs)
{
	static const std::shared_ptr<const TourneyPageView> none = std::make_shared<const TourneyPageView>();
	if (slug.empty()) return none;
	std::lock_guard<std::mutex> lock(g_mx);
	const DWORD now = GetTickCount();
	if (!g_pages.count(slug) && g_pages.size() >= kPagesKept) {
		auto oldest = g_pages.end();
		for (auto it = g_pages.begin(); it != g_pages.end(); ++it) {
			if (!it->second.view->loading && (oldest == g_pages.end() || it->second.used < oldest->second.used)) oldest = it;
		}
		if (oldest != g_pages.end()) g_pages.erase(oldest);
	}
	Page &p = g_pages[slug];
	p.used = now;
	if (!p.view->loading && !Quiet() && (!p.at || (now - p.at >= std::max(static_cast<DWORD>(maxAgeMs), p.wait) && !Idle()))) {
		ChangeView(p.view, [](TourneyPageView &v) { v.loading = true; });
		const auto code = g_codes.find(slug);
		SafeThread(FetchPage, slug, code == g_codes.end() ? std::string() : code->second).detach();
	}
	return p.view;
}

std::shared_ptr<const TourneyMineView> LeagueTourneyMine()
{
	static const std::shared_ptr<const TourneyMineView> none = std::make_shared<const TourneyMineView>();
	std::string token, steamId;
	if (!AccountFor(token, steamId)) return none;
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_mine.steamId != steamId) return none;
	return g_mine.view;
}

void LeagueTourneyMineRefresh()
{
	std::lock_guard<std::mutex> lock(g_mx);
	StartMine();
}

void LeagueTourneyGameReported()
{
	std::lock_guard<std::mutex> lock(g_mx);
	StartMine();
	g_lists.at = 0;
	g_lists.changes++;
	for (auto &p : g_pages) p.second.at = 0;
	for (auto &d : g_disputes) d.second.at = 0;
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
	body.Set("mods", LeagueModsOwnJson(ModsOwnLeagueMods()));
	Start(TourneyOp::SignUp, tournament, "POST", TourneyPath(tournament, "/entries"), body.Dump());
}

void LeagueTourneySignUpTeam(int64_t tournament, uint64_t partner, const std::string &teamName)
{
	std::string code;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const auto found = g_codeIds.find(tournament);
		if (found != g_codeIds.end()) code = found->second;
	}
	Json body = TourneyTeamEntryJson(partner, teamName, code);
	body.Set("mods", LeagueModsOwnJson(ModsOwnLeagueMods()));
	Start(TourneyOp::SignUp, tournament, "POST", TourneyPath(tournament, "/entries"), body.Dump());
}

void LeagueTourneyAnswerInvite(int64_t tournament, int64_t entry, bool accept)
{
	Start(accept ? TourneyOp::Accept : TourneyOp::Decline, tournament, "POST", TourneyPath(tournament, "/entries/") + std::to_string(entry) + (accept ? "/accept" : "/decline"),
		"{}");
}

void LeagueTourneyCancelTeam(int64_t tournament)
{
	Start(TourneyOp::CancelTeam, tournament, "DELETE", TourneyPath(tournament, "/entries/me"));
}

std::vector<TourneyMapType> LeagueTourneyMapTypes()
{
	std::lock_guard<std::mutex> lock(g_mx);
	const DWORD now = GetTickCount();
	const DWORD keep = g_mapTypes.loaded ? kMapTypesKeepMs : kMapTypesRetryMs;
	if (!g_mapTypes.loading && !Quiet() && (!g_mapTypes.at || now - g_mapTypes.at >= keep)) {
		g_mapTypes.loading = true;
		SafeThread(FetchMapTypes).detach();
	}
	return g_mapTypes.types;
}

void LeagueTourneyWithdraw(int64_t tournament)
{
	Start(TourneyOp::Withdraw, tournament, "DELETE", "/v1/tournaments/" + std::to_string(tournament) + "/entries/me");
}

void LeagueTourneyCheckIn(int64_t tournament)
{
	Json body = Json::MakeObject();
	body.Set("contentKey", Json::MakeString(SteamOwnGameplayKey()));
	body.Set("mods", LeagueModsOwnJson(ModsOwnLeagueMods()));
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

void LeagueTourneyConcede(int64_t match)
{
	Start(TourneyOp::Concede, match, "POST", MatchPath(match, "/concede"));
}

void LeagueTourneyFillBots(int64_t tournament)
{
	Start(TourneyOp::Bots, tournament, "POST", TourneyPath(tournament, "/bots"), "{}");
}

void LeagueTourneyDispute(int64_t match, const std::string &text)
{
	Json body = Json::MakeObject();
	body.Set("text", Json::MakeString(TourneyCutText(text, 500)));
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

void LeagueTourneySwapSeeds(int64_t tournament, int64_t a, int64_t b)
{
	Json body = Json::MakeObject();
	Json swap = Json::MakeArray();
	swap.arr.push_back(Json::MakeNumber(static_cast<double>(a)));
	swap.arr.push_back(Json::MakeNumber(static_cast<double>(b)));
	body.Set("swap", swap);
	LogI("league", "cup %lld: entries %lld and %lld swapped in the first round", static_cast<long long>(tournament), static_cast<long long>(a), static_cast<long long>(b));
	Start(TourneyOp::Seeds, tournament, "POST", TourneyPath(tournament, "/seeds"), body.Dump());
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
	if (!d.loading && !Quiet() && (!d.at || (now - d.at >= static_cast<DWORD>(maxAgeMs) && !Idle()))) {
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
