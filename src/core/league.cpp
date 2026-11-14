// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/league.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string Trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool StartsWith(const std::string &s, const char *prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}

// The host of an http(s) address, lower case and without the port.
std::string HostOf(const std::string &lowerUrl)
{
	const size_t scheme = lowerUrl.find("://");
	if (scheme == std::string::npos) return std::string();
	std::string host = lowerUrl.substr(scheme + 3);
	host = host.substr(0, host.find_first_of("/?#"));
	if (const size_t at = host.find('@'); at != std::string::npos) host = host.substr(at + 1);
	if (!host.empty() && host[0] == '[') return host.substr(0, host.find(']') + 1);
	return host.substr(0, host.find(':'));
}

bool LocalHost(const std::string &host)
{
	const std::string suffix = ".localhost";
	return host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
		(host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0);
}

bool CleanUrl(const std::string &url)
{
	for (unsigned char c : url) {
		if (c <= ' ' || c == '"' || c == '<' || c == '>' || c == '\\' || c == 0x7f) return false;
	}
	return true;
}

// Plain ASCII lower case, with the accents of the Spanish names taken off.
std::string Folded(const std::string &s)
{
	static const std::pair<const char *, char> kAccents[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}, {"\xC3\xB3", 'o'}, {"\xC3\xBA", 'u'},
		{"\xC3\x81", 'a'}, {"\xC3\x89", 'e'}, {"\xC3\x8D", 'i'}, {"\xC3\x93", 'o'}, {"\xC3\x9A", 'u'}};
	std::string out;
	for (size_t i = 0; i < s.size();) {
		bool replaced = false;
		for (const auto &a : kAccents) {
			if (s.compare(i, 2, a.first) == 0) {
				out += a.second;
				i += 2;
				replaced = true;
				break;
			}
		}
		if (replaced) continue;
		out += static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
		i++;
	}
	return Trim(out);
}

struct TierRow {
	LeagueTier tier;
	const char *id, *english, *name;
};
const TierRow kTiers[] = {
	{LeagueTier::Recruit, "recluta", "recruit", "Recruit"},
	{LeagueTier::Legionary, "legionario", "legionary", "Legionary"},
	{LeagueTier::Signifer, "signifero", "signifer", "Signifer"},
	{LeagueTier::Centurion, "centurion", "centurion", "Centurion"},
	{LeagueTier::Tribune, "tribuno", "tribune", "Tribune"},
	{LeagueTier::Legate, "legado", "legate", "Legate"},
	{LeagueTier::Praetorian, "pretoriano", "praetorian", "Praetorian"},
	{LeagueTier::Imperator, "imperator", "imperator", "Imperator"},
};

int Int(const Json &j, const char *key)
{
	return static_cast<int>(j.GetNumber(key));
}

int64_t Int64(const Json &j, const char *key)
{
	return static_cast<int64_t>(j.GetNumber(key));
}

// An id the server may send as text or as a number.
std::string IdText(const Json *v)
{
	if (!v) return std::string();
	if (v->type == Json::String) return v->str;
	if (v->type == Json::Number) return std::to_string(static_cast<long long>(v->num));
	return std::string();
}

std::string OptionValue(const std::string &text, const char *label)
{
	const std::string prefix = std::string(label) + ": ";
	for (size_t a = 0; a < text.size();) {
		size_t b = text.find('\n', a);
		if (b == std::string::npos) b = text.size();
		if (text.compare(a, prefix.size(), prefix) == 0) return Trim(text.substr(a + prefix.size(), b - a - prefix.size()));
		a = b + 1;
	}
	return std::string();
}

}

std::string LeagueBaseUrl(const std::string &configured)
{
	std::string s = Trim(configured);
	while (!s.empty() && s.back() == '/') s.pop_back();
	const std::string lower = Lower(s);
	if (!CleanUrl(s) || HostOf(lower).empty()) return kLeagueDefaultUrl;
	if (StartsWith(lower, "https://")) return s;
	if (StartsWith(lower, "http://") && LocalHost(HostOf(lower))) return s;
	return kLeagueDefaultUrl;
}

bool LeagueUrlIsDefault(const std::string &base)
{
	return Lower(base) == kLeagueDefaultUrl;
}

LeagueTier LeagueTierFromId(const std::string &id)
{
	const std::string f = Folded(id);
	for (const TierRow &r : kTiers) {
		if (f == r.id || f == r.english) return r.tier;
	}
	return LeagueTier::None;
}

LeagueTier LeagueTierFromJson(const Json *v)
{
	if (!v) return LeagueTier::None;
	if (v->type == Json::Number) {
		const int n = static_cast<int>(v->num);
		return n >= 1 && n <= kLeagueTierCount ? static_cast<LeagueTier>(n) : LeagueTier::None;
	}
	if (v->type == Json::String) {
		const std::string s = Trim(v->str);
		if (!s.empty() && isdigit(static_cast<unsigned char>(s[0]))) {
			Json n = Json::MakeNumber(atoi(s.c_str()));
			return LeagueTierFromJson(&n);
		}
		return LeagueTierFromId(s);
	}
	if (v->type == Json::Object) return LeagueTierFromJson(v->Get("id"));
	return LeagueTier::None;
}

const char *LeagueTierId(LeagueTier t)
{
	for (const TierRow &r : kTiers) {
		if (r.tier == t) return r.id;
	}
	return "";
}

const char *LeagueTierName(LeagueTier t)
{
	for (const TierRow &r : kTiers) {
		if (r.tier == t) return r.name;
	}
	return "Unranked";
}

bool LeagueTierHasDivisions(LeagueTier t)
{
	return t >= LeagueTier::Recruit && t <= LeagueTier::Legate;
}

int LeagueTierGloryFrom(LeagueTier t)
{
	if (!LeagueTierHasDivisions(t)) return -1;
	return (static_cast<int>(t) - 1) * 300;
}

int LeagueDivisionFromJson(const Json *v)
{
	if (!v) return 0;
	int n = 0;
	if (v->type == Json::Number) n = static_cast<int>(v->num);
	else if (v->type == Json::String) {
		const std::string s = Trim(v->str);
		if (s == "I" || s == "i") n = 1;
		else if (s == "II" || s == "ii") n = 2;
		else if (s == "III" || s == "iii") n = 3;
		else n = atoi(s.c_str());
	}
	return n >= 1 && n <= 3 ? n : 0;
}

std::string LeagueDivisionText(int division)
{
	switch (division) {
	case 1: return "I";
	case 2: return "II";
	case 3: return "III";
	default: return std::string();
	}
}

std::string LeagueRankText(const std::string &tierName, int division)
{
	const std::string d = LeagueDivisionText(division);
	return d.empty() ? tierName : tierName + " " + d;
}

LeagueBadge LeagueBadgeFromJson(const Json *v)
{
	LeagueBadge b;
	if (!v || v->type != Json::Object) return b;
	const std::string kind = Lower(v->GetString("kind"));
	b.id = IdText(v->Get("id"));
	b.tier = LeagueTierFromJson(v->Get("tier"));
	b.division = LeagueDivisionFromJson(v->Get("division"));
	if (kind == "rank") {
		if (b.tier == LeagueTier::None) b.tier = LeagueTierFromId(b.id);
		b.kind = b.tier == LeagueTier::None ? LeagueBadgeKind::None : LeagueBadgeKind::Rank;
	}
	else if (kind == "season") b.kind = LeagueBadgeKind::Season;
	else if (kind == "trophy") b.kind = LeagueBadgeKind::Trophy;
	if (!LeagueTierHasDivisions(b.tier)) b.division = 0;
	if (b.kind == LeagueBadgeKind::None) b = LeagueBadge();
	return b;
}

bool LeagueParsePlayersLookup(const Json &j, std::map<uint64_t, LeagueLookupEntry> &out)
{
	const Json *list = &j;
	if (const Json *players = j.Get("players"); players && players->type == Json::Object) list = players;
	if (list->type != Json::Object) return false;
	for (const auto &kv : list->obj) {
		const uint64_t id = _strtoui64(kv.first.c_str(), nullptr, 10);
		if (!id || kv.second.type != Json::Object) continue;
		LeagueLookupEntry e;
		if (const Json *p = kv.second.Get("patron"); p && p->type == Json::Object) {
			e.patronTier = p->GetString("tier");
			e.patronPlace = Int(*p, "place");
		}
		e.badge = LeagueBadgeFromJson(kv.second.Get("badge"));
		out[id] = e;
	}
	return true;
}

bool LeagueParseMe(const Json &j, LeagueMe &out)
{
	if (j.type != Json::Object) return false;
	out = LeagueMe();
	out.steamId = IdText(j.Get("steamId"));
	out.name = j.GetString("name");
	if (const Json *r = j.Get("ranked"); r && r->type == Json::Object) {
		LeagueRanked &k = out.ranked;
		k.present = true;
		k.queue = r->GetString("queue", "1v1");
		k.tier = LeagueTierFromJson(r->Get("tier"));
		k.division = LeagueTierHasDivisions(k.tier) ? LeagueDivisionFromJson(r->Get("division")) : 0;
		k.glory = Int(*r, "glory");
		k.placementLeft = Int(*r, "placementLeft");
		k.shield = Int(*r, "shield");
		k.wins = Int(*r, "wins");
		k.losses = Int(*r, "losses");
		k.position = Int(*r, "position");
		k.elite = r->GetBool("elite");
	}
	out.queueBanUntil = Int64(j, "queueBanUntil");
	out.badge = LeagueBadgeFromJson(j.Get("badge"));
	if (const Json *list = j.Get("badges"); list && list->type == Json::Array) {
		for (const Json &b : list->arr) {
			const LeagueBadge badge = LeagueBadgeFromJson(&b);
			if (badge.Shown()) out.badges.push_back(badge);
		}
	}
	return true;
}

bool LeagueParseLinkStart(const Json &j, LeagueLinkCode &out)
{
	out = LeagueLinkCode();
	out.code = j.GetString("code");
	out.pollToken = j.GetString("pollToken");
	out.url = j.GetString("url");
	out.expiresAt = Int64(j, "expiresAt");
	return !out.code.empty() && !out.pollToken.empty();
}

LeagueLinkPoll LeagueParseLinkPoll(int httpStatus, const Json &j)
{
	LeagueLinkPoll p;
	if (httpStatus == 410) {
		p.status = LeaguePollStatus::Expired;
		return p;
	}
	const std::string status = Lower(j.GetString("status"));
	if (status == "pending") p.status = LeaguePollStatus::Pending;
	else if (status == "expired") p.status = LeaguePollStatus::Expired;
	else if (status == "linked") {
		p.gameToken = j.GetString("gameToken");
		p.steamId = IdText(j.Get("steamId"));
		p.name = j.GetString("name");
		p.status = p.gameToken.empty() ? LeaguePollStatus::Unknown : LeaguePollStatus::Linked;
	}
	return p;
}

std::string LeagueCodeDisplay(const std::string &code)
{
	std::string clean;
	for (unsigned char c : code) {
		if (isalnum(c)) clean += static_cast<char>(toupper(c));
	}
	if (clean.size() == 8) return clean.substr(0, 4) + "-" + clean.substr(4);
	return clean;
}

std::string LeagueUrlDisplay(const std::string &url)
{
	std::string s = Trim(url.empty() ? std::string(kLeagueLinkPage) : url);
	if (const size_t scheme = s.find("://"); scheme != std::string::npos) s = s.substr(scheme + 3);
	if (Lower(s).compare(0, 4, "www.") == 0) s = s.substr(4);
	s = s.substr(0, s.find_first_of("?#"));
	while (!s.empty() && s.back() == '/') s.pop_back();
	return s;
}

std::string LeagueBrowserUrl(const std::string &url)
{
	const std::string s = Trim(url), lower = Lower(s);
	if (!CleanUrl(s) || HostOf(lower).empty()) return kLeagueLinkPage;
	if (StartsWith(lower, "https://")) return s;
	if (StartsWith(lower, "http://") && LocalHost(HostOf(lower))) return s;
	return kLeagueLinkPage;
}

std::string LeagueMatchKey(uint64_t lobby, int64_t startedAtMs)
{
	return std::to_string(lobby) + ":" + std::to_string(startedAtMs);
}

const char *LeagueMatchKindId(LeagueMatchKind k)
{
	switch (k) {
	case LeagueMatchKind::Ranked: return "ranked";
	case LeagueMatchKind::Tournament: return "tournament";
	default: return "normal";
	}
}

bool LeagueParseRoomTag(const std::string &tag, LeagueMatchKind &kind, int64_t &matchId)
{
	const size_t colon = tag.find(':');
	if (colon == std::string::npos) return false;
	const std::string what = Lower(Trim(tag.substr(0, colon))), number = Trim(tag.substr(colon + 1));
	if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos || number.size() > 15) return false;
	const int64_t id = _atoi64(number.c_str());
	if (id <= 0) return false;
	if (what == "ranked") kind = LeagueMatchKind::Ranked;
	else if (what == "tournament") kind = LeagueMatchKind::Tournament;
	else return false;
	matchId = id;
	return true;
}

LeagueMapFacts LeagueMapFromOptions(const std::string &optionsText, const std::string &lobbyMap)
{
	LeagueMapFacts m;
	const std::string scenario = OptionValue(optionsText, "Scenario"), type = OptionValue(optionsText, "Map type");
	m.size = OptionValue(optionsText, "Map size");
	if (!scenario.empty()) {
		m.name = scenario;
		m.type = "scenario";
	}
	else {
		m.name = !type.empty() ? type : Trim(lobbyMap);
		m.type = type;
	}
	return m;
}

Json LeagueReportJson(const LeagueReport &r)
{
	Json o = Json::MakeObject();
	o.Set("key", Json::MakeString(r.key));
	o.Set("kind", Json::MakeString(LeagueMatchKindId(r.kind)));
	if (r.kind != LeagueMatchKind::Normal && r.matchId > 0) o.Set("matchId", Json::MakeNumber(static_cast<double>(r.matchId)));
	Json map = Json::MakeObject();
	map.Set("name", Json::MakeString(r.map.name));
	map.Set("type", Json::MakeString(r.map.type));
	map.Set("size", Json::MakeString(r.map.size));
	o.Set("map", map);
	o.Set("contentKey", Json::MakeString(r.contentKey));
	o.Set("startedAt", Json::MakeNumber(static_cast<double>(r.startedAt)));
	o.Set("durationMs", Json::MakeNumber(r.durationMs));
	o.Set("endedBy", Json::MakeString(r.endedBy));
	Json players = Json::MakeArray();
	for (const LeagueReportPlayer &p : r.players) {
		Json e = Json::MakeObject();
		e.Set("steamId", Json::MakeString(p.steamId));
		e.Set("name", Json::MakeString(p.name));
		e.Set("team", Json::MakeNumber(p.team));
		e.Set("civ", Json::MakeString(p.civ));
		e.Set("result", Json::MakeString(p.result));
		players.arr.push_back(e);
	}
	o.Set("players", players);
	Json hashes = Json::MakeArray();
	for (uint32_t h : r.hashes) hashes.arr.push_back(Json::MakeNumber(h));
	o.Set("hashes", hashes);
	return o;
}

void LeagueCheckpoints::Add(uint32_t tick, uint32_t hash)
{
	if (any_ && tick < lastTick_) Clear();
	any_ = true;
	lastTick_ = tick;
	if (tick == 0 || tick % kEvery != 0) return;
	const size_t index = tick / kEvery - 1;
	if (index >= kMax) return;
	if (hashes_.size() <= index) {
		hashes_.resize(index + 1, 0);
		have_.resize(index + 1, false);
	}
	hashes_[index] = hash;
	have_[index] = true;
}

std::vector<uint32_t> LeagueCheckpoints::Hashes() const
{
	std::vector<uint32_t> out;
	for (size_t i = 0; i < hashes_.size() && have_[i]; i++) out.push_back(hashes_[i]);
	return out;
}

void LeagueCheckpoints::Clear()
{
	hashes_.clear();
	have_.clear();
	lastTick_ = 0;
	any_ = false;
}

LeagueOutcome LeagueResolve(const std::vector<LeagueSeat> &input, bool desync)
{
	std::vector<LeagueSeat> seats = input;
	LeagueOutcome out;
	auto standing = [&seats]() {
		std::set<int> teams;
		for (const LeagueSeat &s : seats) {
			if (s.state == 1 && !s.left) teams.insert(s.team);
		}
		return teams;
	};
	std::set<int> winners;
	for (const LeagueSeat &s : seats) {
		if (s.state == 3) winners.insert(s.team);
	}
	if (winners.empty() && !desync) {
		std::set<int> up = standing();
		if (up.size() != 1) {
			for (LeagueSeat &s : seats) {
				if (s.local && s.state == 1 && !s.left) {
					s.left = true;
					out.localLeft = true;
				}
			}
			up = standing();
		}
		if (up.size() == 1) winners = up;
	}
	bool anyLeft = false, loserSurrendered = false;
	for (const LeagueSeat &s : seats) {
		const bool won = !s.left && winners.count(s.team) != 0;
		out.results.push_back(s.left ? "left" : won ? "win" : "loss");
		anyLeft = anyLeft || s.left;
		loserSurrendered = loserSurrendered || (s.surrendered && !winners.count(s.team));
	}
	if (desync) out.endedBy = "disconnect";
	else if (loserSurrendered) out.endedBy = "surrender";
	else if (anyLeft || winners.empty()) out.endedBy = "disconnect";
	else out.endedBy = "victory";
	return out;
}

std::vector<LeagueReportPlayer> LeagueReportPlayers(const std::vector<LeagueSeat> &seats, const LeagueOutcome &outcome)
{
	std::vector<size_t> order;
	std::set<int> teams;
	for (size_t i = 0; i < seats.size(); i++) {
		if (!seats[i].steamId) continue;
		order.push_back(i);
		teams.insert(seats[i].team);
	}
	std::sort(order.begin(), order.end(), [&seats](size_t a, size_t b) { return seats[a].index < seats[b].index; });
	std::vector<LeagueReportPlayer> out;
	for (size_t i : order) {
		const LeagueSeat &s = seats[i];
		LeagueReportPlayer p;
		p.steamId = std::to_string(s.steamId);
		p.name = s.name;
		p.civ = s.civ;
		p.team = static_cast<int>(std::distance(teams.begin(), teams.find(s.team))) + 1;
		p.result = i < outcome.results.size() ? outcome.results[i] : "loss";
		out.push_back(p);
	}
	return out;
}

std::string LeagueQueueEncode(const std::vector<LeagueQueuedReport> &queue)
{
	Json list = Json::MakeArray();
	for (const LeagueQueuedReport &q : queue) {
		Json e = Json::MakeObject();
		e.Set("steamId", Json::MakeString(q.steamId));
		e.Set("tries", Json::MakeNumber(q.tries));
		e.Set("createdAt", Json::MakeNumber(static_cast<double>(q.createdAt)));
		e.Set("nextAt", Json::MakeNumber(static_cast<double>(q.nextAt)));
		e.Set("report", q.body);
		list.arr.push_back(e);
	}
	return list.Dump(1);
}

std::vector<LeagueQueuedReport> LeagueQueueDecode(const std::string &text)
{
	std::vector<LeagueQueuedReport> out;
	Json list;
	if (!Json::Parse(text, list) || list.type != Json::Array) return out;
	for (const Json &e : list.arr) {
		const Json *body = e.Get("report");
		if (e.type != Json::Object || !body || body->type != Json::Object || body->GetString("key").empty()) continue;
		LeagueQueuedReport q;
		q.steamId = e.GetString("steamId");
		q.tries = static_cast<int>(e.GetNumber("tries"));
		q.createdAt = static_cast<int64_t>(e.GetNumber("createdAt"));
		q.nextAt = static_cast<int64_t>(e.GetNumber("nextAt"));
		q.body = *body;
		if (!q.steamId.empty()) out.push_back(q);
	}
	return out;
}

void LeagueQueueAdd(std::vector<LeagueQueuedReport> &queue, const LeagueQueuedReport &item)
{
	const std::string key = item.body.GetString("key");
	queue.erase(std::remove_if(queue.begin(), queue.end(),
					[&](const LeagueQueuedReport &q) { return q.steamId == item.steamId && q.body.GetString("key") == key; }),
		queue.end());
	queue.push_back(item);
	if (queue.size() > kLeagueQueueMax) queue.erase(queue.begin(), queue.begin() + (queue.size() - kLeagueQueueMax));
}

size_t LeagueQueuePrune(std::vector<LeagueQueuedReport> &queue, int64_t nowMs)
{
	const size_t before = queue.size();
	queue.erase(std::remove_if(queue.begin(), queue.end(), [nowMs](const LeagueQueuedReport &q) { return nowMs - q.createdAt > kLeagueQueueKeepMs; }),
		queue.end());
	return before - queue.size();
}

int64_t LeagueRetryDelayMs(int tries)
{
	const int64_t kFirst = 30000, kLongest = 30 * 60 * 1000;
	const int shift = std::max(0, std::min(tries - 1, 10));
	return std::min(kLongest, kFirst << shift);
}
