// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "net/leaguepresence.h"

#include "core/league.h"
#include "core/leagueplay.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"

std::string LeagueRankedPresence()
{
	if (!LeagueLinked()) return std::string();
	const LeagueRankedPhase phase = LeagueRankedState().phase;
	const bool searching = phase == LeagueRankedPhase::Joining || phase == LeagueRankedPhase::Searching || phase == LeagueRankedPhase::Found ||
		phase == LeagueRankedPhase::Accepted;
	const char *label = LeaguePresenceLabel(LeaguePresenceOf(searching, LeagueMatchRanked()));
	if (!label) return std::string();
	std::string rank;
	const LeagueMeView me = LeagueMeState();
	if (me.loaded && me.me.ranked.present) {
		const LeagueRanked &r = me.me.ranked;
		rank = r.placementLeft > 0 || r.tier == LeagueTier::None ? Tr("In placement") : LeagueRankText(Tr(LeagueTierName(r.tier)), r.division);
	}
	return LeaguePresenceText(Tr(label), rank);
}
