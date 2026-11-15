// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "i18n/tr.h"

namespace {

std::string g_language = "english";

const std::string &TestLanguage()
{
	return g_language;
}

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

int TierNumber(LeagueTier t) { return static_cast<int>(t); }

void MeAdditions()
{
	LeagueMe me;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"7","ranked":{"queue":"1v1","season":1,"tier":"centurion","division":2,"glory":964,"wins":30,"losses":12,
		"placementLeft":0,"peak":{"glory":1012,"tier":"tribune","division":3},"streak":-2,"placementResults":["win","loss","void","Win","bogus"]},
		"rewards":[{"season":1,"seasonName":"Bellvm Gallicvm","queue":"1v1","tier":"tribune","place":121,"games":94,"frame":true},
			{"season":0,"seasonName":"Trial of Arms","queue":"1v1","tier":"founder","peakTier":"centurion","place":40,"games":12,"frame":false}],
		"ceremony":{"season":1,"seasonName":"Bellvm Gallicvm","queue":"1v1","tier":"tribune","place":121,"games":94,"wins":53,"losses":41,"frame":true,
			"finalTier":"centurion","finalDivision":1},
		"equipped":{"frame":1,"title":null}})"),
		me));
	CHECK(me.ranked.peak.present);
	CHECK_EQ(me.ranked.peak.glory, 1012);
	CHECK_EQ(TierNumber(me.ranked.peak.tier), TierNumber(LeagueTier::Tribune));
	CHECK_EQ(me.ranked.peak.division, 3);
	CHECK_EQ(me.ranked.streak, -2);
	CHECK_EQ(static_cast<int>(me.ranked.placement.size()), 4);
	CHECK(me.ranked.placement[0] == LeaguePlacementMark::Win);
	CHECK(me.ranked.placement[1] == LeaguePlacementMark::Loss);
	CHECK(me.ranked.placement[2] == LeaguePlacementMark::Void);
	CHECK_EQ(static_cast<int>(me.rewards.size()), 2);
	CHECK_EQ(me.rewards[0].season, 1);
	CHECK_EQ(me.rewards[0].seasonName, std::string("Bellvm Gallicvm"));
	CHECK_EQ(TierNumber(me.rewards[0].tier), TierNumber(LeagueTier::Tribune));
	CHECK(me.rewards[0].frame);
	CHECK(!me.rewards[0].founder);
	CHECK_EQ(me.rewards[0].place, 121);
	// The test season gives the Founder's vexillum, and its title keeps the highest rank reached.
	CHECK(me.rewards[1].founder);
	CHECK_EQ(TierNumber(me.rewards[1].tier), TierNumber(LeagueTier::Centurion));
	CHECK(!me.rewards[1].frame);
	CHECK(me.ceremony.present);
	CHECK_EQ(me.ceremony.reward.season, 1);
	CHECK_EQ(me.ceremony.reward.games, 94);
	CHECK_EQ(me.ceremony.wins, 53);
	CHECK_EQ(me.ceremony.losses, 41);
	CHECK_EQ(TierNumber(me.ceremony.finalTier), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(me.ceremony.finalDivision, 1);
	CHECK_EQ(me.equippedFrame, 1);
	CHECK_EQ(me.equippedTitle, -1);
	CHECK(LeagueRewardOf(me, 0) && LeagueRewardOf(me, 0)->games == 12);
	CHECK(!LeagueRewardOf(me, 5));
	const std::vector<LeagueReward> titles = LeagueTitleRewards(me);
	CHECK_EQ(static_cast<int>(titles.size()), 1);
	CHECK_EQ(titles[0].season, 1);

	// A server that sends nothing new, or puts the standing's extras beside `ranked`.
	LeagueMe old;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"7","ranked":{"tier":"recruit","division":3},"ceremony":null,"equipped":{"frame":0,"title":"0"},
		"streak":4,"peak":{"glory":250,"tier":"recruit","division":1}})"),
		old));
	CHECK(!old.ceremony.present);
	CHECK(old.rewards.empty());
	CHECK_EQ(old.ranked.streak, 4);
	CHECK_EQ(old.ranked.peak.glory, 250);
	CHECK_EQ(old.equippedFrame, 0);
	CHECK_EQ(old.equippedTitle, 0);
	LeagueMe bare;
	CHECK(LeagueParseMe(Parse(R"({"steamId":"7"})"), bare));
	CHECK_EQ(bare.equippedFrame, -1);
	CHECK(!bare.ranked.peak.present);
	CHECK(bare.ranked.placement.empty());
}

void LookupFrames()
{
	std::map<uint64_t, LeagueLookupEntry> out;
	CHECK(LeagueParsePlayersLookup(Parse(R"({"11":{"patron":null,"badge":null,"frame":2},"12":{"frame":null},
		"13":{"frame":{"season":1,"tier":"legate","seasonName":"Bellvm Gallicvm"}},"14":{"frame":{"season":0,"tier":"founder"}},"15":{}})"),
		out));
	CHECK_EQ(out[11].frame.season, 2);
	CHECK(out[11].frame.Shown());
	CHECK_EQ(TierNumber(out[11].frame.tier), TierNumber(LeagueTier::None));
	CHECK(!out[12].frame.Shown());
	CHECK_EQ(out[13].frame.season, 1);
	CHECK_EQ(TierNumber(out[13].frame.tier), TierNumber(LeagueTier::Legate));
	CHECK_EQ(out[13].frame.seasonName, std::string("Bellvm Gallicvm"));
	CHECK(out[14].frame.founder);
	CHECK(LeagueParsePlayersLookup(Parse(R"({"16":{"frame":{"season":0,"tier":"founder","peakTier":"tribune"}}})"), out));
	CHECK_EQ(TierNumber(out[16].frame.tier), TierNumber(LeagueTier::Tribune));
	CHECK(!out[16].frame.founder);
	// A bare season number says nothing of the rank, even for the test season.
	CHECK(LeagueParsePlayersLookup(Parse(R"({"17":{"frame":0}})"), out));
	CHECK(out[17].frame.Shown());
	CHECK(!out[17].frame.founder);
	CHECK(!out[15].frame.Shown());
	CHECK(!LeagueFrameFromJson(nullptr).Shown());
	const Json text = Json::MakeString("3");
	CHECK_EQ(LeagueFrameFromJson(&text).season, 3);
	const Json negative = Json::MakeNumber(-1);
	CHECK(!LeagueFrameFromJson(&negative).Shown());
}

void BoardRows()
{
	LeagueBoard b;
	CHECK(LeagueParseBoard(Parse(R"({"season":1,"total":3,"offset":0,"rows":[
		{"position":1,"steamId":"11","name":"Scipio","tier":"imperator","glory":1811,"wins":112,"losses":41,"delta7":36,"streak":6},
		{"position":2,"steamId":"12","name":"Draco","tier":"imperator","glory":1790,"wins":98,"losses":39,"delta7":-12,"streak":-1},
		{"position":3,"steamId":"13","name":"Novus","tier":"recruit","division":3,"glory":40,"wins":2,"losses":3,"delta7":null}]})"),
		b));
	CHECK(b.rows[0].hasDelta7);
	CHECK_EQ(b.rows[0].delta7, 36);
	CHECK_EQ(b.rows[0].streak, 6);
	CHECK_EQ(b.rows[1].delta7, -12);
	CHECK_EQ(b.rows[1].streak, -1);
	CHECK(!b.rows[2].hasDelta7);
	CHECK_EQ(b.rows[2].streak, 0);
}

void Seasons()
{
	std::vector<LeagueSeasonInfo> list;
	CHECK(LeagueParseSeasons(Parse(R"({"seasons":[{"id":2,"name":"Bellvm Civile","startsAt":300,"endsAt":400,"closed":false,"beta":false},
		{"id":"1","name":"Bellvm Gallicvm","startsAt":100,"endsAt":200,"closed":true},{"id":0,"name":"Trial of Arms","closed":true,"beta":true},
		{"name":"no id"},7]})"),
		list));
	CHECK_EQ(static_cast<int>(list.size()), 3);
	CHECK_EQ(list[0].id, 2);
	CHECK(!list[0].closed);
	CHECK_EQ(list[1].id, 1);
	CHECK(list[1].closed);
	CHECK_EQ(static_cast<int>(list[1].endsAt), 200);
	CHECK(list[2].beta);
	CHECK(LeagueSeasonById(list, 0) && LeagueSeasonById(list, 0)->name == "Trial of Arms");
	CHECK(!LeagueSeasonById(list, 9));
	CHECK(!LeagueParseSeasons(Parse(R"({"error":"x"})"), list));
}

void Hall()
{
	LeagueHall h;
	CHECK(LeagueParseHall(Parse(R"({"season":1,"name":"Bellvm Gallicvm","top":[
		{"place":2,"steamId":"12","name":"Draco","tier":"imperator","glory":1790,"games":137},
		{"place":1,"steamId":"11","name":"Scipio","tier":"imperator","division":2,"glory":1811,"games":153},
		{"place":0,"name":"nobody"},
		{"place":3,"steamId":"13","name":"Viriato","tier":"legate","division":1,"glory":1497,"games":90}]})"),
		h));
	CHECK_EQ(h.season, 1);
	CHECK_EQ(h.name, std::string("Bellvm Gallicvm"));
	CHECK_EQ(static_cast<int>(h.top.size()), 3);
	CHECK_EQ(h.top[0].place, 1);
	CHECK_EQ(h.top[0].name, std::string("Scipio"));
	// The elite has no divisions, whatever is sent.
	CHECK_EQ(h.top[0].division, 0);
	CHECK_EQ(h.top[2].division, 1);
	CHECK_EQ(h.top[1].games, 137);
	CHECK(!LeagueParseHall(Parse(R"({"season":1})"), h));
}

void PlayerCard()
{
	LeaguePlayerProfile p;
	CHECK(LeagueParsePlayerProfile(Parse(R"({"steamId":"12","name":"Draco","season":1,"tier":"imperator","division":null,"glory":1790,"place":2,
		"elite":true,"peak":{"glory":1802,"tier":"imperator","place":1},"games":137,"wins":98,"losses":39,"streak":2,"avgDurationMs":1560000,
		"civs":[{"civ":"carthage","games":42,"wins":30},{"civ":"iberians","games":63,"wins":41},{"civ":"gauls","games":12,"wins":5},
			{"civ":"rome","games":20,"wins":10}],
		"recent":[{"id":9,"playedAt":5,"result":"win","gloryDelta":21,"opponent":{"steamId":"13","name":"Viriato","tier":"legate","division":1}},
			{"id":8,"playedAt":4,"result":"loss","gloryDelta":-18},{"bogus":true}],
		"frame":1,"title":{"season":0,"tier":"centurion","seasonName":"Trial of Arms"}})"),
		p));
	CHECK_EQ(p.steamId, std::string("12"));
	CHECK_EQ(p.place, 2);
	CHECK(p.elite);
	CHECK_EQ(p.peak.place, 1);
	CHECK_EQ(p.games, 137);
	CHECK_EQ(p.streak, 2);
	CHECK_EQ(static_cast<int>(p.avgDurationMs), 1560000);
	// The three played most, the most first.
	CHECK_EQ(static_cast<int>(p.civs.size()), 3);
	CHECK_EQ(p.civs[0].civ, std::string("iberians"));
	CHECK_EQ(p.civs[1].civ, std::string("carthage"));
	CHECK_EQ(p.civs[2].civ, std::string("rome"));
	CHECK_EQ(static_cast<int>(p.recent.size()), 2);
	CHECK(p.recent[0].result == LeagueGameResult::Win);
	CHECK_EQ(p.recent[1].gloryDelta, -18);
	// A frame of the season shown takes the card's rank; the title says its own.
	CHECK_EQ(p.frame.season, 1);
	CHECK_EQ(TierNumber(p.frame.tier), TierNumber(LeagueTier::Imperator));
	CHECK_EQ(p.title.season, 0);
	CHECK_EQ(TierNumber(p.title.tier), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(p.title.seasonName, std::string("Trial of Arms"));

	LeaguePlayerProfile bare;
	CHECK(LeagueParsePlayerProfile(Parse(R"({"steamId":"5","wins":3,"losses":2,"frame":null})"), bare));
	CHECK_EQ(bare.games, 5);
	CHECK(!bare.frame.Shown());
	CHECK(!LeagueParsePlayerProfile(Parse(R"({"error":"not_found"})"), bare));
}

void Texts()
{
	CHECK_EQ(LeagueRoman(1), std::string("I"));
	CHECK_EQ(LeagueRoman(4), std::string("IV"));
	CHECK_EQ(LeagueRoman(9), std::string("IX"));
	CHECK_EQ(LeagueRoman(14), std::string("XIV"));
	CHECK_EQ(LeagueRoman(0), std::string("0"));

	CHECK_EQ(LeagueFramePlate("Tribuno", 1), std::string("T \xC2\xB7 I"));
	CHECK_EQ(LeagueFramePlate("centurion", 3), std::string("C \xC2\xB7 III"));
	CHECK_EQ(LeagueFramePlate("\xC3\x89lite", 2), std::string("\xC3\x89 \xC2\xB7 II"));
	CHECK_EQ(LeagueFramePlate("Fundador", 0), std::string("F \xC2\xB7 0"));
	CHECK_EQ(LeagueFramePlate("", 1), std::string("? \xC2\xB7 I"));

	g_language = "english";
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::Tribune, false, "Trial of Arms", 0), std::string("Tribune of Trial of Arms"));
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::Tribune, false, "", 1), std::string("Tribune of Season I"));
	CHECK_EQ(LeagueStreakText(3), std::string("W3"));
	CHECK_EQ(LeagueStreakText(-2), std::string("L2"));
	CHECK_EQ(LeagueStreakText(0), std::string("\xE2\x80\x94"));
	g_language = "spanish";
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::Tribune, false, "Prueba de armas", 0), std::string("Tribuno de Prueba de armas"));
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::Centurion, false, "", 1), std::string("Centuri\xC3\xB3n de la Temporada I"));
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::None, true, "Prueba de armas", 0), std::string("Fundador de Prueba de armas"));
	CHECK_EQ(LeagueStreakText(3), std::string("V3"));
	CHECK_EQ(LeagueStreakText(-2), std::string("D2"));
	g_language = "italian";
	CHECK_EQ(LeagueSeasonTitle(LeagueTier::Legate, false, "", 2), std::string("Legato della Stagione II"));
	CHECK_EQ(LeagueStreakText(-2), std::string("S2"));
	g_language = "english";
}

LeagueRanked Standing(LeagueTier tier, int division, int glory, int wins, int losses)
{
	LeagueRanked r;
	r.present = true;
	r.tier = tier;
	r.division = division;
	r.glory = glory;
	r.wins = wins;
	r.losses = losses;
	return r;
}

void RewardsToday()
{
	// Under 10 games nothing yet; the goal is the vexillum.
	LeagueRewardPreview p = LeagueRewardsToday(Standing(LeagueTier::Centurion, 2, 964, 4, 3), false);
	CHECK(!p.placing);
	CHECK_EQ(p.games, 7);
	CHECK_EQ(p.goal, 10);
	CHECK(!p.vexillum && !p.frame);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Centurion));
	CHECK_EQ(TierNumber(p.nextTier), TierNumber(LeagueTier::Tribune));
	CHECK_EQ(p.gloryToNext, 236);

	// The highest rank reached counts, not the one held now.
	LeagueRanked fell = Standing(LeagueTier::Centurion, 1, 1134, 8, 6);
	fell.peak.present = true;
	fell.peak.tier = LeagueTier::Tribune;
	p = LeagueRewardsToday(fell, false);
	CHECK_EQ(p.games, 14);
	CHECK(p.vexillum && !p.frame);
	CHECK_EQ(p.goal, 25);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Tribune));
	CHECK_EQ(TierNumber(p.nextTier), TierNumber(LeagueTier::Legate));
	CHECK_EQ(p.gloryToNext, 1500 - 1134);

	// The elite only by the place held at the end: a fallen Praetorian keeps Legate.
	LeagueRanked elite = Standing(LeagueTier::Legate, 1, 1790, 30, 20);
	elite.peak.present = true;
	elite.peak.tier = LeagueTier::Praetorian;
	p = LeagueRewardsToday(elite, false);
	CHECK(p.vexillum && p.frame);
	CHECK_EQ(p.goal, 0);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Legate));
	CHECK_EQ(TierNumber(p.nextTier), TierNumber(LeagueTier::Praetorian));
	CHECK_EQ(p.gloryToNext, 0);
	p = LeagueRewardsToday(Standing(LeagueTier::Imperator, 0, 1850, 60, 20), false);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::Imperator));
	CHECK_EQ(TierNumber(p.nextTier), TierNumber(LeagueTier::None));

	// In the placement there is no rank to reward yet; the test season gives the Founder.
	LeagueRanked placing = Standing(LeagueTier::None, 0, 0, 2, 1);
	placing.placementLeft = 2;
	p = LeagueRewardsToday(placing, true);
	CHECK(p.placing);
	CHECK(p.founder);
	CHECK_EQ(TierNumber(p.tier), TierNumber(LeagueTier::None));
	CHECK(LeagueRewardsToday(LeagueRanked(), false).placing);

	LeagueReward r;
	r.season = 1;
	r.tier = LeagueTier::Tribune;
	r.seasonName = "Bellvm Gallicvm";
	const LeagueFrame f = LeagueFrameOfReward(r);
	CHECK_EQ(f.season, 1);
	CHECK_EQ(TierNumber(f.tier), TierNumber(LeagueTier::Tribune));
	CHECK(!f.founder);
}

}

int main()
{
	TrSetLanguageSource(&TestLanguage);
	MeAdditions();
	LookupFrames();
	BoardRows();
	Seasons();
	Hall();
	PlayerCard();
	Texts();
	RewardsToday();
	return test::Summary("leagueseasons");
}
