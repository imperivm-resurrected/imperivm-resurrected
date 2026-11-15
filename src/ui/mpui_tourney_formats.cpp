// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <string>

#include "core/tournament.h"
#include "i18n/tr.h"
#include "ui/mpui_tourney_internal.h"

namespace mpui_tourney_impl {

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

std::string PointsText(int points)
{
	return points == 1 ? Tr("1 point") : TrFmt("%s points", {std::to_string(points)});
}

}
