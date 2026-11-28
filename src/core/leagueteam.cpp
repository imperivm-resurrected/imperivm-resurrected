// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueteam.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

const char kVotePrefix[] = "#IRV:";
const size_t kVotePrefixLen = sizeof(kVotePrefix) - 1;
const char kGonePrefix[] = "#IRG:";
const size_t kGonePrefixLen = sizeof(kGonePrefix) - 1;

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

}

std::string LeagueQueueId(const std::string &queue)
{
	return Lower(queue) == kLeagueQueueTeam ? kLeagueQueueTeam : kLeagueQueueDuel;
}

bool LeagueQueueIsTeam(const std::string &queue)
{
	return Lower(queue) == kLeagueQueueTeam;
}

const LeagueRanked &LeagueMeRanked(const LeagueMe &me, const std::string &queue)
{
	return LeagueQueueIsTeam(queue) ? me.ranked2v2 : me.ranked;
}

LeagueParty LeaguePartyOf(const Json *v)
{
	LeagueParty p;
	if (!v || v->type != Json::Object) return p;
	p.id = v->GetIdText("id");
	p.leader = v->GetIdText("leader");
	p.expiresAt = v->GetInt64("expiresAt");
	if (const Json *list = v->Get("members"); list && list->type == Json::Array) {
		for (const Json &m : list->arr) {
			if (m.type != Json::Object) continue;
			LeaguePartyMember member;
			member.steamId = m.GetIdText("steamId");
			member.name = m.GetString("name");
			member.accepted = m.GetBool("accepted");
			if (!member.steamId.empty()) p.members.push_back(member);
		}
	}
	p.present = !p.id.empty();
	return p;
}

bool LeagueParseParty(const Json &j, LeaguePartyState &out)
{
	if (j.type != Json::Object) return false;
	out = LeaguePartyState();
	out.party = LeaguePartyOf(j.Get("party"));
	if (const Json *list = j.Get("invites"); list && list->type == Json::Array) {
		for (const Json &i : list->arr) {
			if (i.type != Json::Object) continue;
			LeaguePartyInvite invite;
			invite.id = i.GetIdText("id");
			if (const Json *from = i.Get("from"); from && from->type == Json::Object) {
				invite.fromId = from->GetIdText("steamId");
				invite.fromName = from->GetString("name");
			}
			invite.expiresAt = i.GetInt64("expiresAt");
			if (!invite.id.empty()) out.invites.push_back(invite);
		}
	}
	return true;
}

const LeaguePartyMember *LeaguePartyPartner(const LeagueParty &p, const std::string &me)
{
	if (!p.present) return nullptr;
	for (const LeaguePartyMember &m : p.members) {
		if (m.steamId != me) return &m;
	}
	return nullptr;
}

bool LeaguePartyReady(const LeagueParty &p)
{
	if (!p.present || p.members.size() < 2) return false;
	return std::all_of(p.members.begin(), p.members.end(), [](const LeaguePartyMember &m) { return m.accepted; });
}

LeagueDuoRole LeagueDuoRoleOf(const LeagueParty &p, const std::string &me)
{
	if (!p.present || !LeaguePartyPartner(p, me)) return LeagueDuoRole::Solo;
	if (!LeaguePartyReady(p)) return LeagueDuoRole::Waiting;
	return p.leader == me ? LeagueDuoRole::Leader : LeagueDuoRole::Member;
}

std::vector<LeaguePartyInvite> LeagueNewInvites(const std::vector<LeaguePartyInvite> &now, const std::vector<std::string> &seen)
{
	std::vector<LeaguePartyInvite> out;
	for (const LeaguePartyInvite &i : now) {
		if (std::find(seen.begin(), seen.end(), i.id) == seen.end()) out.push_back(i);
	}
	return out;
}

int LeaguePartyPollMs(bool missing, bool waiting, bool screen, bool inDuo, bool away)
{
	if (missing) return 600000;
	if (away) return waiting ? 60000 : inDuo ? 300000 : 600000;
	if (waiting || screen) return 5000;
	return inDuo ? 60000 : 120000;
}

int LeagueDuoQueuePollMs(bool screen, bool inFront)
{
	if (!inFront) return 15000;
	return screen ? 5000 : 10000;
}

std::string LeagueBoardPath(const std::string &queue)
{
	return "/v1/league/leaderboard?queue=" + LeagueQueueId(queue);
}

std::string LeagueHistoryPath(int64_t before, int limit, const std::string &queue)
{
	std::string path = "/v1/league/matches?mine=1&limit=" + std::to_string(limit);
	if (before > 0) path += "&before=" + std::to_string(static_cast<long long>(before));
	if (LeagueQueueIsTeam(queue)) path += "&queue=2v2";
	return path;
}

std::string LeaguePlayerPath(const std::string &steamId, int season, const std::string &queue)
{
	std::string query;
	if (season >= 0) query = "season=" + std::to_string(season);
	if (LeagueQueueIsTeam(queue)) query += (query.empty() ? "" : "&") + std::string("queue=2v2");
	return "/v1/league/players/" + steamId + (query.empty() ? "" : "?" + query);
}

std::string LeagueHallPath(int season, const std::string &lang, const std::string &queue)
{
	return "/v1/league/seasons/" + std::to_string(season) + "/hall?lang=" + lang + (LeagueQueueIsTeam(queue) ? "&queue=2v2" : "");
}

std::string LeagueCeremonySeenPath(int season, const std::string &queue)
{
	return "/v1/league/me/ceremony/" + std::to_string(season) + "/seen" + (LeagueQueueIsTeam(queue) ? "?queue=2v2" : "");
}

std::string LeaguePartyAnswerPath(const std::string &inviteId, bool accept)
{
	return "/v1/league/party/" + inviteId + (accept ? "/accept" : "/decline");
}

std::vector<LeagueMatchTeam> LeagueTicketTeams(const LeagueMatchTicket &t)
{
	if (!t.teams.empty()) return t.teams;
	std::vector<LeagueMatchTeam> out;
	for (int side = 1; side <= 2; side++) {
		LeagueMatchTeam team;
		team.side = side;
		for (const LeaguePlayerCard &p : t.players) {
			if (p.team == side) team.players.push_back(p);
		}
		if (!team.players.empty()) out.push_back(team);
	}
	return out;
}

int LeagueSideOf(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId)
{
	for (const LeagueMatchTeam &t : teams) {
		for (const LeaguePlayerCard &p : t.players) {
			if (!steamId.empty() && p.steamId == steamId) return t.side;
		}
	}
	return 0;
}

const LeaguePlayerCard *LeagueTicketCard(const LeagueMatchTicket &t, const std::string &steamId)
{
	if (steamId.empty()) return nullptr;
	for (const LeaguePlayerCard &p : t.players) {
		if (p.steamId == steamId) return &p;
	}
	return nullptr;
}

int LeagueAcceptedCount(const LeagueMatchTicket &t)
{
	return static_cast<int>(std::count_if(t.players.begin(), t.players.end(), [](const LeaguePlayerCard &p) { return p.accepted; }));
}

const LeaguePlayerCard *LeagueTeamPartner(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId)
{
	const int side = LeagueSideOf(teams, steamId);
	for (const LeagueMatchTeam &t : teams) {
		if (t.side != side) continue;
		for (const LeaguePlayerCard &p : t.players) {
			if (p.steamId != steamId) return &p;
		}
	}
	return nullptr;
}

std::vector<LeaguePlayerCard> LeagueTeamOpponents(const std::vector<LeagueMatchTeam> &teams, const std::string &steamId)
{
	std::vector<LeaguePlayerCard> out;
	const int side = LeagueSideOf(teams, steamId);
	for (const LeagueMatchTeam &t : teams) {
		if (t.side != side) out.insert(out.end(), t.players.begin(), t.players.end());
	}
	return out;
}

bool LeagueTeamDropOver(uint32_t leftAtMs, uint32_t clockMs)
{
	return clockMs >= leftAtMs && clockMs - leftAtMs >= kLeagueTeamDropWaitMs;
}

uint32_t LeagueTeamDropLeftMs(uint32_t leftAtMs, uint32_t clockMs)
{
	if (clockMs < leftAtMs) return kLeagueTeamDropWaitMs;
	const uint32_t gone = clockMs - leftAtMs;
	return gone >= kLeagueTeamDropWaitMs ? 0 : kLeagueTeamDropWaitMs - gone;
}

LeagueSurrenderStep LeagueSurrenderOnPress(uint32_t gameMs, bool partnerHere, bool voteOpen, bool ranked)
{
	// A cup game can be given up from the start, as a cup's 1v1.
	if (!LeagueSurrenderAllowed(ranked, gameMs)) return LeagueSurrenderStep::TooEarly;
	if (!partnerHere) return LeagueSurrenderStep::Alone;
	return voteOpen ? LeagueSurrenderStep::Pending : LeagueSurrenderStep::Propose;
}

void LeagueVoteOpen(LeagueVote &v, int proposer, int64_t nowMs)
{
	v.open = true;
	v.proposer = proposer;
	v.since = nowMs;
}

int64_t LeagueVoteLeftMs(const LeagueVote &v, int64_t nowMs)
{
	if (!v.open) return 0;
	return std::max<int64_t>(0, kLeagueVoteMs - (nowMs - v.since));
}

bool LeagueVoteExpired(const LeagueVote &v, int64_t nowMs)
{
	return v.open && nowMs - v.since >= kLeagueVoteMs;
}

bool LeagueVoteAnswerCounts(const LeagueVote &v, int64_t atMs)
{
	return v.open && atMs - v.since < kLeagueVoteMs;
}

std::string LeagueVoteMessage(LeagueVoteVerb verb)
{
	const char letter = verb == LeagueVoteVerb::Propose ? 'p' : verb == LeagueVoteVerb::Yes ? 'y' : verb == LeagueVoteVerb::No ? 'n' : 0;
	return letter ? std::string(kVotePrefix) + letter : std::string();
}

LeagueVoteVerb LeagueVoteParse(const char *text, unsigned size)
{
	if (!text || size != kVotePrefixLen + 1 || memcmp(text, kVotePrefix, kVotePrefixLen) != 0) return LeagueVoteVerb::None;
	switch (text[kVotePrefixLen]) {
	case 'p': return LeagueVoteVerb::Propose;
	case 'y': return LeagueVoteVerb::Yes;
	case 'n': return LeagueVoteVerb::No;
	default: return LeagueVoteVerb::None;
	}
}

std::string LeagueGoneMessage(int player)
{
	if (player < 0 || player > 7) return std::string();
	return std::string(kGonePrefix) + static_cast<char>('0' + player);
}

int LeagueGoneParse(const char *text, unsigned size)
{
	if (!text || size != kGonePrefixLen + 1 || memcmp(text, kGonePrefix, kGonePrefixLen) != 0) return -1;
	const char c = text[kGonePrefixLen];
	return c >= '0' && c <= '7' ? c - '0' : -1;
}
