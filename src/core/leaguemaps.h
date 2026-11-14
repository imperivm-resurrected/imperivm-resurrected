// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The maps of the league (imperivm-league, src/maps.ts) by the ids the game itself goes by. A random map type is its template file,
// data/RandomMap/<id>.rm.xml (lower case, spaces as underscores: "mountain_passes"), and a size is the map-size of the template's
// limits, the order of the room's size list: "small", "medium", "large". The room's lists show the game's translated names; the
// ids behind their items are read from the game (game/gameroom.cpp), and their text only serves when those can't be read.

// The id of a template file: "data/RandomMap/MOUNTAIN PASSES.RM.XML" -> "mountain_passes". Empty when it is not a template file.
std::string LeagueMapIdOfTemplate(const std::string &path);
// The id of the item at `index` of the room's size list (0 small, 1 medium, 2 large); empty past them.
std::string LeagueMapSizeIdAt(int index);
// Whether the text of an item of the room's map type or size list ("Mapa aleatorio: Montañoso", "Pequeño") names the id, in the
// game's English, Spanish or Italian. Accents and case are ignored; an id it does not know matches by its words.
bool LeagueMapTextMatches(const std::string &item, const std::string &id);
// The item of a room list that is `id`: the one whose id behind it (`keys`, one per item, empty where unknown) is `id`, else the
// one whose text names it; items marked in `disabled` are never chosen. -1 when neither finds it; `byText` says the text was used.
int LeagueMapItemFor(const std::vector<std::string> &keys, const std::vector<std::string> &items, const std::vector<char> &disabled,
	const std::string &id, bool &byText);
// The English name of a map type or size ("Mountainous", "Normal"), the key of its translation; null for an id it doesn't know.
const char *LeagueMapName(const std::string &id);
// The random map types in the order of the room's list, and the sizes each lets two players in on, as the server's list
// (src/maps.ts): small only on "continent", "island" and "randommap".
std::vector<std::string> LeagueMapTypes();
std::vector<std::string> LeagueMapSizesOf(const std::string &type);
// A type and a size the league accepts for a tournament round.
bool LeagueMapValid(const std::string &type, const std::string &size);
