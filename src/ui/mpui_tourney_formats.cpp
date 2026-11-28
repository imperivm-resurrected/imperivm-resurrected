// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <algorithm>
#include <cstdint>
#include <string>

#include "core/tournament.h"
#include "i18n/tr.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {
namespace {

// The champion's games in a match, theirs first: "2–1".
std::string ScoreFor(const TourneyMatch &m, int64_t entry)
{
	const bool a = m.a.entryId == entry;
	return std::to_string(a ? m.scoreA : m.scoreB) + "\xE2\x80\x93" + std::to_string(a ? m.scoreB : m.scoreA);
}

// The losers' round a placement of a double elimination fell in: its final gives the 3rd place, the round before it the 4th, then two
// rounds for each range of places (5-6, 7-8, then 9-12, 13-16...).
int LosersRoundOfPlace(int placement, int losersRounds)
{
	int start = 3, round = losersRounds;
	while (round > 1 && start < placement) {
		start = start < 5 ? start + 1 : TourneyDoublePlaceEnd(start) + 1;
		round--;
	}
	return round;
}

}

std::string MatchStageText(const TourneyDetail &d, const TourneyMatch &m)
{
	const std::string round = std::to_string(m.round);
	switch (m.bracket) {
	case TourneyBracket::Swiss: return TrFmt("Round %s", {round});
	case TourneyBracket::Final: return Tr("Grand final");
	case TourneyBracket::Reset: return Tr("Reset match");
	case TourneyBracket::Losers: return m.round >= d.losersRounds ? Tr("Losers' final") : TrFmt("Losers' round %s", {round});
	default: break;
	}
	if (!TourneyIsDouble(d)) return StageText(m.stage, m.round);
	if (m.round >= d.rounds) return Tr("Winners' final");
	if (m.round == d.rounds - 1) return Tr("Winners' semifinal");
	return TrFmt("Winners' round %s", {round});
}

int EntrantPlace(const TourneyDetail &d, const TourneyEntrant &e)
{
	if (e.placement > 0 || !TourneyIsSwiss(d) || d.status != TourneyStatus::Finished) return e.placement;
	const TourneyStanding *s = TourneyStandingOf(d, e.entryId);
	return s ? s->place : 0;
}

std::string PlacementText(const TourneyDetail &d, int placement)
{
	if (placement <= 0) return std::string();
	if (placement == 1) return Tr("Champion");
	if (TourneyIsSwiss(d)) {
		if (placement == 2) return Tr("2nd place");
		if (placement == 3) return Tr("3rd place");
		const int players = std::max(placement, static_cast<int>(d.standings.size()));
		return TrFmt("Place %s of %s", {std::to_string(placement), std::to_string(players)});
	}
	if (placement == 2) return Tr("Finalist");
	const int round = LosersRoundOfPlace(placement, d.losersRounds);
	const std::string stage = round >= d.losersRounds ? Tr("Losers' final") : TrFmt("Losers' round %s", {std::to_string(round)});
	if (placement == 3) return stage + " \xC2\xB7 " + Tr("3rd place");
	if (placement == 4) return stage + " \xC2\xB7 " + Tr("4th place");
	return TrFmt("%s \xC2\xB7 places %s to %s", {stage, std::to_string(placement), std::to_string(TourneyDoublePlaceEnd(placement))});
}

std::string PointsText(int points)
{
	return points == 1 ? Tr("1 point") : TrFmt("%s points", {std::to_string(points)});
}

std::string StandingTieText(const TourneyDetail &d, const TourneyStanding &s)
{
	if (TourneyIsRoundRobin(d)) return TrFmt("Difference %s", {(s.gameDiff > 0 ? "+" : "") + std::to_string(s.gameDiff)});
	return Tr("Buchholz") + " " + std::to_string(s.buchholz);
}

std::string ChampionLine(const TourneyDetail &d, const TourneyEntrant &champ)
{
	if (TourneyIsSwiss(d)) {
		const TourneyStanding *s = TourneyStandingOf(d, champ.entryId);
		if (!s) return std::string();
		return PointsText(s->points) + " \xC2\xB7 " + std::to_string(s->wins) + "\xE2\x80\x93" + std::to_string(s->losses) + " \xC2\xB7 " + StandingTieText(d, *s);
	}
	const TourneyMatch *decider = TourneyDecider(d), *final = decider;
	if (TourneyIsDouble(d)) {
		for (const TourneyMatch &m : d.matches) {
			if (m.bracket == TourneyBracket::Final) final = &m;
		}
	}
	if (!final || !final->a.Present() || !final->b.Present()) return std::string();
	const TourneyCard &other = final->a.entryId == champ.entryId ? final->b : final->a;
	if (!TourneyIsDouble(d)) {
		if (final->status == TourneyMatchStatus::Walkover) return TrFmt("Final: %s did not come", {other.name});
		return TrFmt("Final: %s against %s", {ScoreFor(*final, champ.entryId), other.name});
	}
	if (decider && decider != final && TourneyMatchOver(decider->status)) {
		if (decider->status == TourneyMatchStatus::Walkover) return TrFmt("Grand final reset: %s did not come", {other.name});
		return TrFmt("Grand final: %s, and %s in the reset, against %s", {ScoreFor(*final, champ.entryId), ScoreFor(*decider, champ.entryId), other.name});
	}
	if (final->status == TourneyMatchStatus::Walkover) return TrFmt("Grand final: %s did not come", {other.name});
	return TrFmt("Grand final: %s against %s", {ScoreFor(*final, champ.entryId), other.name});
}

std::string SpanText(int64_t ms)
{
	if (ms < 86400000LL) return DurationText(ms);
	const int64_t hours = ms / 3600000LL;
	return TrFmt("%s d %s h", {std::to_string(hours / 24), std::to_string(hours % 24)});
}

}
