// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/leagueroomoptions.h"

namespace {

void NothingSaidIsTheCompetitiveRoomInSpring()
{
	const LeagueRoomOptions o = LeagueRoomOptionsFor(LeagueRoomRequest());
	CHECK(o.preset == LeagueRoomPreset::Competitive);
	CHECK_EQ(o.goldKey, std::string("5000@gold"));
	CHECK_EQ(o.populationKey, std::string("Normal@population"));
	CHECK_EQ(o.gameType, 0);
	CHECK_EQ(o.limit, 0);
	CHECK(!o.bonuses);
	CHECK(o.fog);
	CHECK(!o.explored);
	CHECK(o.sharedSupport);
	CHECK(o.sharedControl);
	CHECK(o.lockDiplomacy);
	CHECK(!o.hideCivs);
	CHECK(!o.freeAttach);
	CHECK(!o.cheats);
	CHECK(o.timers);
	CHECK(o.allSites);
	CHECK_EQ(o.climate, 0);
	CHECK(o.contentDefaults);
	CHECK_EQ(o.gameSpeed, -1);
}

void EachPresetHasItsOwnOptions()
{
	LeagueRoomRequest r;
	r.preset = "Classic";
	LeagueRoomOptions o = LeagueRoomOptionsFor(r);
	CHECK(o.preset == LeagueRoomPreset::Classic);
	CHECK_EQ(o.goldKey, std::string("Default@gold"));
	CHECK_EQ(o.populationKey, std::string("Normal@population"));
	CHECK(o.bonuses);
	CHECK(o.fog);
	CHECK(!o.explored);
	CHECK(!o.lockDiplomacy);
	CHECK(!o.sharedControl);
	r.preset = "various";
	o = LeagueRoomOptionsFor(r);
	CHECK(o.preset == LeagueRoomPreset::Various);
	CHECK_EQ(o.goldKey, std::string("10000@gold"));
	CHECK_EQ(o.populationKey, std::string("High@population"));
	CHECK(o.bonuses);
	CHECK(!o.fog);
	CHECK(o.explored);
	CHECK(!o.lockDiplomacy);
	CHECK(o.sharedControl);
	r.preset = "something new";
	CHECK(LeagueRoomOptionsFor(r).preset == LeagueRoomPreset::Competitive);
	CHECK_EQ(std::string(LeagueRoomPresetId(LeagueRoomPreset::Various)), std::string("various"));
}

void TheSwitchesWinOverThePreset()
{
	LeagueRoomRequest r;
	r.preset = "competitive";
	r.fog = 0;
	r.explored = 1;
	r.hideCivs = 1;
	r.cheats = 1;
	r.timers = 0;
	r.diplomacy = "free";
	LeagueRoomOptions o = LeagueRoomOptionsFor(r);
	CHECK(!o.fog);
	CHECK(o.explored);
	CHECK(o.hideCivs);
	CHECK(o.cheats);
	CHECK(!o.timers);
	CHECK(!o.lockDiplomacy);
	CHECK_EQ(o.goldKey, std::string("5000@gold"));
	r.preset = "various";
	r.diplomacy = "LOCKED";
	CHECK(LeagueRoomOptionsFor(r).lockDiplomacy);
	r.diplomacy = "open";
	CHECK(!LeagueRoomOptionsFor(r).lockDiplomacy);
	r.diplomacy = "whatever";
	CHECK(!LeagueRoomOptionsFor(r).lockDiplomacy);
}

void TheSeasonIsSpringUnlessTheLeagueSaysOtherwise()
{
	LeagueRoomRequest r;
	CHECK_EQ(LeagueRoomOptionsFor(r).climate, 0);
	r.climate = 2;
	CHECK_EQ(LeagueRoomOptionsFor(r).climate, 2);
	r.climate = 7;
	CHECK_EQ(LeagueRoomOptionsFor(r).climate, 0);
	CHECK_EQ(std::string(LeagueRoomClimateName(0)), std::string("spring"));
	CHECK_EQ(std::string(LeagueRoomClimateName(1)), std::string("autumn"));
	CHECK_EQ(std::string(LeagueRoomClimateName(2)), std::string("winter"));
}

void TheRankedSeasonsSettingsAreTheRequest()
{
	LeagueRoomSettings s;
	s.preset = "classic";
	s.fog = 0;
	s.explored = 1;
	s.hideCivs = 1;
	s.cheats = 0;
	s.timers = 0;
	s.diplomacy = "locked";
	const LeagueRoomOptions o = LeagueRoomOptionsFor(LeagueRoomRequestOf(s));
	CHECK(o.preset == LeagueRoomPreset::Classic);
	CHECK(!o.fog);
	CHECK(o.explored);
	CHECK(o.hideCivs);
	CHECK(!o.cheats);
	CHECK(!o.timers);
	CHECK(o.lockDiplomacy);
	CHECK_EQ(o.climate, 0);
	// A season that says nothing: the competitive room.
	const LeagueRoomOptions none = LeagueRoomOptionsFor(LeagueRoomRequestOf(LeagueRoomSettings()));
	CHECK(none.preset == LeagueRoomPreset::Competitive);
	CHECK(!none.hideCivs);
	CHECK(none.timers);
}

void TheCupsSettingsAreTheRequest()
{
	TourneySettings s;
	s.preset = "various";
	s.hideCivs = true;
	s.fog = 1;
	s.timers = 0;
	s.diplomacy = "free";
	const LeagueRoomOptions o = LeagueRoomOptionsFor(LeagueRoomRequestOf(s));
	CHECK(o.preset == LeagueRoomPreset::Various);
	CHECK(o.hideCivs);
	CHECK(o.fog);
	CHECK(o.explored);
	CHECK(!o.timers);
	CHECK(!o.cheats);
	CHECK(!o.lockDiplomacy);
	CHECK_EQ(o.goldKey, std::string("10000@gold"));
	TourneySettings plain;
	const LeagueRoomOptions d = LeagueRoomOptionsFor(LeagueRoomRequestOf(plain));
	CHECK(d.preset == LeagueRoomPreset::Competitive);
	CHECK(!d.hideCivs);
	CHECK(d.timers);
	CHECK(d.lockDiplomacy);
}

void TheLogLineNamesEveryOption()
{
	const std::string text = LeagueRoomOptionsText(LeagueRoomOptionsFor(LeagueRoomRequest()));
	for (const char *part : {"preset competitive", "gold 5000@gold", "population Normal@population", "game type item 0", "limit item 0", "bonuses no",
			 "fog yes", "explored no", "shared support yes", "shared control yes", "diplomacy locked", "hidden civilizations no", "free attachment no",
			 "cheats no", "respawn timers yes", "neutral sites all", "season spring", "content options default", "game speed -1"}) {
		CHECK(text.find(part) != std::string::npos);
	}
}

void TheRoomsItemsAreFoundInAnyLanguage()
{
	CHECK_EQ(LeagueRoomItemText("Por defecto"), std::string("por defecto"));
	CHECK_EQ(LeagueRoomItemText("  Elevata! "), std::string("elevata"));
	CHECK_EQ(LeagueRoomItemText("Predefinit\xC3\xA0"), std::string("predefinita"));
	CHECK_EQ(LeagueRoomItemText("M\xC3\xA1s alta"), std::string("mas alta"));
	const std::vector<std::string> items = {"Baja", "Normal", "Alta"};
	CHECK_EQ(LeagueRoomItemFor(items, {}, {"normal"}), 1);
	CHECK_EQ(LeagueRoomItemFor(items, {}, {"", "alta"}), 2);
	CHECK_EQ(LeagueRoomItemFor(items, {}, {"high"}), -1);
	CHECK_EQ(LeagueRoomItemFor(items, {0, 1, 0}, {"normal"}), -1);
	CHECK_EQ(LeagueRoomItemFor({"Por defecto", "2500", "5000", "10000"}, {}, {"5000"}), 2);
}

}

int main()
{
	NothingSaidIsTheCompetitiveRoomInSpring();
	EachPresetHasItsOwnOptions();
	TheSwitchesWinOverThePreset();
	TheSeasonIsSpringUnlessTheLeagueSaysOtherwise();
	TheRankedSeasonsSettingsAreTheRequest();
	TheCupsSettingsAreTheRequest();
	TheLogLineNamesEveryOption();
	TheRoomsItemsAreFoundInAnyLanguage();
	return test::Summary("leagueroomoptions");
}
