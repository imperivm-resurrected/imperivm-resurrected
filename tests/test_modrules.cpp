// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/sha256.h"
#include "mods/modrules.h"

using namespace mods_impl;

static void Sha()
{
	CHECK_EQ(HashString(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK_EQ(HashString("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_EQ(HashString("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	Sha256 split;
	split.Add("ab");
	split.Add(std::string("c"));
	CHECK_EQ(split.Hex(), HashString("abc"));
}

static void Files()
{
	const std::string dir = test::TempDir("rules");
	const std::string shortFile = dir + "\\short.txt";
	const std::string longFile = dir + "\\long.bin";
	test::WriteFile(shortFile, "abc");
	test::WriteFile(longFile, std::string(3 * 1024 * 1024 + 17, 'x'));
	CHECK_EQ(HashFile(shortFile), HashString("abc"));
	CHECK_EQ(HashFile(longFile), HashString(std::string(3 * 1024 * 1024 + 17, 'x')));
	CHECK_EQ(HashFile(dir + "\\missing.txt"), "");
	test::RemoveTree(dir);
}

static void Helpers()
{
	CHECK_EQ(Lower("AbC.XML"), "abc.xml");
	CHECK_EQ(NormPath("\\Data\\Commands\\Foo.XML"), "data/commands/foo.xml");
	CHECK_EQ(NormPath("///Units/A.xml"), "units/a.xml");
	CHECK_EQ(NormPath(""), "");
	CHECK(StartsWith("data/x", "data/"));
	CHECK(!StartsWith("dat", "data/"));
	CHECK(EndsWith("a.xml", ".xml"));
	CHECK(!EndsWith("xml", ".xml"));
	CHECK_EQ(SafeFileName("a/b\\c:d*e?f\"g<h>i|j"), "a_b_c_d_e_f_g_h_i_j");
	CHECK_EQ(SafeFileName(""), "profile");
	CHECK_EQ(MakeId("My Cool Mod!"), "my-cool-mod");
	CHECK_EQ(MakeId("  --  "), "mod");
	CHECK_EQ(MakeId("Roman_Legions v2"), "roman-legions-v2");
	CHECK_EQ(MakeId("abc"), "abc");
}

static void Extensions()
{
	CHECK(AllowedExtension("data/commands/barracks.xml"));
	CHECK(AllowedExtension("sound/music/theme.ogg"));
	CHECK(AllowedExtension("packs/mine.bfhp"));
	CHECK(AllowedExtension("readme"));
	CHECK(AllowedExtension("folder.d/readme"));
	CHECK(AllowedExtension("custom.unknownext"));
	CHECK(!AllowedExtension("tools/run.exe"));
	CHECK(!AllowedExtension("hook.dll"));
	CHECK(!AllowedExtension("a/b/start.bat"));
	CHECK(!AllowedExtension("script.ps1"));
	CHECK(!AllowedExtension("run.js"));
	CHECK(!AllowedExtension("x.lnk"));
}

static void Classification()
{
	CHECK(IsGameplayPath("data/const.ini"));
	CHECK(IsGameplayPath("civs/dacia.json"));
	CHECK(IsGameplayPath("specials/x.json"));
	CHECK(IsGameplayPath("events/e.xml"));
	CHECK(IsGameplayPath("rules/game.json"));
	CHECK(IsGameplayPath("modes/2 liga.json"));
	CHECK(IsGameplayPath("options/o.json"));
	CHECK(IsGameplayPath("randommap/a.ini"));
	CHECK(IsGameplayPath("randommapsettlements/a.ini"));
	CHECK(IsGameplayPath("packs/x.bfhp"));
	CHECK(!IsGameplayPath("packs/x.txt"));
	CHECK(IsGameplayPath("units/rhastatus.xml"));
	CHECK(!IsGameplayPath("units/readme.txt"));
	CHECK(IsGameplayPath("buildings/barracks.xml"));
	CHECK(IsGameplayPath("mapobjects/tree.xml"));
	CHECK(IsGameplayPath("visuals/x.xml"));
	CHECK(!IsGameplayPath("visuals/x.tga"));
	CHECK(IsGameplayPath("units/a.pass"));
	CHECK(IsGameplayPath("buildings/a.pass"));
	CHECK(!IsGameplayPath("visuals/a.pass"));
	CHECK(!IsGameplayPath("sound/music/theme.ogg"));
	CHECK(!IsGameplayPath("mod.json"));
	CHECK(IsContentPath("scenarios/a.bfhp"));
	CHECK(IsContentPath("adventures/a/b.bfhp"));
	CHECK(IsContentPath("conquests/a"));
	CHECK(IsContentPath("conquestmaps/a"));
	CHECK(!IsContentPath("data/const.ini"));
}

static void Fingerprint()
{
	const std::string empty = RulesFingerprint({});
	CHECK_EQ(empty.size(), size_t(64));
	CHECK_EQ(empty, RulesFingerprint({}));
	const std::string one = RulesFingerprint({{"mod-a", "aaaa"}});
	CHECK(one != empty);
	CHECK_EQ(one, RulesFingerprint({{"mod-a", "aaaa"}}));
	CHECK(one != RulesFingerprint({{"mod-a", "aaab"}}));
	CHECK(one != RulesFingerprint({{"mod-b", "aaaa"}}));
	const std::string two = RulesFingerprint({{"mod-a", "aaaa"}, {"mod-b", "bbbb"}});
	CHECK_EQ(two, RulesFingerprint({{"mod-b", "bbbb"}, {"mod-a", "aaaa"}}));
	const std::string three = RulesFingerprint({{"mod-a", "aaaa"}, {"mod-b", "bbbb"}, {"mod-c", "cccc"}});
	CHECK_EQ(three, RulesFingerprint({{"mod-c", "cccc"}, {"mod-a", "aaaa"}, {"mod-b", "bbbb"}}));
	CHECK(three != two);
	CHECK_EQ(RulesFingerprint({{"mod-a", "aaaa"}, {"mod-b", ""}}), "");
	CHECK_EQ(RulesFingerprint({{"mod-a", ""}}), "");
	CHECK_EQ(empty, HashString(std::string(kGameId) + "|sim:3|esq:2|"));
}

int main()
{
	Sha();
	Files();
	Helpers();
	Extensions();
	Classification();
	Fingerprint();
	return test::Summary("modrules");
}
