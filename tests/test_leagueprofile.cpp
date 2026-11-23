// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <cmath>
#include <string>

#include "core/json.h"
#include "core/league.h"
#include "core/leagueprofile.h"
#include "core/matchhistory.h"

namespace {

Json Parse(const std::string &text)
{
	Json j;
	CHECK(Json::Parse(text, j));
	return j;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

const char *const kFull = R"({"steamId":"76561198000000001","name":"Marcus","avatarUrl":"https://a/b.jpg","patron":{"tier":"Gold","place":3},
	"badge":{"kind":"trophy","id":"9","tier":"silver","count":2},"frame":{"season":1,"tier":"tribune","seasonName":"Bellvm"},"title":2,
	"ranked":{"1v1":{"season":2,"tier":"centurion","division":2,"glory":964,"placementLeft":0,"wins":30,"losses":12,"position":7,"peakTier":"tribune"},
		"2v2":null},
	"gloryHistory":{"1v1":[{"at":1000,"glory":900},{"at":2000,"glory":964},{"at":3000}],"2v2":[]},
	"totals":{"games":40,"wins":28,"losses":12,"kills":5000,"deaths":2500,"damageDealt":900000,"damageTaken":800000,"gold":120000,"units":4000,
		"minutes":1200,"statsGames":20},
	"civs":[{"civ":"rome","games":3,"wins":1,"losses":2,"kills":30,"deaths":60,"statsGames":3,"minutes":90,"army":300,"units":120},
		{"civ":"","games":9,"wins":4},{"civ":"gaul","games":12,"wins":9,"losses":3,"kills":1200,"deaths":400,"damageDealt":24000,"gold":6000,
		"units":600,"army":1200,"minutes":360,"statsGames":10},{"civ":"carthage","games":0}],
	"recent":[{"at":5000,"kind":"cup","queue":null,"cup":"Copa de Hispania","civ":"gaul","won":true,"durationMs":1800000,"kills":120,"deaths":40,
		"map":"forest","opponents":[{"steamId":"76561198000000002","name":"Brutus"}],"matchId":77},
		{"at":9000,"kind":"ranked","queue":"2v2","cup":null,"civ":"rome","won":false,"durationMs":600000,"kills":10,"deaths":30,"map":"desert",
		"opponents":[{"steamId":"76561198000000003","name":"A"},{"steamId":"76561198000000004","name":"B"}],"matchId":78}],
	"trophies":[{"kind":"gold","title":"Copa I","season":1,"awardedAt":123},{"kind":"weird","title":"X","season":2}],
	"cupTitles":[{"kind":"season","name":"Campeón de la temporada I","season":1}]})";

void Full()
{
	LeagueProfile p;
	CHECK(LeagueParseProfile(Parse(kFull), p));
	CHECK_EQ(p.steamId, std::string("76561198000000001"));
	CHECK_EQ(p.name, std::string("Marcus"));
	CHECK_EQ(p.patronTier, std::string("gold"));
	CHECK_EQ(p.patronPlace, 3);
	CHECK(p.badge.kind == LeagueBadgeKind::Trophy);
	CHECK(p.badge.metal == LeagueMetal::Silver);
	CHECK_EQ(p.frame.season, 1);
	CHECK(p.frame.tier == LeagueTier::Tribune);
	CHECK_EQ(p.title.season, 2);

	CHECK(p.ranked.present);
	CHECK_EQ(p.ranked.queue, std::string("1v1"));
	CHECK(p.ranked.tier == LeagueTier::Centurion);
	CHECK_EQ(p.ranked.division, 2);
	CHECK_EQ(p.ranked.glory, 964);
	CHECK_EQ(p.ranked.wins, 30);
	CHECK_EQ(p.ranked.position, 7);
	CHECK(p.ranked.peak.present);
	CHECK(p.ranked.peak.tier == LeagueTier::Tribune);
	// A point without its Glory is left out.
	CHECK(p.ranked.gloryKnown);
	CHECK_EQ(static_cast<int>(p.ranked.gloryHistory.size()), 2);
	CHECK_EQ(p.ranked.gloryHistory[1].glory, 964);
	CHECK(!p.ranked2v2.present);
	CHECK_EQ(p.ranked2v2.queue, std::string("2v2"));
	CHECK(p.ranked2v2.gloryKnown);
	CHECK(p.ranked2v2.gloryHistory.empty());

	CHECK_EQ(p.totals.games, 40);
	CHECK_EQ(p.totals.statsGames, 20);
	CHECK_EQ(p.totals.kills, 5000ull);
	CHECK(Near(p.totals.minutes, 1200));

	// The most games first, the one with no civilization last, and none without games.
	CHECK_EQ(static_cast<int>(p.civs.size()), 3);
	CHECK_EQ(p.civs[0].civ, std::string("gaul"));
	CHECK_EQ(p.civs[1].civ, std::string("rome"));
	CHECK_EQ(p.civs[2].civ, std::string(""));
	CHECK_EQ(p.civs[0].sums.army, 1200ull);

	// Newest first.
	CHECK_EQ(static_cast<int>(p.recent.size()), 2);
	const LeagueProfileGame &ranked = p.recent[0];
	CHECK(!ranked.cup);
	CHECK_EQ(ranked.queue, std::string("2v2"));
	CHECK(!ranked.won);
	CHECK_EQ(ranked.civ, std::string("rome"));
	CHECK_EQ(static_cast<int>(ranked.opponents.size()), 2);
	CHECK_EQ(ranked.opponents[0].first, std::string("76561198000000003"));
	const LeagueProfileGame &cup = p.recent[1];
	CHECK(cup.cup);
	CHECK_EQ(cup.cupName, std::string("Copa de Hispania"));
	CHECK(cup.won);
	CHECK_EQ(cup.durationMs, 1800000u);
	CHECK_EQ(cup.kills, 120u);
	CHECK_EQ(cup.deaths, 40u);
	CHECK_EQ(cup.map, std::string("forest"));
	CHECK_EQ(cup.opponents[0].second, std::string("Brutus"));
	CHECK_EQ(static_cast<int>(cup.matchId), 77);

	CHECK_EQ(static_cast<int>(p.trophies.size()), 2);
	CHECK(p.trophies[0].metal == LeagueMetal::Gold);
	CHECK_EQ(p.trophies[0].title, std::string("Copa I"));
	// A metal it does not know is gold, as the badges do.
	CHECK(p.trophies[1].metal == LeagueMetal::Gold);
	CHECK_EQ(static_cast<int>(p.cupTitles.size()), 1);
}

void Sparse()
{
	LeagueProfile p;
	CHECK(LeagueParseProfile(Parse(R"({"steamId":"5","name":"X","ranked":null,"totals":{"games":3,"wins":9,"statsGames":7}})"), p));
	CHECK(!p.ranked.present);
	CHECK(!p.ranked.gloryKnown);
	CHECK(!p.badge.Shown());
	CHECK(!p.frame.Shown());
	// Never more wins, or games with figures, than games.
	CHECK_EQ(p.totals.wins, 3);
	CHECK_EQ(p.totals.statsGames, 3);
	CHECK(p.civs.empty());
	CHECK(p.recent.empty());
	CHECK(!LeagueParseProfile(Parse("[1,2]"), p));
}

void Records()
{
	LeagueProfile p;
	CHECK(LeagueParseProfile(Parse(kFull), p));
	const MatchCivRecord gaul = LeagueProfileRecord(p.civs[0].sums, 4);
	CHECK_EQ(gaul.race, 4);
	CHECK_EQ(gaul.games, 12);
	CHECK_EQ(gaul.wins, 9);
	CHECK_EQ(gaul.winPercent, 75);
	CHECK_EQ(gaul.ran, 10);
	// The stat figures over the games with figures, the duration over every game.
	CHECK(Near(gaul.kills1, 120));
	CHECK(Near(gaul.deaths1, 40));
	CHECK(Near(gaul.damageDealt, 2400));
	CHECK(Near(gaul.gold, 600));
	CHECK(Near(gaul.produced, 60));
	CHECK(Near(gaul.army, 120));
	CHECK(Near(gaul.minutes, 30));
	CHECK(Near(gaul.killRatio, 3));
	// Without figures, the averages stay at zero instead of dividing by none.
	const MatchCivRecord none = LeagueProfileRecord(p.civs[2].sums, kCivUnknown);
	CHECK_EQ(none.games, 9);
	CHECK_EQ(none.ran, 0);
	CHECK(Near(none.kills1, 0));
	CHECK(Near(none.minutes, 0));
	CHECK_EQ(LeagueProfilePath("76561198000000001", "es"), std::string("/v1/league/players/76561198000000001/profile?lang=es"));
	CHECK_EQ(LeagueProfilePath("1", ""), std::string("/v1/league/players/1/profile?lang=en"));
}

}

int main()
{
	Full();
	Sparse();
	Records();
	return test::Summary("leagueprofile");
}
