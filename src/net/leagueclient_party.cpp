// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/leagueclient.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "net/leagueclient_internal.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

// The pace is LeaguePartyPollMs, never during a game. After a failure the next poll waits longer each time from kRetryMs, never less
// than the pace.
const DWORD kRetryMs = 15000, kRetryCapMs = 30 * 60 * 1000, kWatchMs = 3000;

struct Duo {
	bool loaded = false, polling = false, busy = false, missing = false;
	// `at`: the last poll; `askNow`: asked again at once after a request of the player; `retryAt`: the next try after `fails` in a row.
	DWORD at = 0, retryAt = 0, watchUntil = 0;
	bool askNow = false;
	int fails = 0;
	// Changes with every request of the player, so a poll that left before it does not undo what it did.
	unsigned gen = 1;
	LeaguePartyState state;
	std::vector<std::string> seenInvites;
	std::vector<LeagueDuoNotice> notices;
};

std::mutex g_mx;
Duo g_duo;

// Under g_mx.
void Notice(const std::string &text, bool bad)
{
	if (g_duo.notices.size() >= 8) g_duo.notices.erase(g_duo.notices.begin());
	g_duo.notices.push_back({text, bad});
}

// Under g_mx.
bool ScreenOpen()
{
	return static_cast<int>(g_duo.watchUntil - GetTickCount()) > 0;
}

// Under g_mx: the pace of the polls, closer while the player looks at the duo or something waits for an answer. Every linked player
// polls this in every menu, which is why someone in no duo is only asked now and then (for an invitation), and less while away.
DWORD PollDelay(bool away)
{
	const bool waiting = !g_duo.state.invites.empty() || LeagueDuoRoleOf(g_duo.state.party, OwnId()) == LeagueDuoRole::Waiting;
	return static_cast<DWORD>(LeaguePartyPollMs(g_duo.missing, waiting, ScreenOpen(), g_duo.state.party.present, away));
}

std::string NameOr(const std::string &name, const char *fallback)
{
	return name.empty() ? Tr(fallback) : name;
}

// What a refused request of the duo means, in the player's language.
std::string RefusalText(const HttpReply &r, const LeagueError &e, const std::string &friendName)
{
	if (!r.failure.empty() && r.failure == "not linked") return Tr("Link your league account to play ranked.");
	if (!r.status) return Tr(FailureText(r));
	const std::string name = NameOr(friendName, "your friend");
	if (e.code == "not_linked") return TrFmt("%s has not linked their league account yet: they link it in Multiplayer, Ranked.", {name});
	if (e.code == "already_in_party" && !e.steamId.empty() && e.steamId == OwnId()) return Tr("You already have a duo: leave it first.");
	if (e.code == "already_in_party" && !friendName.empty()) return TrFmt("%s already has a duo.", {friendName});
	if (e.code == "already_in_party") return Tr("One of you is already in a duo: leave it first.");
	if (e.code == "rank_gap" && e.max > 0) {
		return TrFmt("You are %s Glory apart in 2 vs 2: a duo can be at most %s apart.", {std::to_string(e.gap), std::to_string(e.max)});
	}
	if (e.code == "rank_gap") return Tr("You are too far apart in 2 vs 2 to play as a duo.");
	if (r.status == 404) return Tr("The invitation is no longer valid.");
	if (r.status == 429) return Tr("Too many requests: wait a few seconds and try again.");
	return Tr(FailureText(r));
}

void PollThread(unsigned gen, bool away)
{
	const HttpReply r = Http("GET", "/v1/league/party", "", true);
	Json j;
	LeaguePartyState s;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseParty(j, s);
	const std::string me = OwnId();
	std::lock_guard<std::mutex> lock(g_mx);
	g_duo.polling = false;
	g_duo.at = GetTickCount();
	if (!ok) {
		// A server before the duo has no such page: it is asked again much later.
		g_duo.missing = r.status == 404;
		g_duo.fails = g_duo.missing ? 0 : g_duo.fails + 1;
		if (g_duo.fails) g_duo.retryAt = g_duo.at + RetryDelayMs(r, g_duo.fails, PollDelay(away), kRetryMs, kRetryCapMs);
		if (!g_duo.missing && r.status != 401 && r.failure != "not linked") LogD("league", "duo: no answer (%s)", r.Describe().c_str());
		return;
	}
	g_duo.missing = false;
	g_duo.fails = 0;
	if (gen != g_duo.gen) {
		g_duo.askNow = true;
		return;
	}
	const LeagueDuoRole before = g_duo.loaded ? LeagueDuoRoleOf(g_duo.state.party, me) : LeagueDuoRole::Solo;
	const LeaguePartyMember *old = LeaguePartyPartner(g_duo.state.party, me);
	const std::string oldPartner = old ? old->name : std::string();
	const LeagueDuoRole now = LeagueDuoRoleOf(s.party, me);
	const LeaguePartyMember *partner = LeaguePartyPartner(s.party, me);
	if (g_duo.loaded && before == LeagueDuoRole::Waiting && (now == LeagueDuoRole::Leader || now == LeagueDuoRole::Member) && partner) {
		LogI("league", "duo: %s accepted", partner->name.c_str());
		Notice(TrFmt("%s accepted: you are a duo for ranked 2 vs 2.", {NameOr(partner->name, "Your friend")}), false);
	}
	else if (g_duo.loaded && before != LeagueDuoRole::Solo && now == LeagueDuoRole::Solo) {
		LogI("league", "duo: it is over (%s)", before == LeagueDuoRole::Waiting ? "the invitation was not accepted" : "the partner left");
		Notice(before == LeagueDuoRole::Waiting ? TrFmt("%s did not accept your invitation.", {NameOr(oldPartner, "Your friend")})
												: TrFmt("%s left the duo.", {NameOr(oldPartner, "Your partner")}),
			false);
	}
	for (const LeaguePartyInvite &i : LeagueNewInvites(s.invites, g_duo.seenInvites)) {
		g_duo.seenInvites.push_back(i.id);
		LogI("league", "duo: invitation from %s", i.fromName.c_str());
		Notice(TrFmt("%s invites you to play ranked 2 vs 2 as a duo. Answer in Multiplayer, Ranked.", {NameOr(i.fromName, "A friend")}), false);
	}
	if (g_duo.seenInvites.size() > 64) g_duo.seenInvites.erase(g_duo.seenInvites.begin(), g_duo.seenInvites.begin() + 32);
	g_duo.state = s;
	g_duo.loaded = true;
}

// After a request of the player: the duo is asked again at once, unless the polls are failing.
void AfterRequest()
{
	g_duo.busy = false;
	g_duo.gen++;
	g_duo.askNow = true;
}

void InviteThread(std::string steamId, std::string name)
{
	Json body = Json::MakeObject();
	body.Set("steamId", Json::MakeString(steamId));
	const HttpReply r = Http("POST", "/v1/league/party/invite", body.Dump(), true);
	Json j;
	const bool parsed = !r.body.empty() && Json::Parse(r.body, j);
	std::lock_guard<std::mutex> lock(g_mx);
	AfterRequest();
	if (!r.Ok()) {
		const LeagueError e = parsed ? LeagueParseError(j) : LeagueError();
		LogW("league", "duo: the invitation was not sent (%s)", r.Describe().c_str());
		Notice(RefusalText(r, e, name), true);
		return;
	}
	if (parsed) {
		LeaguePartyState s;
		if (LeagueParseParty(j, s) && s.party.present) g_duo.state.party = s.party;
	}
	LogI("league", "duo: invitation sent");
	Notice(TrFmt("Invitation sent to %s: they have 5 minutes to accept it.", {NameOr(name, "your friend")}), false);
}

void AnswerThread(std::string inviteId, std::string from, bool accept)
{
	const HttpReply r = Http("POST", LeaguePartyAnswerPath(UrlEncode(inviteId), accept), "", true);
	Json j;
	const bool parsed = !r.body.empty() && Json::Parse(r.body, j);
	std::lock_guard<std::mutex> lock(g_mx);
	AfterRequest();
	auto &invites = g_duo.state.invites;
	invites.erase(std::remove_if(invites.begin(), invites.end(), [&](const LeaguePartyInvite &i) { return i.id == inviteId; }), invites.end());
	if (!r.Ok()) {
		const LeagueError e = parsed ? LeagueParseError(j) : LeagueError();
		LogW("league", "duo: the invitation could not be %s (%s)", accept ? "accepted" : "declined", r.Describe().c_str());
		Notice(RefusalText(r, e, from), true);
		return;
	}
	if (accept && parsed) {
		LeaguePartyState s;
		if (LeagueParseParty(j, s) && s.party.present) g_duo.state.party = s.party;
	}
	LogI("league", "duo: invitation %s", accept ? "accepted" : "declined");
	if (accept) Notice(TrFmt("You are a duo with %s: your leader searches for both of you.", {NameOr(from, "your friend")}), false);
}

void LeaveThread()
{
	const HttpReply r = Http("DELETE", "/v1/league/party", "", true);
	std::lock_guard<std::mutex> lock(g_mx);
	AfterRequest();
	if (!r.Ok() && r.status != 404) {
		LogW("league", "duo: could not leave it (%s)", r.Describe().c_str());
		Notice(RefusalText(r, LeagueError(), std::string()), true);
		return;
	}
	g_duo.state.party = LeagueParty();
	LogI("league", "duo: left");
}

}

namespace leagueclient_impl {

void PartyTick(bool linked)
{
	const DWORD now = GetTickCount();
	std::lock_guard<std::mutex> lock(g_mx);
	if (!linked) {
		if (g_duo.loaded) {
			g_duo.loaded = false;
			g_duo.state = LeaguePartyState();
			g_duo.gen++;
		}
		g_duo.at = 0;
		g_duo.fails = 0;
		return;
	}
	if (g_duo.polling || g_duo.busy || InGame() || !SteamOwnId()) return;
	const bool away = PlayerAway();
	// The pace is worked out now, so a player back from elsewhere, or on the 2v2, is asked at once when the last poll is older.
	const bool due = g_duo.fails ? static_cast<int>(now - g_duo.retryAt) >= 0 : g_duo.askNow || !g_duo.at || now - g_duo.at >= PollDelay(away);
	if (!due) return;
	g_duo.polling = true;
	g_duo.askNow = false;
	SafeThread(PollThread, g_duo.gen, away).detach();
}

bool PartyScreenOpen()
{
	std::lock_guard<std::mutex> lock(g_mx);
	return ScreenOpen();
}

}

LeagueDuoView LeagueDuoState()
{
	LeagueDuoView v;
	const std::string me = OwnId();
	std::lock_guard<std::mutex> lock(g_mx);
	v.loaded = g_duo.loaded;
	v.busy = g_duo.busy;
	v.missing = g_duo.missing;
	v.party = g_duo.state.party;
	v.invites = g_duo.state.invites;
	v.role = LeagueDuoRoleOf(v.party, me);
	if (const LeaguePartyMember *p = LeaguePartyPartner(v.party, me)) v.partner = *p;
	return v;
}

LeagueDuoRole LeagueDuoOwnRole()
{
	const std::string me = OwnId();
	std::lock_guard<std::mutex> lock(g_mx);
	return LeagueDuoRoleOf(g_duo.state.party, me);
}

void LeagueDuoWatch()
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_duo.watchUntil = GetTickCount() + kWatchMs;
}

void LeagueDuoInvite(const std::string &steamId, const std::string &name)
{
	if (steamId.empty()) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_duo.busy) return;
		g_duo.busy = true;
	}
	LogI("league", "duo: inviting a friend");
	SafeThread(InviteThread, steamId, name).detach();
}

void LeagueDuoAnswer(const std::string &inviteId, bool accept)
{
	std::string from;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_duo.busy || inviteId.empty()) return;
		for (const LeaguePartyInvite &i : g_duo.state.invites) {
			if (i.id == inviteId) from = i.fromName;
		}
		g_duo.busy = true;
	}
	SafeThread(AnswerThread, inviteId, from, accept).detach();
}

void LeagueDuoLeave()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_duo.busy || !g_duo.state.party.present) return;
		g_duo.busy = true;
	}
	LogI("league", "duo: leaving it");
	SafeThread(LeaveThread).detach();
}

std::vector<LeagueDuoNotice> LeagueDuoTakeNotices()
{
	std::lock_guard<std::mutex> lock(g_mx);
	std::vector<LeagueDuoNotice> out;
	out.swap(g_duo.notices);
	return out;
}
