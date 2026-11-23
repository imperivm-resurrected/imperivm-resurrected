// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct MapPlayer {
	int slot = 0;
	std::string race;
	bool human = false;
	bool hasStart = false;
	int x = 0;
	int y = 0;
	uint32_t color = 0;
	// Its settlements and its objects on the start map (units, buildings and settlements), to tell a place made to play from one for show.
	int settlements = 0;
	int objects = 0;
};

struct MapInfo {
	bool ok = false;
	std::string name;
	std::string victory;
	std::string season;
	// The scenario's own text, as game.xml has it (its "\n" made line breaks).
	std::string description;
	// game.xml's single_only: the game leaves these out of the multiplayer selector.
	bool singleOnly = false;
	int gameType = 0;
	int units = 0;
	std::vector<MapPlayer> players;
	int cols = 0;
	int rows = 0;
	std::vector<uint8_t> terrain;
	int heightCols = 0;
	int heightRows = 0;
	std::vector<uint8_t> height;
};

struct MapLayerColor {
	uint32_t rgb = 0x808080;
	bool water = false;
	bool known = false;
};

struct MapLayerDef {
	int z = 0;
	int type = 0;
	std::string tile;
};

struct MapImage {
	int w = 0;
	int h = 0;
	std::vector<uint32_t> bgra;
};

std::vector<MapLayerDef> MapLayerDefs(const std::string &terrainsXml);
std::string MapLayerTilePath(const MapLayerDef &layer, const std::string &season);
bool MapInfoParse(const unsigned char *data, size_t size, MapInfo &out);
void MapInfoDropGrids(MapInfo &info);
uint32_t MapRgb555(unsigned value);
bool BmpAverageColor(const unsigned char *data, size_t size, uint32_t &rgb);
MapImage MapRender(const MapInfo &info, const std::vector<MapLayerColor> &layers, int maxSide);
const char *MapSizeName(int units);
int MapHumanSlots(const MapInfo &info);
// The text of every sequence script of the scenario (Maps/N/Sequences/*.vs), at most 4 MB in all.
std::vector<std::string> MapSequenceScripts(const unsigned char *data, size_t size);
std::vector<std::pair<int, std::string>> MapPlayerLines(const std::string &gameText);
std::string MapDescriptionBody(const std::string &gameText, bool dropPlayerLines);
