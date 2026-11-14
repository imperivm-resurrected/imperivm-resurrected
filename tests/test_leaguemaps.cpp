// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "core/leaguemaps.h"

namespace {

void Ids()
{
	// The paths the game keeps for its templates, as its folder scan and its old automatch write them.
	CHECK_EQ(LeagueMapIdOfTemplate("data/RandomMap/MOUNTAIN PASSES.RM.XML"), std::string("mountain_passes"));
	CHECK_EQ(LeagueMapIdOfTemplate("data/randommap/Mountain passes.rm.xml"), std::string("mountain_passes"));
	CHECK_EQ(LeagueMapIdOfTemplate("data\\RandomMap\\coastal.rm.xml"), std::string("coastal"));
	CHECK_EQ(LeagueMapIdOfTemplate("LARGE ISLANDS.RM.XML"), std::string("large_islands"));
	// A mod's own template.
	CHECK_EQ(LeagueMapIdOfTemplate("data/RandomMap/Anillo.rm.xml"), std::string("anillo"));
	CHECK_EQ(LeagueMapIdOfTemplate("data/RandomMap/terrainmix.xml"), std::string());
	CHECK_EQ(LeagueMapIdOfTemplate(".rm.xml"), std::string());
	CHECK_EQ(LeagueMapIdOfTemplate(""), std::string());

	CHECK_EQ(LeagueMapSizeIdAt(0), std::string("small"));
	CHECK_EQ(LeagueMapSizeIdAt(1), std::string("medium"));
	CHECK_EQ(LeagueMapSizeIdAt(2), std::string("large"));
	CHECK_EQ(LeagueMapSizeIdAt(3), std::string());
	CHECK_EQ(LeagueMapSizeIdAt(-1), std::string());

	CHECK_EQ(std::string(LeagueMapName("mountain_passes")), std::string("Mountainous"));
	CHECK_EQ(std::string(LeagueMapName("Medium")), std::string("Normal"));
	CHECK(!LeagueMapName("atlantis"));
}

// The room's lists as the game fills them in Spanish, with the ids behind the items read from the game.
const std::vector<std::string> kTypes = {"Mapa predefinido", "Mapa aleatorio: Costero", "Mapa aleatorio: Continente", "Mapa aleatorio: Isla",
	"Mapa aleatorio: Islas grandes", "Mapa aleatorio: Mediterr\xC3\xA1neo", "Mapa aleatorio: Monta\xC3\xB1oso", "Mapa aleatorio: Aleatorio",
	"Mapa aleatorio: Islas peque\xC3\xB1" "as", "Mapa aleatorio: Torneo"};
const std::vector<std::string> kTypeKeys = {"", "coastal", "continent", "island", "large_islands", "mediterranean", "mountain_passes", "randommap",
	"small_islands", "tournament"};
const std::vector<std::string> kSizes = {"Peque\xC3\xB1o", "Normal", "Grande"};
const std::vector<std::string> kSizeKeys = {"small", "medium", "large"};

void ById()
{
	bool byText = true;
	CHECK_EQ(LeagueMapItemFor(kTypeKeys, kTypes, {}, "mountain_passes", byText), 6);
	CHECK(!byText);
	CHECK_EQ(LeagueMapItemFor(kTypeKeys, kTypes, {}, "island", byText), 3);
	CHECK_EQ(LeagueMapItemFor(kTypeKeys, kTypes, {}, "randommap", byText), 7);
	CHECK_EQ(LeagueMapItemFor(kSizeKeys, kSizes, {}, "medium", byText), 1);
	CHECK(!byText);
	// A size the chosen type does not offer is never picked.
	CHECK_EQ(LeagueMapItemFor(kSizeKeys, kSizes, {1, 0, 0}, "small", byText), -1);
	CHECK_EQ(LeagueMapItemFor(kTypeKeys, kTypes, {}, "atlantis", byText), -1);
	CHECK_EQ(LeagueMapItemFor(kTypeKeys, kTypes, {}, "", byText), -1);
	// The ids win over a text that would say otherwise.
	const std::vector<std::string> swapped = {"", "coastal", "continent"};
	CHECK_EQ(LeagueMapItemFor(swapped, {"Mapa predefinido", "Mapa aleatorio: Continente", "Mapa aleatorio: Costero"}, {}, "continent", byText), 2);
}

void ByText()
{
	// Without the ids, the game's names in its three languages.
	bool byText = false;
	const std::vector<std::string> none;
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "mountain_passes", byText), 6);
	CHECK(byText);
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "island", byText), 3);
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "large_islands", byText), 4);
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "small_islands", byText), 8);
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "randommap", byText), 7);
	CHECK_EQ(LeagueMapItemFor(none, kTypes, {}, "mediterranean", byText), 5);
	CHECK_EQ(LeagueMapItemFor(none, kSizes, {}, "medium", byText), 1);
	CHECK_EQ(LeagueMapItemFor(none, kSizes, {}, "large", byText), 2);

	CHECK(LeagueMapTextMatches("Random Map : Mountainous", "mountain_passes"));
	CHECK(LeagueMapTextMatches("Mappa casuale: Montagnosa", "mountain_passes"));
	CHECK(LeagueMapTextMatches("Mapa aleatorio: Pasos de monta\xC3\xB1" "a", "mountain_passes"));
	CHECK(LeagueMapTextMatches("Random Map : RandomMap", "randommap"));
	CHECK(LeagueMapTextMatches("Mappa casuale: Casuale", "randommap"));
	CHECK(!LeagueMapTextMatches("Mappa casuale: Costiera", "randommap"));
	CHECK(LeagueMapTextMatches("Mappa casuale: Costiera", "coastal"));
	CHECK(LeagueMapTextMatches("Mappa casuale: Isola", "island"));
	CHECK(!LeagueMapTextMatches("Mappa casuale: Isole grandi", "island"));
	CHECK(LeagueMapTextMatches("Mappa casuale: Isole piccole", "small_islands"));
	CHECK(!LeagueMapTextMatches("Random Map : Continent", "mountain_passes"));
	// The game's Windows-1252 text as well as UTF-8.
	CHECK(LeagueMapTextMatches("Mapa aleatorio: Mediterr\xE1neo", "mediterranean"));
	CHECK(LeagueMapTextMatches("Peque\xF1o", "small"));
	CHECK(LeagueMapTextMatches("Piccola", "small"));
	CHECK(LeagueMapTextMatches("Normale", "medium"));
	CHECK(LeagueMapTextMatches("Huge", "large"));
	CHECK(LeagueMapTextMatches("Grande", "large"));
	CHECK(!LeagueMapTextMatches("Muy grande", "large"));
	CHECK(!LeagueMapTextMatches("Anything", ""));
	// An id the table does not know still matches by its words.
	CHECK(LeagueMapTextMatches("Random map: Great Steppe", "great_steppe"));
}

}

void Choices()
{
	const std::vector<std::string> types = LeagueMapTypes();
	CHECK_EQ(static_cast<int>(types.size()), 9);
	CHECK(!types.empty() && types.front() == "coastal");
	CHECK(!types.empty() && types.back() == "tournament");
	for (const std::string &t : types) CHECK(LeagueMapName(t) != nullptr);
	CHECK(LeagueMapSizesOf("coastal") == std::vector<std::string>({"medium", "large"}));
	CHECK(LeagueMapSizesOf("island") == std::vector<std::string>({"small", "medium", "large"}));
	CHECK(LeagueMapSizesOf("randommap") == std::vector<std::string>({"small", "medium", "large"}));
	CHECK(LeagueMapSizesOf("empty").empty());
	CHECK(LeagueMapSizesOf("medium").empty());
	CHECK(LeagueMapValid("continent", "small"));
	CHECK(LeagueMapValid("tournament", "large"));
	CHECK(!LeagueMapValid("mediterranean", "small"));
	CHECK(!LeagueMapValid("mediterranean", "huge"));
	CHECK(!LeagueMapValid("", "medium"));
}

int main()
{
	Ids();
	ById();
	ByText();
	Choices();
	return test::Summary("leaguemaps");
}
