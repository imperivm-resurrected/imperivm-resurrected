// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueprofile.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

uint64_t Count(const Json &j, const char *key)
{
	const double v = j.GetNumber(key);
	return v > 0 ? static_cast<uint64_t>(v + 0.5) : 0;
}

int Small(const Json &j, const char *key)
{
	return std::max(0, j.GetInt(key));
}

LeagueProfileSums SumsOf(const Json &j)
{
	LeagueProfileSums s;
	if (j.type != Json::Object) return s;
	s.games = Small(j, "games");
	s.wins = std::min(Small(j, "wins"), s.games);
	s.losses = Small(j, "losses");
	s.statsGames = std::min(Small(j, "statsGames"), s.games);
	s.kills = Count(j, "kills");
	s.deaths = Count(j, "deaths");
	s.damageDealt = Count(j, "damageDealt");
	s.damageTaken = Count(j, "damageTaken");
	s.gold = Count(j, "gold");
	s.units = Count(j, "units");
	s.army = Count(j, "army");
	s.minutes = std::max(0.0, j.GetNumber("minutes"));
	return s;
}

LeagueRanked RankedOf(const Json *r, const char *queue)
{
	LeagueRanked k;
	k.queue = queue;
	if (!r || r->type != Json::Object) return k;
	k.present = true;
	k.season = std::max(0, r->GetInt("season"));
	k.tier = LeagueTierFromJson(r->Get("tier"));
	k.division = LeagueDivisionOf(k.tier, r->Get("division"));
	k.glory = std::max(0, r->GetInt("glory"));
	k.placementLeft = std::max(0, r->GetInt("placementLeft"));
	k.wins = Small(*r, "wins");
	k.losses = Small(*r, "losses");
	k.position = Small(*r, "position");
	k.elite = !LeagueTierHasDivisions(k.tier) && k.tier != LeagueTier::None;
	k.peak.tier = LeagueTierFromJson(r->Get("peakTier"));
	k.peak.present = k.peak.tier != LeagueTier::None;
	return k;
}

void GloryOf(const Json *list, LeagueRanked &k)
{
	k.gloryKnown = true;
	if (!list || list->type != Json::Array) return;
	for (const Json &p : list->arr) {
		const Json *glory = p.type == Json::Object ? p.Get("glory") : nullptr;
		if (!glory || glory->type != Json::Number) continue;
		k.gloryHistory.push_back({p.GetInt64("at"), std::max(0, static_cast<int>(glory->num))});
	}
}

}

bool LeagueParseProfile(const Json &j, LeagueProfile &out)
{
	if (j.type != Json::Object) return false;
	out = LeagueProfile();
	out.steamId = j.GetIdText("steamId");
	out.name = j.GetString("name");
	out.avatarUrl = j.GetString("avatarUrl");
	if (const Json *p = j.Get("patron"); p && p->type == Json::Object) {
		out.patronTier = Lower(p->GetString("tier"));
		out.patronPlace = Small(*p, "place");
	}
	else if (p && p->type == Json::String) out.patronTier = Lower(p->str);
	out.badge = LeagueBadgeFromJson(j.Get("badge"));
	out.frame = LeagueFrameFromJson(j.Get("frame"));
	out.title = LeagueFrameFromJson(j.Get("title"));
	const Json *ranked = j.Get("ranked");
	const bool rankedObject = ranked && ranked->type == Json::Object;
	out.ranked = RankedOf(rankedObject ? ranked->Get("1v1") : nullptr, "1v1");
	out.ranked2v2 = RankedOf(rankedObject ? ranked->Get("2v2") : nullptr, "2v2");
	if (const Json *h = j.Get("gloryHistory"); h && h->type == Json::Object) {
		GloryOf(h->Get("1v1"), out.ranked);
		GloryOf(h->Get("2v2"), out.ranked2v2);
	}
	if (const Json *t = j.Get("totals")) out.totals = SumsOf(*t);
	if (const Json *list = j.Get("civs"); list && list->type == Json::Array) {
		for (const Json &c : list->arr) {
			if (c.type != Json::Object) continue;
			LeagueProfileCiv civ;
			civ.civ = c.GetString("civ");
			civ.sums = SumsOf(c);
			if (civ.sums.games <= 0) continue;
			out.civs.push_back(civ);
		}
		std::stable_sort(out.civs.begin(), out.civs.end(), [](const LeagueProfileCiv &a, const LeagueProfileCiv &b) {
			// The games with no civilization named go last.
			if (a.civ.empty() != b.civ.empty()) return b.civ.empty();
			return a.sums.games > b.sums.games;
		});
	}
	if (const Json *list = j.Get("recent"); list && list->type == Json::Array) {
		for (const Json &g : list->arr) {
			if (g.type != Json::Object) continue;
			LeagueProfileGame r;
			r.at = g.GetInt64("at");
			r.matchId = g.GetInt64("matchId");
			r.cup = Lower(g.GetString("kind")) == "cup";
			r.won = g.GetBool("won");
			r.queue = Lower(g.GetString("queue"));
			r.cupName = g.GetString("cup");
			r.civ = g.GetString("civ");
			r.map = g.GetString("map");
			const double ms = g.GetNumber("durationMs");
			r.durationMs = ms > 0 ? static_cast<uint32_t>(std::min(ms, 4.0e9)) : 0;
			r.kills = static_cast<uint32_t>(std::min<uint64_t>(Count(g, "kills"), 0xffffffffu));
			r.deaths = static_cast<uint32_t>(std::min<uint64_t>(Count(g, "deaths"), 0xffffffffu));
			if (const Json *opp = g.Get("opponents"); opp && opp->type == Json::Array) {
				for (const Json &o : opp->arr) {
					if (o.type == Json::Object) r.opponents.push_back({o.GetIdText("steamId"), o.GetString("name")});
				}
			}
			out.recent.push_back(r);
		}
		std::stable_sort(out.recent.begin(), out.recent.end(), [](const LeagueProfileGame &a, const LeagueProfileGame &b) { return a.at > b.at; });
	}
	if (const Json *list = j.Get("trophies"); list && list->type == Json::Array) {
		for (const Json &t : list->arr) {
			if (t.type != Json::Object) continue;
			LeagueProfileTrophy trophy;
			trophy.metal = LeagueMetalFromId(t.GetString("kind"));
			if (trophy.metal == LeagueMetal::None) trophy.metal = LeagueMetal::Gold;
			trophy.title = t.GetString("title");
			trophy.season = Small(t, "season");
			trophy.awardedAt = t.GetInt64("awardedAt");
			out.trophies.push_back(trophy);
		}
	}
	out.cupTitles = LeagueParseCupTitles(j.Get("cupTitles"));
	return true;
}

std::string LeagueProfilePath(const std::string &encodedSteamId, const std::string &lang)
{
	return "/v1/league/players/" + encodedSteamId + "/profile?lang=" + (lang.empty() ? std::string("en") : lang);
}

MatchCivRecord LeagueProfileRecord(const LeagueProfileSums &s, int race)
{
	MatchCivRecord r;
	r.race = race;
	r.games = s.games;
	r.wins = s.wins;
	r.ran = s.statsGames;
	r.winPercent = s.games > 0 ? static_cast<int>(static_cast<int64_t>(s.wins) * 100 / s.games) : 0;
	r.kills = s.kills;
	r.deaths = s.deaths;
	r.killRatio = MatchKillRatio(s.kills, s.deaths);
	if (s.statsGames > 0) {
		const double n = s.statsGames;
		r.kills1 = s.kills / n;
		r.deaths1 = s.deaths / n;
		r.damageDealt = s.damageDealt / n;
		r.damageTaken = s.damageTaken / n;
		r.gold = s.gold / n;
		r.produced = s.units / n;
		r.army = s.army / n;
	}
	if (s.games > 0) r.minutes = s.minutes / s.games;
	return r;
}
