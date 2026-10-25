// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "net/modlist.h"

static ModInfo Mod(const std::string &id, int kind, const std::vector<std::string> &requires = {})
{
	ModInfo m;
	m.id = id;
	m.name = "Name of " + id;
	m.version = "1.2";
	m.kind = kind;
	m.gameplay = kind == kModRules;
	m.totalBytes = 1000 + id.size();
	m.redistributable = true;
	m.gbId = 42;
	for (const std::string &r : requires) m.requires.push_back({r, ""});
	return m;
}

static void RoundTrip()
{
	int optional = -1;
	const std::string text = EncodeModList({Mod("rules-a", kModRules), Mod("skins", kModCosmetic)}, "", optional);
	CHECK_EQ(optional, 1);
	const std::vector<OnlineMod> list = DecodeModList(text);
	CHECK_EQ(list.size(), size_t(1));
	if (list.size() != 1) return;
	CHECK_EQ(list[0].id, "rules-a");
	CHECK_EQ(list[0].name, "Name of rules-a");
	CHECK_EQ(list[0].version, "1.2");
	CHECK(list[0].gameplay);
	CHECK_EQ(list[0].kind, 2);
	CHECK_EQ(list[0].bytes, 1007ull);
	CHECK(list[0].redistributable);
	CHECK_EQ(list[0].gbId, 42ull);
}

static void Closure()
{
	int optional = 0;
	const std::vector<ModInfo> mounted = {
		Mod("lib-base", kModCosmetic),
		Mod("lib-mid", kModCosmetic, {"LIB-BASE"}),
		Mod("rules", kModRules, {"lib-mid"}),
		Mod("skins", kModCosmetic),
		Mod("map-pack", kModContent),
	};
	std::vector<OnlineMod> list = DecodeModList(EncodeModList(mounted, "", optional));
	CHECK_EQ(list.size(), size_t(3));
	CHECK_EQ(optional, 2);
	std::vector<std::string> ids;
	for (const OnlineMod &m : list) ids.push_back(m.id);
	CHECK_EQ(ids, (std::vector<std::string>{"lib-base", "lib-mid", "rules"}));

	list = DecodeModList(EncodeModList(mounted, "MAP-PACK", optional));
	CHECK_EQ(list.size(), size_t(4));
	CHECK_EQ(optional, 1);
	CHECK_EQ(list[3].id, "map-pack");
	CHECK_EQ(list[3].kind, 1);
	CHECK(!list[3].gameplay);
}

static void Nothing()
{
	int optional = 9;
	CHECK_EQ(EncodeModList({}, "", optional), "");
	CHECK_EQ(optional, 0);
	CHECK_EQ(EncodeModList({Mod("skins", kModCosmetic)}, "", optional), "");
	CHECK_EQ(optional, 1);
	CHECK(DecodeModList("").empty());
}

static void Forbidden()
{
	ModInfo m = Mod("rules", kModRules);
	m.forbidden.push_back("evil.exe");
	int optional = 0;
	const std::vector<OnlineMod> list = DecodeModList(EncodeModList({m}, "", optional));
	CHECK_EQ(list.size(), size_t(1));
	if (!list.empty()) CHECK(!list[0].redistributable);
}

static void Legacy()
{
	std::vector<OnlineMod> list = DecodeModList("old\tOld mod\t1.0\t1\t500\n");
	CHECK_EQ(list.size(), size_t(1));
	if (list.size() == 1) {
		CHECK_EQ(list[0].id, "old");
		CHECK(list[0].gameplay);
		CHECK_EQ(list[0].kind, 2);
		CHECK_EQ(list[0].bytes, 500ull);
		CHECK(!list[0].redistributable);
		CHECK_EQ(list[0].gbId, 0ull);
	}
	list = DecodeModList("cosm\tCosmetic\t1.0\t0\t10\t1\t7\n");
	CHECK_EQ(list.size(), size_t(1));
	if (list.size() == 1) {
		CHECK(!list[0].gameplay);
		CHECK_EQ(list[0].kind, 0);
		CHECK(list[0].redistributable);
		CHECK_EQ(list[0].gbId, 7ull);
	}
	list = DecodeModList("flag\tFlag\t1.0\t1\t10\t0\t0\t1\n");
	CHECK_EQ(list.size(), size_t(1));
	if (list.size() == 1) {
		CHECK_EQ(list[0].kind, 1);
		CHECK(!list[0].gameplay);
		CHECK_EQ(list[0].gbFile, 0ull);
	}
}

static void GbFile()
{
	ModInfo m = Mod("rules", kModRules);
	m.gbId = 730058;
	m.gbFile = 1856737;
	int optional = 0;
	const std::vector<OnlineMod> list = DecodeModList(EncodeModList({m}, "", optional));
	CHECK_EQ(list.size(), size_t(1));
	if (list.size() == 1) {
		CHECK_EQ(list[0].gbId, 730058ull);
		CHECK_EQ(list[0].gbFile, 1856737ull);
	}
}

static void CopyKey()
{
	ModInfo host = Mod("cm", kModRules);
	host.gameplayHash = "0123456789abcdef0123456789abcdef";
	int optional = 0;
	const std::vector<OnlineMod> list = DecodeModList(EncodeModList({host, Mod("skins", kModCosmetic), Mod("cosm", kModCosmetic)}, "COSM", optional));
	CHECK_EQ(list.size(), size_t(2));
	if (list.size() != 2) return;
	CHECK_EQ(list[0].copyKey, "0123456789abcdef");
	CHECK_EQ(list[1].copyKey, "");
	CHECK_EQ(DecodeModList("old\tOld\t1.0\t1\t5\t0\t0\t2\t0\n")[0].copyKey, "");

	ModInfo mine = host;
	CHECK(ModsCompareRoomCopy(mine, list[0].copyKey) == RoomCopy::kSame);
	mine.gameplayHash = "fedcba9876543210fedcba9876543210";
	CHECK(ModsCompareRoomCopy(mine, list[0].copyKey) == RoomCopy::kOther);
	CHECK(ModsCompareRoomCopy(mine, "") == RoomCopy::kSame);
	mine.gameplayHash.clear();
	CHECK(ModsCompareRoomCopy(mine, list[0].copyKey) == RoomCopy::kNotHashed);
	mine.gameplay = false;
	CHECK(ModsCompareRoomCopy(mine, list[0].copyKey) == RoomCopy::kOther);
	CHECK(ModsCompareRoomCopy(Mod("cosm", kModCosmetic), list[1].copyKey) == RoomCopy::kSame);
}

static void Malformed()
{
	CHECK(DecodeModList("only\ttwo\n").empty());
	CHECK(DecodeModList("\n\n\n").empty());
	CHECK_EQ(DecodeModList("a\tA\t1\t1\t5\n\nb\tB\t2\t0\t6").size(), size_t(2));
	CHECK_EQ(DecodeModList("a\tA\t1\t1\t5").size(), size_t(1));
	CHECK_EQ(DecodeModList("a\tA\t1\t1\tnotanumber\n")[0].bytes, 0ull);
}

int main()
{
	RoundTrip();
	Closure();
	Nothing();
	Forbidden();
	Legacy();
	GbFile();
	CopyKey();
	Malformed();
	return test::Summary("modlist");
}
