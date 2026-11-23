// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "game/mapinfo.h"
#include "hpfs_builder.h"

namespace {
std::string Grid(int units, int cell, int bits, const std::vector<unsigned char> &cells)
{
	std::string out = "DIRG";
	const uint32_t head[4] = {static_cast<uint32_t>(cell), static_cast<uint32_t>(bits), static_cast<uint32_t>(units), static_cast<uint32_t>(units)};
	out.append(reinterpret_cast<const char *>(head), sizeof(head));
	out.append(cells.begin(), cells.end());
	return out;
}

std::string Player(int id, const std::string &race, const std::string &control, int x, int y, int color)
{
	return "<playerdata\n\tid=\"" + std::to_string(id) + "\"\n\tname=\"P\"\n\trace=\"" + race + "\"\n\tAllowedRaces=\"All\"\n\tcontrol=\"" + control +
		"\"\n\tstartx=\"" + std::to_string(x) + "\"\n\tstarty=\"" + std::to_string(y) + "\"\n\tcolor=\"" + std::to_string(color) + "\"/>";
}

std::string Objects()
{
	return "<objects>\n"
		"<settlement\n\tid=\"0\"\n\tplayer=\"1\"\n\tclassoffirstbuilding=\"ITownhall\">\n"
		"<scriptobj\n\tclass=\"IGateNW\"\n\tnum=\"1\"\n\tx=\"50\"\n\ty=\"60\"\n\tdir.x=\"0\"/>\n"
		"<scriptobj\n\tclass=\"ITownhall\"\n\tx=\"700\"\n\ty=\"300\"\n\tdir.x=\"7\"/>\n"
		"</settlement>\n"
		"<settlement player=\"2\" classoffirstbuilding=\"EVillage\"><scriptobj class=\"EVillage\" x=\"400\" y=\"500\"/></settlement>\n"
		"<settlement player=\"3\" classoffirstbuilding=\"MTower\"><scriptobj class=\"MWall\" x=\"11\" y=\"12\"/><scriptobj class=\"MTower\" x=\"21\" y=\"22\"/></settlement>\n"
		"<settlement player=\"3\" classoffirstbuilding=\"MTownhall\"><scriptobj class=\"MTownhall\" x=\"30\" y=\"40\"/></settlement>\n"
		"<settlement player=\"5\" classoffirstbuilding=\"EVillage\"><scriptobj class=\"EVillage\" x=\"1\" y=\"2\"/></settlement>\n"
		"<settlement player=\"5\" classoffirstbuilding=\"MutableStronghold\"><scriptobj class=\"MutableStronghold\" x=\"60\" y=\"70\"/></settlement>\n"
		"</objects>";
}

std::vector<unsigned char> Map(bool withHeights, const std::string &objects = Objects())
{
	std::vector<unsigned char> terrain(16 * 16, 3);
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 8; x++) terrain[y * 16 + x] = 12;
	}
	std::vector<unsigned char> height(32 * 32, 100);
	std::vector<HpfsBuilder::Node> maps = {
		HpfsBuilder::File("map.xml", "<map name=\"New Map\"><size x=\"1024\" y=\"1024\"/></map>"),
		HpfsBuilder::File("map.obj.xml", objects),
		HpfsBuilder::File("Terrain.terrain.grid", Grid(1024, 64, 8, terrain)),
	};
	if (withHeights) maps.push_back(HpfsBuilder::File("Terrain.height.grid", Grid(1024, 32, 8, height)));
	HpfsBuilder b;
	return b.Build({
		HpfsBuilder::File("game.xml",
			"<game><properties game_type=\"0\" name=\"Test Map\" victory_condition=\"1 Elimination\" start_map=\"1\" season=\"winter\"/></game>"),
		HpfsBuilder::File("player0.xml", Player(0, "Mutable", "Both", 100, 200, 31810)),
		HpfsBuilder::File("player1.xml", Player(1, "Gaul", "Both", 900, 800, 992)),
		HpfsBuilder::File("player2.xml", Player(2, "Germany", "Computer", 0, 0, 957)),
		HpfsBuilder::File("player3.xml", Player(3, "Egypt", "Disabled", 0, 0, 0)),
		HpfsBuilder::File("player4.xml", Player(4, "Carthage", "Both", 0, 0, 31)),
		HpfsBuilder::File("player5.xml", Player(5, "Egypt", "Both", 900, 800, 31)),
		HpfsBuilder::Dir("Maps", {HpfsBuilder::Dir("1", maps)}),
	});
}

void ParsesTheMap()
{
	const std::vector<unsigned char> bytes = Map(true);
	MapInfo info;
	CHECK(MapInfoParse(bytes.data(), bytes.size(), info));
	CHECK_EQ(info.name, std::string("Test Map"));
	CHECK_EQ(info.victory, std::string("1 Elimination"));
	CHECK_EQ(info.season, std::string("winter"));
	CHECK_EQ(info.units, 1024);
	CHECK_EQ(info.cols, 16);
	CHECK_EQ(info.rows, 16);
	CHECK_EQ(info.heightCols, 32);
	CHECK_EQ(info.terrain.size(), static_cast<size_t>(256));
	CHECK_EQ(info.players.size(), static_cast<size_t>(5));
	CHECK_EQ(MapHumanSlots(info), 4);
	CHECK_EQ(info.players[0].x, 700);
	CHECK_EQ(info.players[0].y, 300);
	CHECK_EQ(info.players[1].x, 400);
	CHECK_EQ(info.players[1].y, 500);
	CHECK(!info.players[4].hasStart);
	CHECK(info.players[0].hasStart);
	CHECK_EQ(info.players[0].color, static_cast<uint32_t>(0xff1010));
	CHECK_EQ(info.players[1].race, std::string("Gaul"));
	CHECK(!info.players[2].human);
	CHECK(info.players[2].hasStart);
	CHECK_EQ(info.players[2].x, 30);
	CHECK_EQ(info.players[2].y, 40);
	CHECK(info.players[3].hasStart);
	CHECK_EQ(info.players[3].x, 60);
	CHECK_EQ(info.players[3].y, 70);
	MapInfoDropGrids(info);
	CHECK(info.terrain.empty());
	CHECK_EQ(info.name, std::string("Test Map"));
}

void CameraOnlyWithoutSettlements()
{
	const std::vector<unsigned char> bytes = Map(false, "<objects></objects>");
	MapInfo info;
	CHECK(MapInfoParse(bytes.data(), bytes.size(), info));
	CHECK(info.players[0].hasStart);
	CHECK_EQ(info.players[0].x, 100);
	CHECK_EQ(info.players[0].y, 283);
	CHECK(!info.players[2].hasStart);
}

void SurvivesMissingParts()
{
	HpfsBuilder b;
	const std::vector<unsigned char> onlyGame =
		b.Build({HpfsBuilder::File("game.xml", "<game><properties name=\"Bare\"/></game>")});
	MapInfo info;
	CHECK(MapInfoParse(onlyGame.data(), onlyGame.size(), info));
	CHECK_EQ(info.name, std::string("Bare"));
	CHECK_EQ(info.cols, 0);
	CHECK(MapRender(info, {}, 256).bgra.empty());
	const std::vector<unsigned char> garbage(2000, 7);
	CHECK(!MapInfoParse(garbage.data(), garbage.size(), info));
	const std::vector<unsigned char> noHeights = Map(false);
	CHECK(MapInfoParse(noHeights.data(), noHeights.size(), info));
	CHECK_EQ(info.heightCols, 0);
	std::vector<unsigned char> fourBits = Map(false);
	HpfsBuilder c;
	const std::vector<unsigned char> odd = c.Build({HpfsBuilder::File("game.xml", "<properties name=\"x\"/>"),
		HpfsBuilder::Dir("Maps", {HpfsBuilder::Dir("1", {HpfsBuilder::File("Terrain.terrain.grid", Grid(1024, 64, 4, std::vector<unsigned char>(128, 1)))})})});
	CHECK(MapInfoParse(odd.data(), odd.size(), info));
	CHECK_EQ(info.cols, 0);
}

void Renders()
{
	const std::vector<unsigned char> bytes = Map(false);
	MapInfo info;
	CHECK(MapInfoParse(bytes.data(), bytes.size(), info));
	std::vector<MapLayerColor> layers(16);
	layers[3].rgb = 0x00ff00;
	layers[12].rgb = 0x0000ff;
	layers[12].water = true;
	MapImage full = MapRender(info, layers, 256);
	CHECK_EQ(full.w, 16);
	CHECK_EQ(full.h, 16);
	CHECK_EQ(full.bgra[0], 0xff0000ffu);
	CHECK_EQ(full.bgra[15], 0xff00ff00u);
	MapImage half = MapRender(info, layers, 8);
	CHECK_EQ(half.w, 8);
	CHECK_EQ(half.bgra[0], 0xff0000ffu);
	CHECK_EQ(half.bgra[7], 0xff00ff00u);
	CHECK(MapRender(info, layers, 4).bgra.empty());
	CHECK(MapRender(info, layers, 2).bgra.empty());
	const std::vector<unsigned char> relief = Map(true);
	MapInfo hills;
	CHECK(MapInfoParse(relief.data(), relief.size(), hills));
	for (size_t i = 0; i < hills.height.size(); i++) hills.height[i] = static_cast<uint8_t>((i % 32) * 4);
	MapImage shaded = MapRender(hills, layers, 256);
	CHECK_EQ(shaded.bgra[0], 0xff0000ffu);
	CHECK(shaded.bgra[15] != 0xff00ff00u);
	std::vector<MapLayerColor> unknown;
	const MapImage grey = MapRender(info, unknown, 256);
	CHECK_EQ(grey.bgra[0], 0xff808080u);
}

void Colors()
{
	CHECK_EQ(MapRgb555(0), static_cast<uint32_t>(0));
	CHECK_EQ(MapRgb555(0x7fff), static_cast<uint32_t>(0xffffff));
	CHECK_EQ(MapRgb555(31 << 10), static_cast<uint32_t>(0xff0000));
	std::string bmp(54 + 8 + 8, '\0');
	bmp[0] = 'B';
	bmp[1] = 'M';
	bmp[10] = 54;
	bmp[14] = 40;
	bmp[18] = 2;
	bmp[22] = 2;
	bmp[26] = 1;
	bmp[28] = 24;
	const unsigned char px[2][8] = {{10, 20, 30, 50, 60, 70, 0, 0}, {30, 40, 50, 70, 80, 90, 0, 0}};
	for (int r = 0; r < 2; r++) memcpy(&bmp[54 + r * 8], px[r], 8);
	uint32_t rgb = 0;
	CHECK(BmpAverageColor(reinterpret_cast<const unsigned char *>(bmp.data()), bmp.size(), rgb));
	CHECK_EQ(rgb, static_cast<uint32_t>((60 << 16) | (50 << 8) | 40));
	CHECK(!BmpAverageColor(reinterpret_cast<const unsigned char *>(bmp.data()), 60, rgb));
	// An offset near 4 GB: offset + 16 bytes of pixels wraps round to 0 on 32 bits.
	std::string wrap = bmp;
	wrap[10] = static_cast<char>(0xf0);
	wrap[11] = wrap[12] = wrap[13] = static_cast<char>(0xff);
	CHECK(!BmpAverageColor(reinterpret_cast<const unsigned char *>(wrap.data()), wrap.size(), rgb));
	bmp[28] = 8;
	CHECK(!BmpAverageColor(reinterpret_cast<const unsigned char *>(bmp.data()), bmp.size(), rgb));
}

void Layers()
{
	const std::string xml =
		"<terrain>\n<layer \n\tz=\"3\" type=\"1\" display=\"Grass 1\"\n\timage=\"terrain/%season%/grass1024.vq\"\n\tminimap=\"%season%/grass1024.bmp\"\n\t/>\n"
		"<layer z=\"12\" type=\"4\" minimap=\"swater.bmp\"/>\n<layer type=\"2\"/>\n</terrain>";
	const std::vector<MapLayerDef> defs = MapLayerDefs(xml);
	CHECK_EQ(defs.size(), static_cast<size_t>(2));
	CHECK_EQ(defs[0].z, 3);
	CHECK_EQ(defs[0].type, 1);
	CHECK_EQ(MapLayerTilePath(defs[0], "Winter"), std::string("winter/grass1024.bmp"));
	CHECK_EQ(MapLayerTilePath(defs[0], ""), std::string("spring/grass1024.bmp"));
	CHECK_EQ(MapLayerTilePath(defs[1], "winter"), std::string("swater.bmp"));
	CHECK_EQ(std::string(MapSizeName(8192)), std::string("Small"));
	CHECK_EQ(std::string(MapSizeName(16384)), std::string("Medium"));
	CHECK_EQ(std::string(MapSizeName(32768)), std::string("Large"));
	CHECK_EQ(std::string(MapSizeName(0)), std::string(""));
}
}

void Description()
{
	const std::string full = "Mapa: Mediterraneo\nTamano del mapa: Grande\nVictoria: Eliminacion\n\n\nDescripcion del mapa: Hispania compite por el dominio.\n\nJugador 1: Galos\njugador 2: Galos\n";
	CHECK_EQ(MapDescriptionBody(full, true), std::string("Hispania compite por el dominio."));
	CHECK_EQ(MapDescriptionBody(full, false), std::string("Hispania compite por el dominio.\n\nJugador 1: Galos\njugador 2: Galos"));
	CHECK_EQ(MapDescriptionBody("Just a text without sections.", true), std::string("Just a text without sections."));
	CHECK_EQ(MapDescriptionBody("", true), std::string(""));
	CHECK_EQ(MapDescriptionBody("Mapa: A\nTamano: B\n", true), std::string("Mapa: A\nTamano: B"));
	CHECK_EQ(MapDescriptionBody("Note: first.\nSecond line.\n\nOther paragraph.", true), std::string("Note: first.\nSecond line.\n\nOther paragraph."));
	CHECK_EQ(MapDescriptionBody("Map description: first.\nSecond line.", true), std::string("first.\nSecond line."));
}

void PlayerNames()
{
	const std::string full = "Mapa: X\nVictoria: Y\n\nDescripcion del mapa: Texto.\n\nJugador 1: Galos\njugador 2: R.Imperial\nJugador 10: Cartago\n";
	const std::vector<std::pair<int, std::string>> lines = MapPlayerLines(full);
	CHECK_EQ(lines.size(), static_cast<size_t>(3));
	if (lines.size() == 3) {
		CHECK_EQ(lines[0].first, 1);
		CHECK_EQ(lines[0].second, std::string("Galos"));
		CHECK_EQ(lines[1].second, std::string("R.Imperial"));
		CHECK_EQ(lines[2].first, 10);
	}
	CHECK(MapPlayerLines("Mapa: X\n\nSolo texto: con dos puntos").empty());
	CHECK(MapPlayerLines("Jugador 1: Galos").empty());
	CHECK(MapPlayerLines("").empty());
	CHECK(MapPlayerLines("Texto\n\nJugador 1: Galos\nOtra cosa: x").empty());
}

int main()
{
	ParsesTheMap();
	SurvivesMissingParts();
	Renders();
	Colors();
	Layers();
	Description();
	PlayerNames();
	CameraOnlyWithoutSettlements();
	return test::Summary("mapinfo");
}
