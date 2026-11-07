// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "game/playerini.h"

namespace {

const char kSample[] =
	"[Player]\r\n"
	"name=Marcus Aurelius\r\n"
	"color=4\r\n"
	"race=1\r\n"
	"games=3\r\n"
	"hash=1577946418\r\n"
	"fav=Hastatus\r\n"
	"fval=240\r\n"
	"\r\n"
	"[Player 0]\r\n"
	"plrname=Marcus\r\n"
	"\r\n"
	"[game0]\r\n"
	"id=904801ACA74ABC4FB4F5C3DE8AB333CD\r\n"
	"year=2026\r\nmonth=10\r\nday=20\r\nhour=18\r\nminute=54\r\n"
	"duration=5144975\r\n"
	"multi=1\r\nlost=1\r\n"
	"units_prod=616\r\nunits_killed=436\r\nunits_lost=585\r\nunits_max=120\r\n"
	"level_max=28\r\nlevel_max_unit=Ragimund\r\n"
	"race=7\r\n"
	"damage_taken=176782\r\ndamage_inflicted=165786\r\n"
	"kill_healths=135080\r\ndie_healths=141100\r\n"
	"\r\n"
	"[game1]\r\n"
	"year=2026\r\nmonth=10\r\nday=21\r\nhour=12\r\nminute=53\r\n"
	"duration=0\r\nmulti=0\r\nlost=1\r\nrace=1\r\n"
	"\r\n"
	"[game2]\r\n"
	"year = 2026\r\nmonth = 9\r\nday = 2\r\nhour = 9\r\nminute = 5\r\n"
	"duration = 1800000\r\nmulti = 0\r\nlost = 0\r\nrace = 1\r\n"
	"units_killed = 90\r\nunits_max = 150\r\nlevel_max = 28\r\nlevel_max_unit = Lucius\r\n"
	"kill_healths = 40000\r\ndie_healths = 2000\r\ndamage_inflicted = 30000\r\ndamage_taken = 1000\r\n"
	"\r\n"
	"[game4]\r\n"
	"year=2027\r\nmulti=1\r\nlost=0\r\n";

std::vector<RankStep> Ranks()
{
	return {{0, "Peasant"}, {40, "Apprentice"}, {60, "Fighter"}, {80, "Warrior"}, {100, "Veteran"}, {120, "Master"}, {140, "Chieftain"},
		{160, "Lord"}, {180, "Champion"}, {200, "Hero"}, {2, "Legend"}, {300, "Deity"}, {400, "Cheater"}};
}

void ReadsThePlayer()
{
	const PlayerIni ini = ParsePlayerIni(kSample);
	CHECK_EQ(ini.name, std::string("Marcus Aurelius"));
	CHECK_EQ(ini.race, 1);
	CHECK_EQ(ini.games, 3);
	CHECK_EQ(ini.favoriteUnit, std::string("Hastatus"));
	CHECK_EQ(ini.favoriteCount, 240);
}

void ReadsTheGamesUpToTheFirstGap()
{
	const PlayerIni ini = ParsePlayerIni(kSample);
	CHECK_EQ(static_cast<int>(ini.played.size()), 3);
	const PlayedGame &g = ini.played[0];
	CHECK_EQ(g.year, 2026);
	CHECK_EQ(g.minute, 54);
	CHECK_EQ(g.durationMs, 5144975u);
	CHECK(g.multi);
	CHECK(g.lost);
	CHECK_EQ(g.race, 7);
	CHECK_EQ(g.unitsKilled, 436u);
	CHECK_EQ(g.levelMaxUnit, std::string("Ragimund"));
	CHECK_EQ(ini.played[2].durationMs, 1800000u);
	CHECK_EQ(ini.played[2].levelMaxUnit, std::string("Lucius"));
}

void RatesLikeTheGame()
{
	const PlayerIni ini = ParsePlayerIni(kSample);
	// ((135080/2 + 1000 + 165786) * 100) / (141100/2 + 10000 + 176782)
	CHECK_EQ(GameRating(ini.played[0]), 91u);
	CHECK_EQ(GameRating(ini.played[1]), 10u);
	CHECK_EQ(GameRating(ini.played[2]), 425u);
}

void Summarizes()
{
	const ProfileSummary s = SummarizeProfile(ParsePlayerIni(kSample), Ranks());
	CHECK_EQ(s.games, 3);
	CHECK_EQ(s.singleGames, 2);
	CHECK_EQ(s.singleWins, 1);
	CHECK_EQ(s.multiGames, 1);
	CHECK_EQ(s.multiWins, 0);
	CHECK_EQ(static_cast<unsigned long long>(s.playedMs), 6944975ull);
	CHECK_EQ(static_cast<unsigned long long>(s.kills), 526ull);
	CHECK_EQ(static_cast<int>(s.rating), 175);
	CHECK_EQ(s.rank, 7);
	CHECK_EQ(s.nextRank, 8);
	CHECK_EQ(s.rankPoints, 160);
	CHECK_EQ(s.nextPoints, 180);
	CHECK_EQ(s.favoriteRace, 1);
	CHECK_EQ(s.favoriteRacePercent, 66);
	CHECK_EQ(s.bestLevel, 28u);
	CHECK_EQ(s.bestUnit, std::string("Ragimund"));
	CHECK_EQ(s.largestArmy, 150u);
}

void FollowsTheRanksOfTheGame()
{
	PlayerIni ini;
	PlayedGame g;
	g.killHealths = 0;
	g.dieHealths = 0;
	// 1000 * 100 / 10000 = 10 points: still a Peasant.
	ini.played.push_back(g);
	ProfileSummary s = SummarizeProfile(ini, Ranks());
	CHECK_EQ(s.rank, 0);
	CHECK_EQ(s.nextRank, 1);
	// 250 points: the game skips Hero because the next rank asks for 2 points, so it shows Legend.
	ini.played[0].damageInflicted = 24000;
	s = SummarizeProfile(ini, Ranks());
	CHECK_EQ(static_cast<int>(s.rating), 250);
	CHECK_EQ(s.rank, 10);
	CHECK_EQ(s.nextRank, 11);
	CHECK_EQ(s.rankPoints, 200);
	CHECK_EQ(s.nextPoints, 300);
	// Past the last rank there is no next one.
	ini.played[0].damageInflicted = 99000;
	s = SummarizeProfile(ini, Ranks());
	CHECK_EQ(s.rank, 12);
	CHECK_EQ(s.nextRank, -1);
}

void HandlesAnEmptyProfile()
{
	const PlayerIni ini = ParsePlayerIni("[Player]\nname=Nuevo\ncolor=0\nrace=-1\ngames=0\n");
	CHECK_EQ(ini.name, std::string("Nuevo"));
	CHECK_EQ(ini.race, -1);
	CHECK(ini.played.empty());
	const ProfileSummary s = SummarizeProfile(ini, Ranks());
	CHECK_EQ(s.games, 0);
	CHECK_EQ(static_cast<int>(s.rating), 0);
	CHECK_EQ(s.rank, 0);
	CHECK_EQ(s.favoriteRace, -1);
	CHECK_EQ(SummarizeProfile(ini, {}).rank, -1);
	CHECK(ParsePlayerIni("").name.empty());
	CHECK(ParsePlayerIni("garbage\n=\n[\nname=x").name.empty());
}

void ListsTheNewestFirst()
{
	const std::vector<PlayedGame> recent = RecentGames(ParsePlayerIni(kSample), 2);
	CHECK_EQ(static_cast<int>(recent.size()), 2);
	CHECK_EQ(recent[0].index, 1);
	CHECK_EQ(recent[1].index, 0);
	CHECK_EQ(PercentOf(1, 3), 33);
	CHECK_EQ(PercentOf(1, 0), 0);
}

}

int main()
{
	ReadsThePlayer();
	ReadsTheGamesUpToTheFirstGap();
	RatesLikeTheGame();
	Summarizes();
	FollowsTheRanksOfTheGame();
	HandlesAnEmptyProfile();
	ListsTheNewestFirst();
	return test::Summary("playerini");
}
