// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueranked.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

int64_t IdNumber(const Json &j, const char *key)
{
	return _strtoi64(j.GetIdText(key).c_str(), nullptr, 10);
}

LeagueMapFacts MapOf(const Json *v)
{
	LeagueMapFacts m;
	if (!v) return m;
	if (v->type == Json::String) m.name = v->str;
	else if (v->type == Json::Object) {
		m.name = v->GetString("name");
		m.type = v->GetString("type");
		m.size = v->GetString("size");
	}
	return m;
}

LeaguePlayerCard CardOf(const Json &j)
{
	LeaguePlayerCard c;
	c.steamId = j.GetIdText("steamId");
	c.name = j.GetString("name");
	c.tier = LeagueTierFromJson(j.Get("tier"));
	c.division = LeagueDivisionOf(c.tier, j.Get("division"));
	c.accepted = j.GetBool("accepted");
	c.host = j.GetBool("host");
	c.inRoom = j.GetBool("inRoom");
	for (const char *key : {"wins", "losses", "glory", "place"}) {
		const Json *v = j.Get(key);
		c.standing = c.standing || (v && v->type == Json::Number);
	}
	c.wins = std::max(0, j.GetInt("wins"));
	c.losses = std::max(0, j.GetInt("losses"));
	c.glory = std::max(0, j.GetInt("glory"));
	c.place = std::max(0, j.GetInt("place"));
	const Json *streak = j.Get("streak");
	c.streakKnown = streak && streak->type == Json::Number;
	c.streak = c.streakKnown ? static_cast<int>(streak->num) : 0;
	c.duo = j.GetBool("duo");
	const int team = j.GetInt("team");
	c.team = team == 1 || team == 2 ? team : 0;
	return c;
}

// [{side, players: [...]}]: the two sides of a 2v2 match, each player with its side.
std::vector<LeagueMatchTeam> TeamsOf(const Json *v)
{
	std::vector<LeagueMatchTeam> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &t : v->arr) {
		if (t.type != Json::Object) continue;
		LeagueMatchTeam team;
		team.side = t.GetInt("side");
		if (team.side != 1 && team.side != 2) continue;
		if (const Json *players = t.Get("players"); players && players->type == Json::Array) {
			for (const Json &p : players->arr) {
				if (p.type != Json::Object) continue;
				LeaguePlayerCard c = CardOf(p);
				c.team = team.side;
				team.players.push_back(c);
			}
		}
		out.push_back(team);
	}
	return out;
}

// 1 or 2; 0 for anything else.
int SideOf(const Json *v)
{
	const int side = v && v->type == Json::Number ? static_cast<int>(v->num) : 0;
	return side == 1 || side == 2 ? side : 0;
}

std::vector<LeagueMapFacts> MapsOf(const Json *v)
{
	std::vector<LeagueMapFacts> out;
	if (!v || v->type != Json::Array) return out;
	for (const Json &m : v->arr) {
		const LeagueMapFacts f = MapOf(&m);
		if (!f.name.empty()) out.push_back(f);
	}
	return out;
}

LeagueQueueStats QueueStatsOf(const Json &j)
{
	LeagueQueueStats s;
	s.searching = std::max(0, j.GetInt("searching"));
	s.playing = std::max(0, j.GetInt("playing"));
	s.gamesToday = std::max(0, j.GetInt("gamesToday"));
	return s;
}

// 0 to 1, or -1 when it is missing or out of range.
double ExpectedOf(const Json *v)
{
	if (!v || v->type != Json::Number || !(v->num >= 0 && v->num <= 1)) return -1;
	return v->num;
}

LeagueSearchWindow WindowOf(const Json *v)
{
	LeagueSearchWindow w;
	if (!v || v->type != Json::Object) return w;
	w.present = true;
	const Json *from = v->Get("from"), *to = v->Get("to");
	if (from && from->type == Json::Number && from->num > 0) w.from = static_cast<int>(from->num);
	if (to && to->type == Json::Number && to->num >= 0 && to->num < 1e9) w.to = static_cast<int>(to->num);
	return w;
}

LeagueMatchStatus MatchStatusOf(const std::string &s)
{
	const std::string l = Lower(s);
	if (l == "accepting") return LeagueMatchStatus::Accepting;
	if (l == "ready") return LeagueMatchStatus::Ready;
	if (l == "playing") return LeagueMatchStatus::Playing;
	if (l == "done") return LeagueMatchStatus::Done;
	if (l == "cancelled" || l == "canceled") return LeagueMatchStatus::Cancelled;
	return LeagueMatchStatus::Unknown;
}

float Clamp01(float v)
{
	return v < 0 ? 0 : v > 1 ? 1 : v;
}

float EaseInOut(float t)
{
	t = Clamp01(t);
	return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}

// Where a moment falls in [from, to), 0 to 1.
float Phase(uint32_t ms, uint32_t from, uint32_t to)
{
	if (ms <= from) return 0;
	if (ms >= to) return 1;
	return static_cast<float>(ms - from) / static_cast<float>(to - from);
}

float BarOf(int glory)
{
	const LeagueGloryPlace p = LeagueGloryPlaceOf(glory);
	return p.capped ? p.into / 100.0f : 1.0f;
}

int Lerp(int a, int b, float t)
{
	return a + static_cast<int>(std::lround((b - a) * t));
}

// The one-frame flash when the bar arrives: white, fading over 60 ms.
int FlashAt(uint32_t ms, uint32_t at)
{
	if (ms < at || ms >= at + 60) return 0;
	return static_cast<int>(255 * (1.0f - (ms - at) / 60.0f));
}

}

LeagueRoomSettings LeagueParseRoomSettings(const Json *v)
{
	LeagueRoomSettings s;
	if (!v || v->type != Json::Object) return s;
	s.preset = Lower(v->GetString("preset"));
	s.hideCivs = v->GetFlag("hideCivs");
	s.fog = v->GetFlag("fog");
	s.explored = v->GetFlag("explored");
	s.cheats = v->GetFlag("cheats");
	s.diplomacy = Lower(v->GetString("diplomacy"));
	if (const Json *d = v->Get("specDelay"); d && d->type == Json::Number) s.specDelay = std::max(0, static_cast<int>(d->num));
	else if (const Json *off = v->Get("spectators"); off && off->type == Json::Bool && !off->b) s.specDelay = 0;
	if (const Json *p = v->Get("pauses"); p && p->type == Json::Object) {
		s.pauseCount = p->GetInt("count", -1);
		s.pauseSeconds = p->GetInt("totalSec", -1);
	}
	else if (p && p->type == Json::Number) s.pauseCount = std::max(0, static_cast<int>(p->num));
	return s;
}

bool LeagueParseSeason(const Json &j, LeagueSeason &out)
{
	if (j.type != Json::Object || !j.Get("endsAt")) return false;
	out = LeagueSeason();
	out.number = j.GetInt("number");
	out.name = j.GetString("name");
	out.theme = j.GetString("theme");
	out.contentKey = j.GetString("contentKey");
	if (const Json *names = j.Get("names"); names && names->type == Json::Object) {
		for (const auto &kv : names->obj) {
			if (kv.second.type == Json::String && !kv.second.str.empty()) out.names[Lower(kv.first)] = kv.second.str;
		}
	}
	out.startsAt = j.GetInt64("startsAt");
	out.endsAt = j.GetInt64("endsAt");
	out.beta = j.GetBool("beta");
	out.active = j.GetBool("active", true);
	out.maps = MapsOf(j.Get("maps"));
	out.maps2v2 = MapsOf(j.Get("maps2v2"));
	out.settings = LeagueParseRoomSettings(j.Get("settings"));
	if (const Json *queues = j.Get("queues"); queues && queues->type == Json::Array) {
		for (const Json &q : queues->arr) {
			if (q.type == Json::String && !q.str.empty()) out.queues.push_back(Lower(q.str));
		}
	}
	if (out.queues.empty()) out.queues.push_back("1v1");
	return true;
}

bool LeagueSeasonHasQueue(const LeagueSeason &s, const std::string &queue)
{
	return std::find(s.queues.begin(), s.queues.end(), Lower(queue)) != s.queues.end();
}

std::string LeagueSeasonName(const LeagueSeason &s, const std::string &lang)
{
	auto it = s.names.find(lang);
	return it != s.names.end() ? it->second : s.name;
}

int LeagueDaysLeft(int64_t endsAt, int64_t nowMs)
{
	if (endsAt <= nowMs) return 0;
	const int64_t day = 86400000;
	return static_cast<int>((endsAt - nowMs + day - 1) / day);
}

float LeagueSeasonElapsed(const LeagueSeason &s, int64_t nowMs)
{
	if (s.endsAt <= s.startsAt) return 0;
	return Clamp01(static_cast<float>(static_cast<double>(nowMs - s.startsAt) / static_cast<double>(s.endsAt - s.startsAt)));
}

std::string LeagueLangCode(const std::string &gameLanguage)
{
	const std::string l = Lower(gameLanguage);
	if (l.compare(0, 2, "es") == 0 || l.compare(0, 4, "span") == 0 || l.compare(0, 4, "cast") == 0) return "es";
	if (l.compare(0, 2, "it") == 0) return "it";
	return "en";
}

bool LeagueParseQueue(const Json &j, LeagueQueueAnswer &out)
{
	if (j.type != Json::Object) return false;
	out = LeagueQueueAnswer();
	const std::string status = Lower(j.GetString("status"));
	if (status == "idle") out.status = LeagueQueueStatus::Idle;
	else if (status == "searching") out.status = LeagueQueueStatus::Searching;
	else if (status == "found") out.status = LeagueQueueStatus::Found;
	else return false;
	out.queue = Lower(j.GetString("queue"));
	out.since = j.GetInt64("since");
	out.searching = j.GetInt("searching");
	out.estimateSec = j.GetInt("estimateSec");
	out.serverNow = j.GetInt64("serverNow");
	out.matchId = IdNumber(j, "matchId");
	out.window = WindowOf(j.Get("window"));
	if (const Json *m = j.Get("match"); m && m->type == Json::Object) {
		out.matchId = IdNumber(*m, "id");
		if (const Json *o = m->Get("opponent"); o && o->type == Json::Object) out.opponent = CardOf(*o);
		out.map = MapOf(m->Get("map"));
		out.acceptBy = m->GetInt64("acceptBy");
		out.accepted = m->GetBool("accepted");
		out.expected = ExpectedOf(m->Get("expected"));
		out.teams = TeamsOf(m->Get("teams"));
		out.mySide = SideOf(m->Get("mySide"));
		if (out.queue.empty()) out.queue = Lower(m->GetString("queue"));
	}
	if (out.expected < 0) out.expected = ExpectedOf(j.Get("expected"));
	if (out.teams.empty()) out.teams = TeamsOf(j.Get("teams"));
	if (!out.mySide) out.mySide = SideOf(j.Get("mySide"));
	return out.status != LeagueQueueStatus::Found || out.matchId > 0;
}

LeagueRankedResult LeagueParseRankedResult(const Json *v)
{
	LeagueRankedResult r;
	if (!v || v->type != Json::Object) return r;
	r.present = true;
	r.gloryBefore = v->GetInt("gloryBefore");
	r.gloryAfter = v->GetInt("gloryAfter");
	r.delta = v->GetInt("delta");
	r.tierBefore = LeagueTierFromJson(v->Get("tierBefore"));
	r.tierAfter = LeagueTierFromJson(v->Get("tierAfter"));
	r.divisionBefore = LeagueDivisionOf(r.tierBefore, v->Get("divisionBefore"));
	r.divisionAfter = LeagueDivisionOf(r.tierAfter, v->Get("divisionAfter"));
	r.promoted = v->GetBool("promoted");
	r.demoted = v->GetBool("demoted");
	r.shield = v->GetInt("shield");
	r.placementLeft = v->GetInt("placementLeft");
	r.placed = v->GetBool("placed");
	return r;
}

bool LeagueParseMatch(const Json &j, LeagueMatchTicket &out)
{
	if (j.type != Json::Object) return false;
	out = LeagueMatchTicket();
	out.id = IdNumber(j, "id");
	out.status = MatchStatusOf(j.GetString("status"));
	out.queue = Lower(j.GetString("queue"));
	out.host = j.GetIdText("host");
	out.lobby = j.GetIdText("lobby");
	out.lobbyKey = j.GetString("lobbyKey");
	out.contentKey = j.GetString("contentKey");
	out.outcome = j.GetString("outcome");
	out.map = MapOf(j.Get("map"));
	out.settings = LeagueParseRoomSettings(j.Get("settings"));
	out.acceptBy = j.GetInt64("acceptBy");
	out.readyAt = j.GetInt64("readyAt");
	out.roomDueAt = j.GetInt64("roomDueAt");
	out.startedAt = j.GetInt64("startedAt");
	out.abandonedBy = j.GetIdText("abandonedBy");
	out.fault = j.GetIdText("fault");
	if (const Json *players = j.Get("players"); players && players->type == Json::Array) {
		for (const Json &p : players->arr) {
			if (p.type == Json::Object) out.players.push_back(CardOf(p));
		}
	}
	if (const Json *o = j.Get("opponent"); o && o->type == Json::Object) out.opponent = CardOf(*o);
	out.expected = ExpectedOf(j.Get("expected"));
	out.teams = TeamsOf(j.Get("teams"));
	out.mySide = SideOf(j.Get("mySide"));
	out.ranked = LeagueParseRankedResult(j.Get("ranked"));
	out.serverNow = j.GetInt64("serverNow");
	return out.id > 0 && out.status != LeagueMatchStatus::Unknown;
}

const LeaguePlayerCard *LeagueTicketPlayer(const LeagueMatchTicket &t, const std::string &steamId, bool other)
{
	for (const LeaguePlayerCard &p : t.players) {
		if ((p.steamId == steamId) != other) return &p;
	}
	return nullptr;
}

uint64_t LeagueTicketLobby(const LeagueMatchTicket &t)
{
	return _strtoui64(t.lobby.c_str(), nullptr, 10);
}

std::string LeagueRankedRoomTag(int64_t matchId)
{
	return "ranked:" + std::to_string(static_cast<long long>(matchId));
}

bool LeagueTicketAbandonable(const LeagueMatchTicket &t)
{
	return (t.status == LeagueMatchStatus::Ready || t.status == LeagueMatchStatus::Playing) && !t.startedAt;
}

bool LeagueAbandonCosts(const LeagueMatchTicket &t, const std::string &steamId, int64_t nowMs)
{
	if (!LeagueTicketAbandonable(t) || steamId.empty()) return false;
	if (t.status == LeagueMatchStatus::Ready) return t.roomDueAt && nowMs >= t.roomDueAt ? t.host == steamId : true;
	for (const LeaguePlayerCard &p : t.players) {
		if (!p.inRoom) return false;
	}
	return t.players.size() >= 2;
}

bool LeagueParseBoard(const Json &j, LeagueBoard &out)
{
	if (j.type != Json::Object) return false;
	const Json *rows = j.Get("rows");
	if (!rows || rows->type != Json::Array) return false;
	out = LeagueBoard();
	out.season = j.GetInt("season");
	out.total = j.GetInt("total");
	out.offset = j.GetInt("offset");
	out.me = j.GetInt("me");
	for (const Json &r : rows->arr) {
		if (r.type != Json::Object) continue;
		LeagueBoardRow row;
		row.position = r.GetInt("position");
		row.steamId = r.GetIdText("steamId");
		row.name = r.GetString("name");
		row.tier = LeagueTierFromJson(r.Get("tier"));
		row.division = LeagueDivisionOf(row.tier, r.Get("division"));
		row.glory = r.GetInt("glory");
		row.wins = r.GetInt("wins");
		row.losses = r.GetInt("losses");
		const Json *delta7 = r.Get("delta7");
		row.hasDelta7 = delta7 && delta7->type == Json::Number;
		row.delta7 = row.hasDelta7 ? static_cast<int>(delta7->num) : 0;
		row.streak = r.GetInt("streak");
		if (row.position > 0) out.rows.push_back(row);
	}
	return true;
}

const LeagueBoardRow *LeagueBoardFind(const LeagueBoard &b, const std::string &steamId)
{
	if (steamId.empty()) return nullptr;
	for (const LeagueBoardRow &r : b.rows) {
		if (r.steamId == steamId) return &r;
	}
	return nullptr;
}

int LeagueBoardPageOf(int position, int limit)
{
	if (position <= 0 || limit <= 0) return 0;
	return (position - 1) / limit * limit;
}

int LeagueWinRate(int wins, int losses)
{
	const int games = wins + losses;
	return games > 0 ? static_cast<int>(std::lround(100.0 * wins / games)) : 0;
}

bool LeagueParseReportAnswer(const Json &j, LeagueReportAnswer &out)
{
	if (j.type != Json::Object) return false;
	out = LeagueReportAnswer();
	out.status = Lower(j.GetString("status"));
	out.outcome = j.GetString("outcome");
	out.ranked = LeagueParseRankedResult(j.Get("ranked"));
	return !out.status.empty();
}

LeagueError LeagueParseError(const Json &j)
{
	LeagueError e;
	if (j.type != Json::Object) return e;
	e.code = j.GetString("error");
	e.message = j.GetString("message");
	e.contentKey = j.GetString("contentKey");
	e.contentName = j.GetString("contentName");
	e.kind = Lower(j.GetString("kind"));
	e.reason = j.GetString("reason");
	e.until = j.GetInt64("until");
	if (!e.until) e.until = j.GetInt64("queueBanUntil");
	e.matchId = IdNumber(j, "matchId");
	e.retryAfter = std::max(0, j.GetInt("retryAfter"));
	e.gap = std::max(0, j.GetInt("gap"));
	e.max = std::max(0, j.GetInt("max"));
	e.who = Lower(j.GetString("who"));
	e.steamId = j.GetIdText("steamId");
	return e;
}

int64_t LeagueRetryAfterMs(int httpStatus, const LeagueError &e, int64_t normalMs)
{
	if (httpStatus != 429) return normalMs;
	const int64_t asked = e.retryAfter > 0 ? static_cast<int64_t>(std::min(e.retryAfter, 3600)) * 1000 : 5000;
	return std::max(asked, normalMs);
}

const char *LeagueBanText(const std::string &kind)
{
	const std::string k = Lower(kind);
	if (k == "abandoned") return "You left a ranked game: you can't search for a while.";
	if (k == "staff") return "The league's team has suspended you from ranked for a while.";
	if (k == "desync") return "Too many of your games went out of sync: you can't search for a while.";
	return "You declined or let a match go: you can't search for a while.";
}

bool LeagueParseStats(const Json &j, LeagueStats &out)
{
	if (j.type != Json::Object || !j.Get("searching")) return false;
	out = LeagueStats();
	const LeagueQueueStats all = QueueStatsOf(j);
	out.searching = all.searching;
	out.playing = all.playing;
	out.gamesToday = all.gamesToday;
	out.serverNow = j.GetInt64("serverNow");
	if (const Json *by = j.Get("byQueue"); by && by->type == Json::Object) {
		for (const auto &kv : by->obj) {
			if (kv.second.type == Json::Object) out.byQueue[Lower(kv.first)] = QueueStatsOf(kv.second);
		}
	}
	return true;
}

LeagueQueueStats LeagueStatsOf(const LeagueStats &s, const std::string &queue)
{
	const std::string q = Lower(queue);
	const auto it = s.byQueue.find(q);
	if (it != s.byQueue.end()) return it->second;
	if (!s.byQueue.empty() || (q != "1v1" && !q.empty())) return LeagueQueueStats();
	LeagueQueueStats all;
	all.searching = s.searching;
	all.playing = s.playing;
	all.gamesToday = s.gamesToday;
	return all;
}

int64_t LeagueClockOffsetMs(int64_t serverNow, int64_t sentAtMs, int64_t receivedAtMs)
{
	if (serverNow <= 0) return 0;
	if (receivedAtMs < sentAtMs) receivedAtMs = sentAtMs;
	return serverNow - (sentAtMs + (receivedAtMs - sentAtMs) / 2);
}

bool LeagueParseHistory(const Json &j, LeagueHistoryPage &out)
{
	if (j.type != Json::Object) return false;
	const Json *list = j.Get("matches");
	if (!list || list->type != Json::Array) return false;
	out = LeagueHistoryPage();
	out.more = j.GetBool("more");
	for (const Json &m : list->arr) {
		LeagueHistoryGame g;
		if (LeagueParseHistoryGame(m, g)) out.games.push_back(g);
	}
	return true;
}

bool LeagueParseHistoryGame(const Json &m, LeagueHistoryGame &g)
{
	if (m.type != Json::Object) return false;
	g = LeagueHistoryGame();
	g.id = IdNumber(m, "id");
	g.playedAt = m.GetInt64("playedAt");
	g.durationMs = static_cast<uint32_t>(std::max(0.0, m.GetNumber("durationMs")));
	g.map = MapOf(m.Get("map"));
	if (const Json *o = m.Get("opponent"); o && o->type == Json::Object) g.opponent = CardOf(*o);
	if (const Json *p = m.Get("partner"); p && p->type == Json::Object) g.partner = CardOf(*p);
	if (const Json *list = m.Get("opponents"); list && list->type == Json::Array) {
		for (const Json &o : list->arr) {
			if (o.type == Json::Object) g.opponents.push_back(CardOf(o));
		}
	}
	if (g.opponent.steamId.empty() && g.opponent.name.empty() && !g.opponents.empty()) g.opponent = g.opponents.front();
	g.myCiv = m.GetString("myCiv");
	g.opponentCiv = m.GetString("opponentCiv");
	g.endedBy = Lower(m.GetString("endedBy"));
	const std::string result = Lower(m.GetString("result"));
	g.result = result == "win" ? LeagueGameResult::Win
		: result == "loss"	   ? LeagueGameResult::Loss
		: result == "void"	   ? LeagueGameResult::Void
		: result == "disputed" ? LeagueGameResult::Disputed
							   : LeagueGameResult::Pending;
	const Json *delta = m.Get("gloryDelta");
	g.hasDelta = delta && delta->type == Json::Number;
	g.gloryDelta = g.hasDelta ? static_cast<int>(delta->num) : 0;
	g.placed = m.GetBool("placed");
	if (g.placed) g.hasDelta = false;
	return g.id > 0;
}

int64_t LeagueHistoryNextBefore(const LeagueHistoryPage &p)
{
	return p.more && !p.games.empty() ? p.games.back().playedAt : 0;
}

bool LeagueDisputeOpen(int64_t playedAt, int64_t nowMs)
{
	return playedAt > 0 && nowMs - playedAt < kLeagueDisputeWindowMs;
}

bool LeaguePlaceShown(int position, int total)
{
	return position > 0 && total > 0 && position <= std::max(1, total / 2);
}

bool LeagueImperatorLineAfter(const LeagueBoardRow &row, const LeagueBoardRow *next)
{
	return row.tier == LeagueTier::Imperator && next && next->tier != LeagueTier::Imperator;
}

bool LeagueSurrenderAllowed(bool ranked, uint32_t gameMs)
{
	return !ranked || gameMs >= kLeagueSurrenderFromMs;
}

void LeagueSeatsFromTicket(std::vector<LeagueSeat> &seats, const LeagueMatchTicket &t, uint64_t ownId)
{
	const LeaguePlayerCard *other = LeagueTicketPlayer(t, std::to_string(ownId), true);
	const uint64_t otherId = other ? _strtoui64(other->steamId.c_str(), nullptr, 10) : 0;
	int unknown = 0;
	for (const LeagueSeat &s : seats) unknown += s.human && !s.local && !s.steamId ? 1 : 0;
	for (LeagueSeat &s : seats) {
		if (!s.human || s.steamId) continue;
		if (s.local) s.steamId = ownId;
		else if (unknown == 1 && otherId && otherId != ownId) s.steamId = otherId;
	}
}

LeagueGloryPlace LeagueGloryPlaceOf(int glory)
{
	LeagueGloryPlace p;
	const int index = LeagueDivisionIndex(glory);
	p.tier = LeagueIndexTier(index);
	p.division = LeagueIndexDivision(index);
	p.into = std::max(0, glory) - index * 100;
	p.capped = index < kLeagueDivisionCount - 1;
	return p;
}

void LeagueNextDivision(LeagueTier tier, int division, LeagueTier &nextTier, int &nextDivision)
{
	nextTier = LeagueTier::None;
	nextDivision = 0;
	if (!LeagueTierHasDivisions(tier) || division < 1 || division > 3) return;
	if (division > 1) {
		nextTier = tier;
		nextDivision = division - 1;
	}
	else if (tier != LeagueTier::Legate) {
		nextTier = static_cast<LeagueTier>(static_cast<int>(tier) + 1);
		nextDivision = 3;
	}
}

int LeagueWinsToNext(int glory)
{
	const LeagueGloryPlace p = LeagueGloryPlaceOf(glory);
	if (!p.capped) return 0;
	return std::max(1, (100 - p.into + 19) / 20);
}

float LeagueCountdownFraction(int64_t acceptBy, int64_t nowMs, int64_t windowMs)
{
	if (windowMs <= 0) return 0;
	return Clamp01(static_cast<float>(acceptBy - nowMs) / static_cast<float>(windowMs));
}

int LeagueCountdownSeconds(int64_t acceptBy, int64_t nowMs)
{
	if (acceptBy <= nowMs) return 0;
	return static_cast<int>((acceptBy - nowMs + 999) / 1000);
}

std::string LeagueClockText(int64_t ms)
{
	const long long s = std::max<int64_t>(0, ms) / 1000;
	char buf[32];
	sprintf_s(buf, "%lld:%02lld", s / 60, s % 60);
	return buf;
}

std::vector<LeagueBadgeChoice> LeagueBadgeChoices(const LeagueMe &me)
{
	std::vector<LeagueBadgeChoice> out;
	const std::string chosen = me.badgeKind.empty() ? std::string("rank") : me.badgeKind;
	// The rank of each queue: the 1v1 one always (empty while in placement), the 2v2 one once the player has it.
	const bool team = chosen == "rank" && me.badgeId == "2v2";
	LeagueBadgeChoice rank;
	rank.kind = "rank";
	if (me.ranked.present && me.ranked.tier != LeagueTier::None) {
		rank.look.kind = LeagueBadgeKind::Rank;
		rank.look.tier = me.ranked.tier;
		rank.look.division = me.ranked.division;
	}
	rank.chosen = chosen == "rank" && !team;
	out.push_back(rank);
	if (me.ranked2v2.present && me.ranked2v2.tier != LeagueTier::None) {
		LeagueBadgeChoice duo;
		duo.kind = "rank";
		duo.id = "2v2";
		duo.look.kind = LeagueBadgeKind::Rank;
		duo.look.tier = me.ranked2v2.tier;
		duo.look.division = me.ranked2v2.division;
		duo.chosen = team;
		out.push_back(duo);
	}
	for (const LeagueBadge &b : me.badges) {
		if (b.kind != LeagueBadgeKind::Season && b.kind != LeagueBadgeKind::Trophy) continue;
		LeagueBadgeChoice c;
		c.kind = b.kind == LeagueBadgeKind::Season ? "season" : "trophy";
		c.id = b.id;
		c.look = b;
		c.chosen = chosen == c.kind && me.badgeId == b.id;
		out.push_back(c);
	}
	LeagueBadgeChoice none;
	none.kind = "none";
	none.chosen = chosen == "none";
	out.push_back(none);
	return out;
}

LeagueResultKind LeagueResultKindOf(const LeagueRankedResult &r)
{
	if (r.tierAfter == LeagueTier::None) return LeagueResultKind::Placement;
	if (r.placed || r.tierBefore == LeagueTier::None) return LeagueResultKind::PlacementDone;
	if (r.promoted) return LeagueResultKind::Promotion;
	if (r.demoted) return LeagueResultKind::Demotion;
	return LeagueResultKind::Steady;
}

int LeagueResultDurationMs(LeagueResultKind kind)
{
	switch (kind) {
	case LeagueResultKind::Promotion: return 2400;
	case LeagueResultKind::Demotion: return 1800;
	case LeagueResultKind::Placement: return 600;
	case LeagueResultKind::PlacementDone: return 2000;
	default: return 1200;
	}
}

int LeagueResultCues(const LeagueRankedResult &r, uint32_t ms)
{
	const LeagueResultKind kind = LeagueResultKindOf(r);
	int cues = 0;
	if ((kind == LeagueResultKind::Steady || kind == LeagueResultKind::Promotion || kind == LeagueResultKind::Demotion) && r.delta != 0 &&
		ms >= (kind == LeagueResultKind::Steady ? 300u : 0u)) {
		cues |= kLeagueCueOutcome;
	}
	if ((kind == LeagueResultKind::Promotion || kind == LeagueResultKind::Demotion || kind == LeagueResultKind::PlacementDone) && ms >= 800) cues |= kLeagueCueRank;
	return cues;
}

LeagueResultFrame LeagueResultAt(const LeagueRankedResult &r, uint32_t ms)
{
	LeagueResultFrame f;
	f.kind = LeagueResultKindOf(r);
	const uint32_t end = static_cast<uint32_t>(LeagueResultDurationMs(f.kind));
	if (ms > end) ms = end;
	f.done = ms >= end;
	const float before = BarOf(r.gloryBefore), after = BarOf(r.gloryAfter);
	f.tier = r.tierBefore;
	f.division = r.divisionBefore;
	f.glory = r.gloryBefore;
	f.bar = before;
	switch (f.kind) {
	case LeagueResultKind::Steady: {
		f.deltaShown = ms >= 300;
		const float e = EaseInOut(Phase(ms, 300, 1100));
		f.bar = before + (after - before) * e;
		f.glory = Lerp(r.gloryBefore, r.gloryAfter, e);
		if (f.deltaShown && !f.done) {
			f.ghostLoss = after < before;
			f.ghostFrom = std::min(before, after);
			f.ghostTo = std::max(before, after);
		}
		f.flash = FlashAt(ms, 1100);
		f.tier = r.tierAfter;
		f.division = r.divisionAfter;
		break;
	}
	case LeagueResultKind::Promotion: {
		f.deltaShown = true;
		const int top = (LeagueDivisionIndex(r.gloryBefore) + 1) * 100;
		const bool newTier = r.tierAfter != r.tierBefore;
		if (ms < 800) {
			const float e = EaseInOut(Phase(ms, 0, 600));
			f.bar = before + (1.0f - before) * e;
			f.glory = Lerp(r.gloryBefore, top, e);
			f.ghostFrom = before;
			f.ghostTo = 1.0f;
			f.flash = FlashAt(ms, 600);
			f.oldAlpha = 255 - static_cast<int>(153 * Phase(ms, 600, 800));
			break;
		}
		f.switched = true;
		f.tier = r.tierAfter;
		f.division = r.divisionAfter;
		f.oldAlpha = 0;
		const float in = Phase(ms, 800, 1400);
		f.newScale = 85 + static_cast<int>(std::lround(15 * EaseInOut(in)));
		f.newAlpha = static_cast<int>(255 * in);
		f.rays = newTier ? static_cast<int>(160 * in) : 0;
		const float e = EaseInOut(Phase(ms, 1400, 2000));
		f.bar = after * e;
		f.glory = Lerp(top, r.gloryAfter, e);
		if (ms >= 1400 && !f.done) {
			f.ghostFrom = 0;
			f.ghostTo = after;
		}
		f.shieldShown = ms >= 2000 && r.shield > 0;
		break;
	}
	case LeagueResultKind::Demotion: {
		f.deltaShown = true;
		if (ms < 800) {
			f.oldAlpha = 255 - static_cast<int>(153 * Phase(ms, 400, 800));
			break;
		}
		f.switched = true;
		f.tier = r.tierAfter;
		f.division = r.divisionAfter;
		f.oldAlpha = 0;
		f.newAlpha = static_cast<int>(255 * Phase(ms, 800, 1200));
		const int top = (LeagueDivisionIndex(r.gloryAfter) + 1) * 100;
		const float e = EaseInOut(Phase(ms, 1200, 1600));
		f.bar = 1.0f + (after - 1.0f) * e;
		f.glory = Lerp(top, r.gloryAfter, e);
		if (ms >= 1200) {
			f.ghostLoss = true;
			f.ghostFrom = f.bar;
			f.ghostTo = 1.0f;
		}
		break;
	}
	case LeagueResultKind::Placement:
		f.bar = 0;
		f.glory = 0;
		break;
	case LeagueResultKind::PlacementDone: {
		f.deltaShown = true;
		f.bar = 0;
		f.glory = 0;
		if (ms < 800) {
			f.oldAlpha = 255 - static_cast<int>(153 * Phase(ms, 600, 800));
			break;
		}
		f.switched = true;
		f.tier = r.tierAfter;
		f.division = r.divisionAfter;
		f.oldAlpha = 0;
		const float in = Phase(ms, 800, 1400);
		f.newScale = 85 + static_cast<int>(std::lround(15 * EaseInOut(in)));
		f.newAlpha = static_cast<int>(255 * in);
		f.rays = static_cast<int>(160 * in);
		const float e = EaseInOut(Phase(ms, 1400, 2000));
		f.bar = after * e;
		f.glory = Lerp(0, r.gloryAfter, e);
		break;
	}
	}
	// The delta counts up while it shows, over the first 800 ms.
	if (f.deltaShown) {
		const uint32_t from = f.kind == LeagueResultKind::Steady ? 300 : 0;
		f.delta = Lerp(0, r.delta, EaseInOut(Phase(ms, from, from + 800)));
	}
	if (!f.switched && f.kind != LeagueResultKind::Steady) f.newAlpha = 0;
	if (f.switched && f.done) {
		f.newScale = 100;
		f.newAlpha = 255;
	}
	return f;
}
