// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "core/json.h"
#include "core/leaguemods.h"
#include "core/leagueranked.h"
#include "core/tournament.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

LeagueModRef Ref(uint64_t gbId, uint64_t fileId)
{
	LeagueModRef r;
	r.gbId = gbId;
	r.fileId = fileId;
	return r;
}

int FitOf(const LeagueModsRule &rule, const std::vector<LeagueModRef> &own)
{
	return static_cast<int>(LeagueModsCompare(rule, own).fit);
}

const int kOk = static_cast<int>(LeagueModsFit::Ok), kMissing = static_cast<int>(LeagueModsFit::Missing),
		  kUnknown = static_cast<int>(LeagueModsFit::NotFromGameBanana), kExtra = static_cast<int>(LeagueModsFit::Extra);

void ParseRule()
{
	const Json free = Parse(R"({"mods":null})");
	CHECK(!LeagueParseModsRule(free.Get("mods")).set);
	CHECK(!LeagueParseModsRule(nullptr).set);
	const Json base = Parse(R"({"mods":[]})");
	const LeagueModsRule b = LeagueParseModsRule(base.Get("mods"));
	CHECK(b.set);
	CHECK(b.BaseGame());
	const Json list = Parse(R"({"mods":[{"gbId":728851,"fileId":1864904,"name":"Greek Civilization Mod - Resurrected","file":"greekmod_42.zip","version":"4.2"},
		{"gbId":"5","fileId":"6","name":"","version":""},{"gbId":7},"junk"]})");
	const LeagueModsRule l = LeagueParseModsRule(list.Get("mods"));
	CHECK(l.set);
	CHECK(!l.BaseGame());
	CHECK_EQ(static_cast<int>(l.mods.size()), 2);
	CHECK_EQ(static_cast<unsigned long long>(l.mods[0].gbId), 728851ULL);
	CHECK_EQ(static_cast<unsigned long long>(l.mods[0].fileId), 1864904ULL);
	CHECK_EQ(l.mods[0].file, std::string("greekmod_42.zip"));
	CHECK_EQ(LeagueModLabel(l.mods[0]), std::string("Greek Civilization Mod - Resurrected 4.2"));
	CHECK_EQ(static_cast<unsigned long long>(l.mods[1].gbId), 5ULL);
	CHECK_EQ(LeagueModLabel(l.mods[1]), std::string("GameBanana #5"));
	CHECK_EQ(LeagueModsNames(l.mods), std::string("Greek Civilization Mod - Resurrected 4.2, GameBanana #5"));
	LeagueModRef named;
	named.name = "Mod 2.0";
	named.version = "2.0";
	CHECK_EQ(LeagueModLabel(named), std::string("Mod 2.0"));
}

void OwnJson()
{
	const Json j = LeagueModsOwnJson({Ref(1, 2), Ref(3, 0)});
	CHECK_EQ(j.Dump(), std::string(R"([{"gbId":1,"fileId":2},{"gbId":0,"fileId":0}])"));
	CHECK_EQ(LeagueModsOwnJson({}).Dump(), std::string("[]"));
}

void Compare()
{
	LeagueModsRule free;
	CHECK_EQ(FitOf(free, {Ref(1, 2), Ref(0, 0)}), kOk);
	LeagueModsRule base;
	base.set = true;
	CHECK_EQ(FitOf(base, {}), kOk);
	CHECK_EQ(FitOf(base, {Ref(1, 2)}), kExtra);
	CHECK_EQ(FitOf(base, {Ref(0, 0)}), kExtra);
	LeagueModsRule greek;
	greek.set = true;
	LeagueModRef g = Ref(728851, 1864904);
	g.name = "Greek";
	greek.mods.push_back(g);
	CHECK_EQ(FitOf(greek, {Ref(728851, 1864904)}), kOk);
	// Another file of the same mod is not the one asked for.
	const LeagueModsCheck other = LeagueModsCompare(greek, {Ref(728851, 999)});
	CHECK_EQ(static_cast<int>(other.fit), kMissing);
	CHECK(other.missing.size() == 1 && other.missing[0].name == "Greek");
	CHECK_EQ(FitOf(greek, {}), kMissing);
	// A copy not from GameBanana can't be told apart: the missing file comes first, an unknown extra next.
	CHECK_EQ(FitOf(greek, {Ref(0, 0)}), kMissing);
	CHECK_EQ(FitOf(greek, {Ref(728851, 1864904), Ref(0, 0)}), kUnknown);
	CHECK_EQ(FitOf(greek, {Ref(728851, 1864904), Ref(5, 6)}), kExtra);
	CHECK_EQ(FitOf(greek, {Ref(728851, 1864904), Ref(728851, 1864904)}), kOk);
}

void SeasonAndTourney()
{
	LeagueSeason s;
	CHECK(LeagueParseSeason(Parse(R"({"number":3,"endsAt":10,"mods":[]})"), s));
	CHECK(s.mods.BaseGame());
	CHECK(LeagueParseSeason(Parse(R"({"number":3,"endsAt":10})"), s));
	CHECK(!s.mods.set);
	const LeagueError e = LeagueParseError(Parse(R"({"error":"wrong_mods","mods":[{"gbId":1,"fileId":2,"name":"A"}]})"));
	CHECK_EQ(e.code, std::string("wrong_mods"));
	CHECK(e.mods.set && e.mods.mods.size() == 1);
	TourneySummary t;
	CHECK(TourneyParseSummary(Parse(R"({"id":4,"slug":"cup","mods":[{"gbId":1,"fileId":2}]})"), t));
	CHECK(t.mods.set && t.mods.mods.size() == 1 && t.mods.mods[0].fileId == 2);
	CHECK(TourneyParseSummary(Parse(R"({"id":4,"slug":"cup","mods":null})"), t));
	CHECK(!t.mods.set);
	TourneyNext n;
	CHECK(TourneyParseNext(Parse(R"({"tournament":{"id":4},"match":9,"mods":[]})"), n));
	CHECK(n.mods.BaseGame());
	TourneyClaim c;
	CHECK(TourneyParseClaim(Parse(R"({"create":true,"lobbyKey":"k","mods":[]})"), c));
	CHECK(c.mods.BaseGame());
	const TourneyRefusal r = TourneyParseRefusal(Parse(R"({"error":"wrong_mods","mods":[]})"));
	CHECK(r.mods.BaseGame());
}

}

int main()
{
	ParseRule();
	OwnJson();
	Compare();
	SeasonAndTourney();
	return test::Summary("leaguemods");
}
