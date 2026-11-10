// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <string>
#include <vector>

// The game's help (currentlang/help.xml): <help> holds nested <topic id> elements, and each topic its <entry> elements with the
// attributes font="large", hcenter, vcenter, image, link and vspacing. Texts are UTF-8, with their lines trimmed.
struct HelpEntry {
	std::string text, image, link;
	bool large = false, hcenter = false, vcenter = false;
	int vspacing = -1;
	// The topic the link leads to, or -1: no link, a web address (web) or a topic that does not exist.
	int target = -1;
	bool web = false;
};

struct HelpTopic {
	std::string id;
	int parent = -1;
	std::vector<int> children;
	std::vector<HelpEntry> entries;
};

struct HelpDoc {
	std::vector<HelpTopic> topics;
	// The topics directly under <help>; the first is where the game starts.
	std::vector<int> roots;
};

HelpDoc ParseHelpXml(const std::string &xml);
// As the game resolves a link: a path of ids among the children of the topic, or from the top with a leading '/'. A link the game
// would not find still leads to the only topic with that id, if there is one.
int HelpResolve(const HelpDoc &doc, int from, const std::string &link);
// "/contents/units/RHastatus".
std::string HelpPath(const HelpDoc &doc, int topic);

// A listing is a topic of headings, each followed by links with pictures: the units and the structures, one section per
// civilization. Entries [first, end) belong to the section, its heading at first.
struct HelpSection {
	std::string heading;
	int first = 0, end = 0;
};

struct HelpNavItem {
	std::string label;
	int topic = -1;
	// One row per section when the topic is a listing.
	std::vector<std::string> sections;
};

struct HelpNavGroup {
	std::string label;
	std::vector<HelpNavItem> items;
};

struct HelpIndex {
	int home = -1;
	// The side bar: the headings of the first topic and the links under each one.
	std::vector<HelpNavGroup> groups;
	// One per topic. The home's is empty: the screen names it.
	std::vector<std::string> titles;
	// One per topic, empty for a topic that is not a listing.
	std::vector<std::vector<HelpSection>> sections;
	// The listing and section whose links lead to each topic, or -1.
	std::vector<int> listing, section;
	// Per topic, its paragraphs (not its links) and their folded copies, for the search.
	std::vector<std::vector<std::string>> paragraphs, folded;
	std::vector<std::string> foldedTitles;
	// The page of keys and controls ("Espacio - Muestra el mapa" lines), or -1.
	int keys = -1;
};

HelpIndex BuildHelpIndex(const HelpDoc &doc);
// "Espacio - Muestra el mapa": the key and what it does.
bool HelpSplitKey(const std::string &text, std::string &key, std::string &what);
// Most of the plain one-line entries of the topic are keys.
bool HelpIsKeyTopic(const HelpDoc &doc, int topic);

// A page of the screen: a topic, a section of a listing, or the results of a search when query is not empty.
struct HelpPlace {
	int topic = -1, section = -1;
	std::string query;
	bool operator==(const HelpPlace &o) const { return topic == o.topic && section == o.section && query == o.query; }
	bool operator!=(const HelpPlace &o) const { return !(*this == o); }
};

struct HelpHistory {
	std::vector<HelpPlace> places;
	int at = -1;
	// Drops the places ahead, as a browser does. Going to the current place does nothing.
	void Go(const HelpPlace &place);
	void Replace(const HelpPlace &place);
	bool CanBack() const { return at > 0; }
	bool CanForward() const { return at >= 0 && at + 1 < static_cast<int>(places.size()); }
	bool Back();
	bool Forward();
	const HelpPlace *Current() const { return at >= 0 && at < static_cast<int>(places.size()) ? &places[at] : nullptr; }
};

struct HelpCrumb {
	std::string label;
	HelpPlace place;
};
// From the home to the place; the home's label is empty.
std::vector<HelpCrumb> HelpCrumbs(const HelpDoc &doc, const HelpIndex &index, const HelpPlace &place);

// The row of the side bar that stands for the place, or group -1.
struct HelpNavSpot {
	int group = -1, item = -1, section = -1;
};
HelpNavSpot HelpNavFor(const HelpDoc &doc, const HelpIndex &index, const HelpPlace &place);

// Lower case without accents, for comparing what the player types: "Élite" and "elite" fold alike.
std::string HelpFold(const std::string &utf8);

struct HelpHit {
	int topic = -1, section = -1;
	int score = 0;
	std::string snippet;
	// For a hit that is not a whole page (a row of the keys): what to show instead of the page's title and path.
	std::string title, context;
};
// Every word must appear in the title or the text of the topic; titles weigh more. Ordered by score, then as in the file.
std::vector<HelpHit> HelpSearch(const HelpDoc &doc, const HelpIndex &index, const std::string &query, size_t limit = 60);

// Lines a page shows that are not in the file (the player's keys): every word of the query must be in its text.
struct HelpRow {
	std::string title, context, text;
};
std::vector<HelpHit> HelpSearchRows(const std::vector<HelpRow> &rows, int topic, const std::string &query);
// The hits of both, by score, then pages before rows.
std::vector<HelpHit> HelpMergeHits(std::vector<HelpHit> pages, const std::vector<HelpHit> &rows, size_t limit = 60);

// data/CounterUnits.xml, the table the computer players recruit from: for each enemy class, the classes that answer it and how much.
struct HelpCounter {
	std::string cls;
	int coeff = 0;
};
struct HelpCounters {
	// Lower-case class -> the classes that counter it, as written.
	std::map<std::string, std::vector<HelpCounter>> of;
};
HelpCounters ParseCounterUnits(const std::string &xml);

struct HelpMatchups {
	// The classes it counters and the ones that counter it, the strongest first.
	std::vector<HelpCounter> strong, weak;
};
HelpMatchups HelpMatchupsFor(const HelpCounters &counters, const std::string &cls);

// What the game loaded for the class of a unit page; -1 when it does not have it.
struct HelpUnitStats {
	std::string classId;
	int health = -1, stamina = -1, damage = -1, damageType = -1, armorSlash = -1, armorPierce = -1, speed = -1, sight = -1, range = -1;
	int costGold = -1, costFood = -1, costPop = -1, trainMs = -1;
	// The topics of the buildings that recruit it.
	std::vector<int> recruitedAt;
};

enum HelpDamageType { kHelpDamageSlash = 0, kHelpDamagePierce = 1, kHelpDamageSiege = 2 };
// Missiles and siege: the range is worth showing.
bool HelpUnitIsRanged(const HelpUnitStats &s);
