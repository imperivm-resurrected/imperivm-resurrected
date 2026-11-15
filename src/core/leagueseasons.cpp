// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leagueseasons.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <utility>

#include "i18n/tr.h"

namespace {

int Int(const Json &j, const char *key)
{
	return static_cast<int>(j.GetNumber(key));
}

int64_t Int64(const Json &j, const char *key)
{
	return static_cast<int64_t>(j.GetNumber(key));
}

std::string IdText(const Json *v)
{
	if (!v) return std::string();
	if (v->type == Json::String) return v->str;
	if (v->type == Json::Number) return std::to_string(static_cast<long long>(v->num));
	return std::string();
}

// A season's number from "id" or "season", as a number or as text; -1 when there is none.
int SeasonId(const Json &j, const char *key)
{
	const Json *v = j.Get(key);
	if (!v) return -1;
	if (v->type == Json::Number) return v->num >= 0 ? static_cast<int>(v->num) : -1;
	if (v->type == Json::String && !v->str.empty() && v->str.find_first_not_of("0123456789") == std::string::npos && v->str.size() < 7) return atoi(v->str.c_str());
	return -1;
}

}

bool LeagueParseSeasons(const Json &j, std::vector<LeagueSeasonInfo> &out)
{
	const Json *list = j.type == Json::Array ? &j : j.type == Json::Object ? j.Get("seasons") : nullptr;
	if (!list || list->type != Json::Array) return false;
	out.clear();
	for (const Json &s : list->arr) {
		if (s.type != Json::Object) continue;
		LeagueSeasonInfo info;
		info.id = SeasonId(s, "id");
		if (info.id < 0) info.id = SeasonId(s, "number");
		if (info.id < 0) continue;
		info.name = s.GetString("name");
		info.startsAt = Int64(s, "startsAt");
		info.endsAt = Int64(s, "endsAt");
		info.closed = s.GetBool("closed");
		info.beta = s.GetBool("beta");
		out.push_back(info);
	}
	return true;
}

const LeagueSeasonInfo *LeagueSeasonById(const std::vector<LeagueSeasonInfo> &seasons, int id)
{
	for (const LeagueSeasonInfo &s : seasons) {
		if (s.id == id) return &s;
	}
	return nullptr;
}

bool LeagueParseHall(const Json &j, LeagueHall &out)
{
	if (j.type != Json::Object) return false;
	const Json *top = j.Get("top");
	if (!top || top->type != Json::Array) return false;
	out = LeagueHall();
	out.season = std::max(0, SeasonId(j, "season"));
	out.name = j.GetString("name");
	for (const Json &r : top->arr) {
		if (r.type != Json::Object) continue;
		LeagueHallRow row;
		row.place = Int(r, "place");
		row.steamId = IdText(r.Get("steamId"));
		row.name = r.GetString("name");
		row.tier = LeagueTierFromJson(r.Get("tier"));
		row.division = LeagueTierHasDivisions(row.tier) ? LeagueDivisionFromJson(r.Get("division")) : 0;
		row.glory = Int(r, "glory");
		row.games = std::max(0, Int(r, "games"));
		if (row.place > 0) out.top.push_back(row);
	}
	std::sort(out.top.begin(), out.top.end(), [](const LeagueHallRow &a, const LeagueHallRow &b) { return a.place < b.place; });
	if (out.top.size() > 10) out.top.resize(10);
	return true;
}

bool LeagueParsePlayerProfile(const Json &j, LeaguePlayerProfile &out)
{
	if (j.type != Json::Object) return false;
	out = LeaguePlayerProfile();
	out.steamId = IdText(j.Get("steamId"));
	if (out.steamId.empty()) return false;
	out.name = j.GetString("name");
	out.season = std::max(0, SeasonId(j, "season"));
	out.tier = LeagueTierFromJson(j.Get("tier"));
	out.division = LeagueTierHasDivisions(out.tier) ? LeagueDivisionFromJson(j.Get("division")) : 0;
	out.glory = Int(j, "glory");
	out.place = std::max(0, Int(j, "place"));
	out.elite = j.GetBool("elite");
	out.peak = LeaguePeakFromJson(j.Get("peak"));
	out.games = std::max(0, Int(j, "games"));
	out.wins = std::max(0, Int(j, "wins"));
	out.losses = std::max(0, Int(j, "losses"));
	if (!out.games) out.games = out.wins + out.losses;
	out.streak = Int(j, "streak");
	out.avgDurationMs = static_cast<uint32_t>(std::max(0.0, j.GetNumber("avgDurationMs")));
	if (const Json *civs = j.Get("civs"); civs && civs->type == Json::Array) {
		for (const Json &c : civs->arr) {
			if (c.type != Json::Object || c.GetString("civ").empty()) continue;
			out.civs.push_back({c.GetString("civ"), std::max(0, Int(c, "games")), std::max(0, Int(c, "wins"))});
		}
		std::stable_sort(out.civs.begin(), out.civs.end(), [](const LeagueCivShare &a, const LeagueCivShare &b) { return a.games > b.games; });
		if (out.civs.size() > 3) out.civs.resize(3);
	}
	if (const Json *recent = j.Get("recent"); recent && recent->type == Json::Array) {
		for (const Json &m : recent->arr) {
			LeagueHistoryGame g;
			if (LeagueParseHistoryGame(m, g)) out.recent.push_back(g);
			if (out.recent.size() == 10) break;
		}
	}
	out.frame = LeagueFrameFromJson(j.Get("frame"));
	out.title = LeagueFrameFromJson(j.Get("title"));
	// A frame or a title of the season shown takes its rank from the card when the server only gives the season.
	for (LeagueFrame *f : {&out.frame, &out.title}) {
		if (f->Shown() && f->tier == LeagueTier::None && !f->founder && f->season == out.season) f->tier = out.tier;
	}
	return true;
}

std::string LeagueRoman(int n)
{
	if (n <= 0 || n >= 4000) return std::to_string(std::max(0, n));
	static const std::pair<int, const char *> kDigits[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
		{10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
	std::string out;
	for (const auto &d : kDigits) {
		while (n >= d.first) {
			out += d.second;
			n -= d.first;
		}
	}
	return out;
}

std::string LeagueFramePlate(const std::string &tierName, int season)
{
	// The first letter, whole: a name may start with a letter of two or more bytes.
	size_t len = tierName.empty() ? 0 : 1;
	while (len < tierName.size() && (static_cast<unsigned char>(tierName[len]) & 0xC0) == 0x80) len++;
	std::string initial = tierName.substr(0, len);
	if (len == 1) initial[0] = static_cast<char>(toupper(static_cast<unsigned char>(initial[0])));
	return (initial.empty() ? std::string("?") : initial) + " \xC2\xB7 " + LeagueRoman(season);
}

std::string LeagueSeasonTitle(LeagueTier tier, bool founder, const std::string &seasonName, int season)
{
	const std::string who = tier != LeagueTier::None ? Tr(LeagueTierName(tier)) : Tr(founder ? "Founder" : "Unranked");
	if (!seasonName.empty()) return TrFmt("%s of %s", {who, seasonName});
	return TrFmt("%s of Season %s", {who, LeagueRoman(season)});
}

std::string LeagueStreakText(int streak)
{
	if (streak > 0) return TrFmt("W%s", {std::to_string(streak)});
	if (streak < 0) return TrFmt("L%s", {std::to_string(-streak)});
	return "\xE2\x80\x94";
}

LeagueRewardPreview LeagueRewardsToday(const LeagueRanked &rk, bool testSeason)
{
	LeagueRewardPreview p;
	p.games = std::max(0, rk.wins + rk.losses);
	p.founder = testSeason;
	p.vexillum = p.games >= kLeagueVexillumGames;
	p.frame = p.games >= kLeagueFrameGames;
	p.goal = !p.vexillum ? kLeagueVexillumGames : !p.frame ? kLeagueFrameGames : 0;
	p.placing = !rk.present || rk.tier == LeagueTier::None || rk.placementLeft > 0;
	if (p.placing) return p;
	LeagueTier best = rk.tier;
	if (LeagueTierHasDivisions(rk.tier)) {
		// Having been in the elite during the season does not keep it: there, only the place at the end counts.
		LeagueTier peak = rk.peak.tier;
		if (peak != LeagueTier::None && !LeagueTierHasDivisions(peak)) peak = LeagueTier::Legate;
		if (static_cast<int>(peak) > static_cast<int>(best)) best = peak;
	}
	p.tier = best;
	if (best != LeagueTier::Imperator) {
		p.nextTier = static_cast<LeagueTier>(static_cast<int>(best) + 1);
		const int from = LeagueTierGloryFrom(p.nextTier);
		p.gloryToNext = from >= 0 ? std::max(0, from - rk.glory) : 0;
	}
	return p;
}

const LeagueReward *LeagueRewardOf(const LeagueMe &me, int season)
{
	for (const LeagueReward &r : me.rewards) {
		if (r.season == season) return &r;
	}
	return nullptr;
}

std::vector<LeagueReward> LeagueTitleRewards(const LeagueMe &me)
{
	std::vector<LeagueReward> out;
	for (const LeagueReward &r : me.rewards) {
		if (r.frame) out.push_back(r);
	}
	return out;
}

LeagueFrame LeagueFrameOfReward(const LeagueReward &r)
{
	LeagueFrame f;
	f.season = r.season;
	f.tier = r.tier;
	f.founder = r.founder && r.tier == LeagueTier::None;
	f.seasonName = r.seasonName;
	return f;
}
