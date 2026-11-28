// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "net/leagueclient.h"
#include "addr/engine.h"
#include "addr/net.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "core/leagueranked.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "core/uiport.h"
#include "i18n/tr.h"
#include "net/leagueclient_internal.h"
#include "net/netdrop.h"

using namespace leagueclient_impl;

namespace {

typedef void *(__cdecl *Create_t)();
typedef void *(__fastcall *Assign_t)(void *self, void *edx, const char *text, unsigned len);
typedef void(__fastcall *ThisPtr_t)(void *self, void *edx, void *arg);
typedef void(__cdecl *Void_t)();

// The game's current player, in its list of players, and its clock in ms (CVXGame+0x12c8 and +0x1258).
const size_t kGameCurrentPlayer = 0x12c8, kGameClock = 0x1258;

// A verb of the vote as the game ran it: who sent it and the game's time then, the same on every computer.
struct Verb {
	int from = -1;
	LeagueVoteVerb verb = LeagueVoteVerb::None;
	uint32_t at = 0;
};

// The vote as this computer saw it, and what it still has to do on the game thread.
struct Vote {
	LeagueVote vote;
	int64_t matchId = 0;
	// The partner said yes: this computer surrenders its own player on the next tick.
	bool surrenderDue = false;
	// The verbs read from the game's messages, for the game thread.
	std::vector<Verb> inbox;
	// The players whose drop and return were told, and the abandons told.
	std::vector<int> dropsTold, gonesTold;
	// The game given to this side by the drop rules: when the defeat of each player gone was last asked of the game (GetTickCount).
	std::map<int, DWORD> goneAsked;
};

// The defeat of a player gone is asked again after this while the game has not run it.
const DWORD kGoneAskAgainMs = 10000;

std::mutex g_mx;
Vote g_vote;

// The game's clock: the vote's 30 s run on it, so every computer closes it at the same moment of the game.
int64_t GameClock()
{
	const char *game = *reinterpret_cast<char *const *>(addr::kGame);
	return game ? *reinterpret_cast<const uint32_t *>(game + kGameClock) : 0;
}

const TeamSeat *Seat(const TeamGame &g, int index)
{
	for (const TeamSeat &s : g.seats) {
		if (s.index == index) return &s;
	}
	return nullptr;
}

const TeamSeat *Local(const TeamGame &g)
{
	for (const TeamSeat &s : g.seats) {
		if (s.local) return &s;
	}
	return nullptr;
}

// The other human of this computer's side, or null.
const TeamSeat *Partner(const TeamGame &g)
{
	const TeamSeat *me = Local(g);
	if (!me) return nullptr;
	for (const TeamSeat &s : g.seats) {
		if (!s.local && s.team == me->team) return &s;
	}
	return nullptr;
}

// The partner can vote: they are in the game, not played by the AI and not gone for good.
bool PartnerHere(const TeamGame &g)
{
	const TeamSeat *p = Partner(g);
	return p && p->connected && !p->gone;
}

std::string NameOf(const TeamSeat *s, const char *fallback)
{
	return s && !s->name.empty() ? s->name : Tr(fallback);
}

// A message command of this computer's player, which every computer runs on the same tick.
bool QueueMessage(const std::string &text)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	void *player = game ? *reinterpret_cast<void **>(game + kGameCurrentPlayer) : nullptr;
	if (!player || text.empty()) return false;
	void *cmd = reinterpret_cast<Create_t>(addr::kCmdMessageCreate)();
	if (!cmd) return false;
	reinterpret_cast<Assign_t>(addr::kStringAssign)(static_cast<char *>(cmd) + 0xc, nullptr, text.c_str(), static_cast<unsigned>(text.size()));
	void **vt = *reinterpret_cast<void ***>(cmd);
	reinterpret_cast<ThisPtr_t>(vt[3])(cmd, nullptr, player);
	reinterpret_cast<ThisPtr_t>(addr::kQueueCommand)(game, nullptr, cmd);
	return true;
}

bool QueueVote(LeagueVoteVerb verb)
{
	return QueueMessage(LeagueVoteMessage(verb));
}

// The team surrenders on this computer: its own player, as the game menu's Surrender does once confirmed.
void SurrenderNow()
{
	const std::string message = Tr("Your team has surrendered.");
	reinterpret_cast<Assign_t>(addr::kStringAssign)(reinterpret_cast<void *>(addr::kEndGameMessage), nullptr, message.c_str(), static_cast<unsigned>(message.size()));
	reinterpret_cast<Void_t>(addr::kQueueSurrenderCommand)();
}

// Under g_mx: a verb read from the game. Whether an answer counts goes by the game's time it ran at, so the two of the team agree.
void Read(const TeamGame &g, const Verb &m)
{
	const int from = m.from;
	const TeamSeat *me = Local(g), *sender = Seat(g, from);
	if (!me || !sender || sender->team != me->team) return;
	LeagueVote &v = g_vote.vote;
	switch (m.verb) {
	case LeagueVoteVerb::Propose:
		if (v.open && !LeagueVoteExpired(v, m.at)) return;
		LeagueVoteOpen(v, from, m.at);
		LogI("league", "ranked 2v2: player %d proposes to surrender", from);
		break;
	case LeagueVoteVerb::Yes:
		if (!LeagueVoteAnswerCounts(v, m.at) || from == v.proposer) return;
		v = LeagueVote();
		g_vote.surrenderDue = true;
		LogI("league", "ranked 2v2: the team agreed to surrender");
		break;
	case LeagueVoteVerb::No:
		if (!LeagueVoteAnswerCounts(v, m.at) || from == v.proposer) return;
		v = LeagueVote();
		LogI("league", "ranked 2v2: the surrender was voted down");
		if (!sender->local) UiToast(TrFmt("%s does not want to surrender: the game goes on.", {NameOf(sender, "Your partner")}));
		break;
	default: break;
	}
}

}

namespace leagueclient_impl {

void TeamTick()
{
	TeamGame g;
	const bool team = MatchTeamGame(g);
	const int64_t now = GameClock();
	std::vector<std::string> toasts;
	std::vector<int> defeats;
	bool surrender = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!team || g.matchId != g_vote.matchId) {
			const int64_t id = team ? g.matchId : 0;
			if (g_vote.matchId || g_vote.vote.open || !g_vote.inbox.empty()) g_vote = Vote();
			g_vote.matchId = id;
			if (!team) return;
		}
		for (const Verb &m : g_vote.inbox) Read(g, m);
		g_vote.inbox.clear();
		LeagueVote &v = g_vote.vote;
		const TeamSeat *me = Local(g);
		// Only the game's time closes it: an answer on its way still counts on both computers.
		if (v.open && (g.decided || LeagueVoteExpired(v, now))) {
			if (me && v.proposer == me->index && !g.decided) toasts.push_back(TrFmt("%s did not answer: your team does not surrender.", {NameOf(Partner(g), "Your partner")}));
			v = LeagueVote();
		}
		if (g_vote.surrenderDue) {
			g_vote.surrenderDue = false;
			surrender = !g.decided && me && !me->gone;
		}
		// The drops of the game and what came of them, once each.
		for (const TeamSeat &s : g.seats) {
			if (s.local) continue;
			auto told = [](std::vector<int> &list, int index) {
				if (std::find(list.begin(), list.end(), index) != list.end()) return true;
				list.push_back(index);
				return false;
			};
			if (s.gone && !s.quit && !told(g_vote.gonesTold, s.index)) {
				// A 1v1's opponent gone for good leaves the game to this player.
				if (g.team) toasts.push_back(TrFmt("%s did not come back: they left the game, and the AI keeps their army.", {NameOf(&s, "A player")}));
				else toasts.push_back(TrFmt("%s did not come back in 5 minutes: you win the game.", {NameOf(&s, "Your opponent")}));
			}
			// The game is this side's: the game itself defeats each player of the other side gone for good, so it ends with this side's
			// victory and its own end screen, on every computer at the same tick. Asked again while the game has not run it.
			const DWORD tick = GetTickCount();
			if (g.dropWon && me && s.team != me->team && s.gone) {
				auto asked = g_vote.goneAsked.find(s.index);
				if (asked == g_vote.goneAsked.end() || tick - asked->second >= kGoneAskAgainMs) {
					g_vote.goneAsked[s.index] = tick;
					defeats.push_back(s.index);
				}
			}
			if (!s.connected && !s.gone) told(g_vote.dropsTold, s.index);
			else if (s.connected && !s.gone) {
				auto it = std::find(g_vote.dropsTold.begin(), g_vote.dropsTold.end(), s.index);
				if (it != g_vote.dropsTold.end()) {
					g_vote.dropsTold.erase(it);
					toasts.push_back(TrFmt("%s is back in the game.", {NameOf(&s, "A player")}));
				}
			}
		}
	}
	for (const std::string &t : toasts) UiToast(t);
	for (int index : defeats) {
		// The game's own count of the 5 minutes (from the drop notice, the same everywhere) may end a moment after this computer's.
		if (!NetDropGoneDue(index)) {
			std::lock_guard<std::mutex> lock(g_mx);
			g_vote.goneAsked.erase(index);
			continue;
		}
		if (QueueMessage(LeagueGoneMessage(index))) LogI("league", "player %d left the game for good: the game ends with this side's victory", index);
	}
	if (surrender) {
		LogI("league", "ranked 2v2: surrendering with the partner's yes");
		SurrenderNow();
	}
}

}

bool LeagueVoteCommand(void *cmd, const char *text, unsigned size)
{
	const LeagueVoteVerb verb = LeagueVoteParse(text, size);
	if (verb == LeagueVoteVerb::None) return false;
	char *who = cmd ? *reinterpret_cast<char **>(static_cast<char *>(cmd) + 4) : nullptr;
	Verb m;
	m.from = who ? *reinterpret_cast<int *>(who + 8) : -1;
	m.verb = verb;
	m.at = static_cast<uint32_t>(GameClock());
	if (m.from < 0 || m.from > 7) return true;
	std::lock_guard<std::mutex> lock(g_mx);
	if (g_vote.inbox.size() < 16) g_vote.inbox.push_back(m);
	return true;
}

LeagueSurrenderAsk LeagueSurrenderPress()
{
	TeamGame g;
	if (!MatchTeamGame(g) || g.decided || !g.team) return LeagueSurrenderAsk::NotTeam;
	LeagueSurrenderStep step = LeagueSurrenderStep::TooEarly;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		step = LeagueSurrenderOnPress(g.clockMs, PartnerHere(g), g_vote.vote.open, g.ranked);
	}
	switch (step) {
	case LeagueSurrenderStep::TooEarly: return LeagueSurrenderAsk::TooEarly;
	case LeagueSurrenderStep::Alone:
		LogI("league", "ranked 2v2: surrendering without the partner, who is not in the game");
		return LeagueSurrenderAsk::Alone;
	case LeagueSurrenderStep::Pending: return LeagueSurrenderAsk::Pending;
	default: break;
	}
	if (!QueueVote(LeagueVoteVerb::Propose)) return LeagueSurrenderAsk::NotTeam;
	LogI("league", "ranked 2v2: proposing to surrender to the partner");
	return LeagueSurrenderAsk::Proposed;
}

void LeagueVoteAnswer(bool yes)
{
	TeamGame g;
	if (!MatchTeamGame(g) || !g.team) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const TeamSeat *me = Local(g);
		if (!g_vote.vote.open || !me || g_vote.vote.proposer == me->index) return;
	}
	QueueVote(yes ? LeagueVoteVerb::Yes : LeagueVoteVerb::No);
	LogI("league", "ranked 2v2: answering the proposal to surrender: %s", yes ? "yes" : "no");
}

LeagueTeamView LeagueTeamState()
{
	LeagueTeamView v;
	TeamGame g;
	if (!MatchTeamGame(g) || g.decided) return v;
	v.active = true;
	const TeamSeat *me = Local(g), *partner = Partner(g);
	v.partner = NameOf(partner, "Your partner");
	for (const TeamSeat &s : g.seats) {
		if (s.local || s.connected || s.gone) continue;
		LeagueTeamDrop d;
		d.name = NameOf(&s, "A player");
		d.partner = me && s.team == me->team;
		d.leftMs = LeagueTeamDropLeftMs(s.leftAt, g.clockMs);
		v.drops.push_back(d);
	}
	std::lock_guard<std::mutex> lock(g_mx);
	const LeagueVote &vote = g_vote.vote;
	if (vote.open && me && g.team) {
		v.vote = vote.proposer == me->index ? LeagueTeamVote::Proposed : LeagueTeamVote::Asked;
		v.voteLeftMs = LeagueVoteLeftMs(vote, GameClock());
	}
	return v;
}
