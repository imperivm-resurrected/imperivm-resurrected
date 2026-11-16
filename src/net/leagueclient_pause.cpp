// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>

#include "addr/state.h"
#include "core/leagueplay.h"
#include "core/leagueranked.h"
#include "core/log.h"
#include "core/uiport.h"
#include "hooks/tweaks.h"
#include "i18n/tr.h"
#include "net/leagueclient_internal.h"

using namespace leagueclient_impl;

namespace {

// The game pauses once every player has the tick that carries the request to: a tick or two, of up to 2 s each.
const int64_t kAnswerMs = 6000;
// The request to resume when this player's time is over goes again if the game is still paused this long after.
const int64_t kLiftAgainMs = 10000;

// A ranked game's pauses as this computer counts them: only its own player's (2-clasificatoria-sistema.md §2.3); the other side counts
// theirs.
struct Pauses {
	int64_t matchId = 0;
	bool ranked = false, held = false, over = false;
	bool known = false;
	LeaguePauseBudget budget;
	// When this player asked to pause, and when their run-out pause was last asked to go.
	int64_t askedAt = 0, liftedAt = 0;
	bool wasPaused = false;
};

std::mutex g_mx;
Pauses g_p;

long long Seconds(int64_t ms)
{
	return static_cast<long long>(ms / 1000);
}

// From the game's hooks, on the game thread: whether this player's request to pause goes out.
bool MayPause()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!g_p.ranked || g_p.held || g_p.over) return true;
		if (LeaguePauseCanStart(g_p.budget)) {
			g_p.askedAt = TickMs();
			return true;
		}
		LogI("league", "ranked: pause refused, none left for this player (%d of %d taken, %lld of %lld s)", g_p.budget.used, g_p.budget.count,
			Seconds(g_p.budget.usedMs), Seconds(g_p.budget.totalMs));
	}
	UiToast("You have no pauses left in this game.");
	return false;
}

}

namespace leagueclient_impl {

void PauseTick(bool ranked, int64_t matchId, bool held, bool over)
{
	static bool gated = false;
	if (ranked && !gated) {
		gated = true;
		TweaksSetPauseGate(&MayPause);
	}
	void *game = *reinterpret_cast<void **>(addr::kGame);
	const bool paused = ranked && game && TweaksGamePaused(game);
	const int64_t now = TickMs();
	std::string toast;
	bool lift = false;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (!ranked || matchId != g_p.matchId) {
			if (g_p.ranked && g_p.budget.used) {
				LogI("league", "ranked: this player paused %d times, %lld s in all", g_p.budget.used, Seconds(g_p.budget.usedMs + (g_p.budget.active ? now - g_p.budget.since : 0)));
			}
			g_p = Pauses();
			g_p.ranked = ranked;
			g_p.matchId = ranked ? matchId : 0;
			g_p.wasPaused = paused;
			if (!ranked) return;
		}
		Pauses &p = g_p;
		LeaguePauseBudget &b = p.budget;
		if (!p.known) {
			LeagueMatchTicket t;
			if (RankedTicketOf(matchId, t)) {
				const LeaguePauseBudget rules = LeaguePauseBudgetOf(t.settings);
				b.count = rules.count;
				b.totalMs = rules.totalMs;
				p.known = true;
				LogI("league", "ranked: each player may pause %d times, %lld s in all", b.count, Seconds(b.totalMs));
			}
		}
		p.held = held;
		p.over = over;
		if (held || over) {
			// The wait for an opponent who dropped holds the pause for the league, never for a player; a decided game counts no more.
			if (b.active) {
				LeaguePauseEnded(b, now);
				LogI("league", "ranked: this player's pause stops counting (%lld s left)", Seconds(LeaguePauseLeftMs(b, now)));
			}
			p.askedAt = 0;
		}
		else if (paused && !p.wasPaused) {
			// A pause nobody here asked for is the other player's: they count it on their side.
			if (p.askedAt && now - p.askedAt < kAnswerMs) {
				LeaguePauseStarted(b, now);
				p.liftedAt = 0;
				LogI("league", "ranked: this player paused the game (%d of %d), %lld s of pause left", b.used, b.count, Seconds(LeaguePauseLeftMs(b, now)));
				toast = TrFmt("Pause %s of %s: %s left", {std::to_string(b.used), std::to_string(b.count), LeagueClockText(LeaguePauseLeftMs(b, now))});
			}
			p.askedAt = 0;
		}
		else if (!paused && p.wasPaused && b.active) {
			LeaguePauseEnded(b, now);
			LogI("league", "ranked: the game goes on; %lld s of pause left for this player", Seconds(LeaguePauseLeftMs(b, now)));
		}
		if (p.askedAt && now - p.askedAt >= kAnswerMs) p.askedAt = 0;
		if (paused && !held && !over && LeaguePauseRunOut(b, now) && (!p.liftedAt || now - p.liftedAt >= kLiftAgainMs)) {
			if (!p.liftedAt) toast = Tr("Your pause time is over: the game goes on.");
			p.liftedAt = now;
			lift = true;
			LogI("league", "ranked: this player's pause time is over; resuming the game");
		}
		p.wasPaused = paused;
	}
	if (lift) TweaksRequestUnpause(game);
	if (!toast.empty()) UiToast(toast);
}

}
