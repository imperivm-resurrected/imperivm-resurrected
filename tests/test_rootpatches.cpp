// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
// The real shape of the root patches of two rules packages (tests/test_patchxml.cpp checks the patchers one operation at a time): two packages, each with a
// patches\data\commands\RBarracks.xml that mixes an add and a change, in a folder tree on disk, found by PatchWalk, applied to the game's own file in one
// PatchApplyAll as the game does when it opens it. The first run of this shape in the game applied the changes under the game's own rules because the walk
// did not hand the decision to the files below the first folder; PatchWalk(dir, addOnly) has to give it to every one.
#include "check.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mods/patchwalk.h"
#include "mods/patchxml.h"

namespace fs = std::filesystem;

static const char *kGame =
	"<commands>\r\n"
	"<cmd name=\"trainRHastatus\" costgold = \"100\" method=\"train\"/>\r\n"
	"<cmd name=\"trainRArcher\" costgold=\"80\" method=\"train\"/>\r\n"
	"<cmd name=\"trainRScout\" costgold=\"140\" method=\"train\"/>\r\n"
	"</commands>\r\n";

static const char *kPackC =
	"<patch>\n  <set select=\"/commands/cmd[@name='trainRArcher']\" costgold=\"55\"/>\n"
	"  <add select=\"/commands\"><cmd name=\"trainClashC\" costgold=\"100\" key=\"j\"/></add>\n</patch>\n";
static const char *kPackD =
	"<patch>\n  <set select=\"/commands/cmd[@name='trainRScout']\" costgold=\"99\"/>\n"
	"  <add select=\"/commands\"><cmd name=\"trainClashD\" costgold=\"100\" key=\"k\"/></add>\n</patch>\n";

static void Write(const fs::path &file, const std::string &text)
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << text;
}

static std::string Read(const std::string &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static bool Has(const std::string &data, const std::string &part)
{
	if (data.find(part) != std::string::npos) return true;
	std::printf("missing %s in:\n%s\n", test::Show(part).c_str(), data.c_str());
	return false;
}

static bool Lacks(const std::string &data, const std::string &part)
{
	if (data.find(part) == std::string::npos) return true;
	std::printf("unexpected %s in:\n%s\n", test::Show(part).c_str(), data.c_str());
	return false;
}

// The patch of the game file `rel` that PatchWalk finds in the root of the package at `patches`.
static PatchInput Found(const fs::path &patches, const std::string &rel, bool packAddOnly)
{
	for (const PatchFileFound &f : PatchWalk(patches.string(), packAddOnly)) {
		if (f.rel == rel) return {Read(f.path), f.addOnly};
	}
	return {"", false};
}

static std::string Apply(const fs::path &c, const fs::path &d, bool cAddOnly, bool dAddOnly, std::vector<PatchOutcome> &outcomes)
{
	std::string data = kGame;
	const std::string rel = "data/commands/rbarracks.xml";
	PatchApplyAll(rel, data, {Found(c, rel, cAddOnly), Found(d, rel, dAddOnly)}, outcomes);
	return data;
}

static void WalkGivesTheDecisionToEveryFileBelowTheFolder()
{
	const fs::path dir = fs::temp_directory_path() / "imperivm-rootpatches-walk";
	fs::remove_all(dir);
	Write(dir / "top.xml", "x");
	Write(dir / "data" / "commands" / "RBarracks.xml", "x");
	Write(dir / "data" / "ai" / "deep" / "er.vs", "x");
	Write(dir / "menuini" / "MAINMENU.INI", "x");
	Write(dir / "DATA" / "INTERFACE" / "MENU" / "TEMPLATE.INI", "x");
	const std::vector<PatchFileFound> found = PatchWalk(dir.string(), true);
	CHECK_EQ(found.size(), size_t(5));
	for (const PatchFileFound &f : found) {
		const bool interface = f.rel == "menuini/mainmenu.ini" || f.rel == "data/interface/menu/template.ini";
		if (f.addOnly == interface) std::printf("%s: addOnly %d\n", f.rel.c_str(), f.addOnly);
		CHECK(f.addOnly != interface);
	}
	// the names are the game's: lowercase, "/", relative to the folder
	bool deep = false;
	for (const PatchFileFound &f : found) deep = deep || f.rel == "data/ai/deep/er.vs";
	CHECK(deep);
	// without the decision nothing is add-only; a folder that does not exist has no files
	for (const PatchFileFound &f : PatchWalk(dir.string(), false)) CHECK(!f.addOnly);
	CHECK(PatchWalk((dir / "nowhere").string(), true).empty());
	fs::remove_all(dir);
}

static void TwoRootsWithAMixedPatchOfTheSameFile()
{
	const fs::path base = fs::temp_directory_path() / "imperivm-rootpatches-mixed";
	fs::remove_all(base);
	Write(base / "clash-c" / "patches" / "data" / "commands" / "RBarracks.xml", kPackC);
	Write(base / "clash-d" / "patches" / "data" / "commands" / "RBarracks.xml", kPackD);
	const fs::path c = base / "clash-c" / "patches", d = base / "clash-d" / "patches";
	std::vector<PatchOutcome> o;
	// the game's own rules: both changes are skipped, both adds apply, each patch is applied in part and says one skipped operation
	std::string data = Apply(c, d, true, true, o);
	CHECK(Has(data, "name=\"trainRArcher\" costgold=\"80\"") && Has(data, "name=\"trainRScout\" costgold=\"140\""));
	CHECK(Has(data, "name=\"trainClashC\"") && Has(data, "name=\"trainClashD\""));
	CHECK(Lacks(data, "costgold=\"55\"") && Lacks(data, "costgold=\"99\""));
	CHECK_EQ(o.size(), size_t(2));
	CHECK(o[0].applied && o[1].applied);
	CHECK_EQ(o[0].skipped, 1u);
	CHECK_EQ(o[1].skipped, 1u);
	CHECK(o[0].errors.empty() && o[1].errors.empty());
	// clash-c's own ruleset: its change applies, clash-d's does not
	data = Apply(c, d, false, true, o);
	CHECK(Has(data, "costgold=\"55\"") && Has(data, "name=\"trainRScout\" costgold=\"140\"") && Lacks(data, "costgold=\"99\""));
	CHECK(Has(data, "trainClashC") && Has(data, "trainClashD"));
	CHECK_EQ(o[0].skipped, 0u);
	CHECK_EQ(o[1].skipped, 1u);
	// clash-d's own ruleset
	data = Apply(c, d, true, false, o);
	CHECK(Has(data, "name=\"trainRArcher\" costgold=\"80\"") && Has(data, "costgold=\"99\"") && Lacks(data, "costgold=\"55\""));
	CHECK_EQ(o[0].skipped, 1u);
	CHECK_EQ(o[1].skipped, 0u);
	// both own (never mounted together by the game, but the patcher does what it is told)
	data = Apply(c, d, false, false, o);
	CHECK(Has(data, "costgold=\"55\"") && Has(data, "costgold=\"99\""));
	fs::remove_all(base);
}

static void AnIniOfTheShapeOfCgwar()
{
	const fs::path base = fs::temp_directory_path() / "imperivm-rootpatches-ini";
	fs::remove_all(base);
	Write(base / "cgwar" / "patches" / "DATA" / "AI" / "AI.INI", "[Vars.All]\n-AIV_ExcessGold\nAIV_ExcessGold = 7000\nAIV_Mine = 1\n");
	const std::string rel = "data/ai/ai.ini";
	const std::string game = "[Vars.All]\r\nAIV_ExcessGold = 10000\r\n";
	std::vector<PatchOutcome> o;
	std::string data = game;
	CHECK(PatchApplyAll(rel, data, {Found(base / "cgwar" / "patches", rel, true)}, o));
	CHECK(Has(data, "AIV_ExcessGold = 10000") && Has(data, "AIV_Mine = 1") && Lacks(data, "7000"));
	CHECK_EQ(o[0].skipped, 1u);
	data = game;
	CHECK(PatchApplyAll(rel, data, {Found(base / "cgwar" / "patches", rel, false)}, o));
	CHECK(Has(data, "AIV_ExcessGold = 7000") && Lacks(data, "10000"));
	CHECK_EQ(o[0].skipped, 0u);
	fs::remove_all(base);
}

int main()
{
	WalkGivesTheDecisionToEveryFileBelowTheFolder();
	TwoRootsWithAMixedPatchOfTheSameFile();
	AnIniOfTheShapeOfCgwar();
	return test::Summary("rootpatches");
}
