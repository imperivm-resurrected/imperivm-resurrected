// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "game/roomprefs.h"

static const unsigned kBase = 0x10000 | 0x20000;

static ModOption Season()
{
	ModOption o;
	o.id = kSeasonOptionId;
	o.values = {"Spring", "Autumn", "Winter"};
	o.width = 2;
	o.pos = 9;
	o.bit = kSeasonBits;
	return o;
}

static ModOption Make(const std::string &id, size_t values, int def, int &used)
{
	ModOption o;
	o.id = id;
	for (size_t i = 0; i < values; i++) o.values.push_back("v" + std::to_string(i));
	o.width = ModOptionWidth(values);
	o.def = def;
	ModOptionPlace(o, used);
	return o;
}

static void TextComesBackIntact()
{
	const std::string texts[] = {"5000", "Por defecto", "Mapa aleatorio: Islas peque\xc3\xb1" "as", "2 [cm] competitive conquest", "100%", " spaces ", "a=b;c\"d"};
	for (const std::string &t : texts) CHECK(RoomPrefDecode(RoomPrefEncode(t)) == t);
	for (const char c : RoomPrefEncode("Mapa peque\xc3\xb1o = 100% ; \"x\"")) {
		CHECK(static_cast<unsigned char>(c) < 0x80);
		CHECK(c != ' ' && c != '=' && c != ';' && c != '"');
	}
	CHECK(RoomPrefEncode("Mapa_1-(2).x:y") == "Mapa_1-(2).x:y");
	CHECK(RoomPrefDecode("100%") == "100%");
	CHECK(RoomPrefDecode("100%zz") == "100%zz");
}

static void FindsItemsByText()
{
	const std::vector<std::string> items = {"Por defecto", "2500", "5000"};
	CHECK_EQ(RoomPrefItemIndex(items, "5000"), 2);
	CHECK_EQ(RoomPrefItemIndex(items, "Por defecto"), 0);
	CHECK_EQ(RoomPrefItemIndex(items, "10000"), -1);
	CHECK_EQ(RoomPrefItemIndex({}, "5000"), -1);
}

static void NothingSavedUsesDefaults()
{
	int used = 0;
	const ModOption speed = Make("speed", 3, 2, used);
	const unsigned rules = RoomRulesFromSaved({Season(), speed}, false, 0, kBase, {});
	CHECK_EQ(ModOptionGet(speed, rules), 2);
	CHECK_EQ(ModOptionGet(Season(), rules), 0);
	CHECK_EQ(rules & kBase, 0u);
}

static void ValuesFollowTheIdWhenBitsMove()
{
	int used = 0;
	const ModOption flagBefore = Make("zeta", 0, 0, used);
	const std::map<std::string, int> values = {{kSeasonOptionId, 1}, {"zeta", 1}};
	const unsigned saved = RoomRulesFromSaved({Season(), flagBefore}, true, 0x10000, kBase, values);

	used = 0;
	const ModOption added = Make("alpha", 0, 0, used);
	const ModOption flagAfter = Make("zeta", 0, 0, used);
	CHECK(flagAfter.bit != flagBefore.bit);
	const unsigned rules = RoomRulesFromSaved({Season(), added, flagAfter}, true, saved, kBase, values);
	CHECK_EQ(ModOptionGet(flagAfter, rules), 1);
	CHECK_EQ(ModOptionGet(added, rules), 0);
	CHECK_EQ(ModOptionGet(Season(), rules), 1);
	CHECK_EQ(rules & kBase, 0x10000u);
}

static void NewOptionGetsItsDefault()
{
	int used = 0;
	const ModOption added = Make("added", 0, 1, used);
	const std::map<std::string, int> values = {{kSeasonOptionId, 2}};
	const unsigned rules = RoomRulesFromSaved({Season(), added}, true, 0, kBase, values);
	CHECK_EQ(ModOptionGet(added, rules), 1);
	CHECK_EQ(ModOptionGet(Season(), rules), 2);
}

static void RemovedOptionLeavesNothingBehind()
{
	const std::map<std::string, int> values = {{kSeasonOptionId, 0}, {"gone", 1}};
	CHECK_EQ(RoomRulesFromSaved({Season()}, true, 0x20000, kBase, values), 0x20000u);
}

static void OldRulesAreReadOnce()
{
	int used = 0;
	const ModOption flag = Make("flag", 0, 0, used);
	const unsigned old = 0x10000 | flag.bit | ModOptionSet(Season(), 0, 1);
	const unsigned rules = RoomRulesFromSaved({Season(), flag}, true, old, kBase, {});
	CHECK_EQ(ModOptionGet(flag, rules), 1);
	CHECK_EQ(ModOptionGet(Season(), rules), 1);
	CHECK_EQ(rules & kBase, 0x10000u);
}

static void OutOfRangeValueIsClamped()
{
	int used = 0;
	const ModOption level = Make("level", 3, 0, used);
	const std::map<std::string, int> values = {{kSeasonOptionId, 0}, {"level", 9}};
	CHECK_EQ(ModOptionGet(level, RoomRulesFromSaved({Season(), level}, true, 0, kBase, values)), 2);
}

int main()
{
	TextComesBackIntact();
	FindsItemsByText();
	NothingSavedUsesDefaults();
	ValuesFollowTheIdWhenBitsMove();
	NewOptionGetsItsDefault();
	RemovedOptionLeavesNothingBehind();
	OldRulesAreReadOnce();
	OutOfRangeValueIsClamped();
	return test::Summary("roomprefs");
}
