// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/rulesets.h"

using namespace mods_impl;

static void Ids()
{
	CHECK(RulesetIdValid("cm"));
	CHECK(RulesetIdValid("mci-2_x"));
	CHECK(RulesetIdValid("a23456789012345678901234"));
	CHECK(!RulesetIdValid("a234567890123456789012345"));
	CHECK(!RulesetIdValid(""));
	CHECK(!RulesetIdValid("CM"));
	CHECK(!RulesetIdValid("c m"));
	CHECK(!RulesetIdValid("c/m"));
}

static void Resolve()
{
	std::vector<RulesetDecl> d = {{"cm", ""}, {"mci", "Rulesets\\MCI\\"}};
	CHECK(RulesetResolve(d).empty());
	CHECK_EQ(d.size(), size_t(2));
	CHECK_EQ(d[0].path, "rulesets/cm");
	CHECK_EQ(d[1].path, "rulesets/mci");
}

static void ResolveDropsWhatCannotBeUsed()
{
	std::vector<RulesetDecl> d = {{"cm", ""}, {"Bad", ""}, {"cm", "rulesets/other"}, {"up", "../x"}, {"root", "civs"}, {"top", "rulesets"},
		{"same", "rulesets/cm"}, {"inner", "rulesets/cm/sub"}, {"outer", "rulesets"}, {"fine", "rulesets/fine"}};
	const std::vector<RulesetIssue> issues = RulesetResolve(d);
	CHECK_EQ(d.size(), size_t(2));
	CHECK_EQ(d[0].id, "cm");
	CHECK_EQ(d[1].id, "fine");
	CHECK_EQ(issues.size(), size_t(8));
	CHECK(issues[0].problem == RulesetProblem::kBadId);
	CHECK(issues[1].problem == RulesetProblem::kDuplicate);
	CHECK(issues[2].problem == RulesetProblem::kBadPath);
	CHECK(issues[3].problem == RulesetProblem::kBadPath);
	CHECK(issues[4].problem == RulesetProblem::kBadPath);
	CHECK(issues[5].problem == RulesetProblem::kOverlap);
	CHECK_EQ(issues[5].other, "cm");
	CHECK(issues[6].problem == RulesetProblem::kOverlap);
	CHECK_EQ(issues[6].id, "inner");
	CHECK(issues[7].problem == RulesetProblem::kBadPath);
}

static void ANestedLayerIsDroppedWhicheverComesFirst()
{
	std::vector<RulesetDecl> d = {{"inner", "rulesets/a/b"}, {"outer", "rulesets/a"}};
	const std::vector<RulesetIssue> issues = RulesetResolve(d);
	CHECK_EQ(d.size(), size_t(1));
	CHECK_EQ(d[0].id, "inner");
	CHECK_EQ(issues.size(), size_t(1));
	CHECK_EQ(issues[0].other, "inner");
	std::vector<RulesetDecl> siblings = {{"a", "rulesets/ab"}, {"b", "rulesets/a"}};
	CHECK(RulesetResolve(siblings).empty());
}

static void Owner()
{
	std::vector<RulesetDecl> d = {{"cm", ""}, {"mci", ""}};
	RulesetResolve(d);
	CHECK_EQ(RulesetOwnerOf(d, "rulesets/cm/data/const.ini"), 0);
	CHECK_EQ(RulesetOwnerOf(d, "rulesets/mci/patches/data/const.ini"), 1);
	CHECK_EQ(RulesetOwnerOf(d, "rulesets/cmx/data/const.ini"), -1);
	CHECK_EQ(RulesetOwnerOf(d, "rulesets/cm"), -1);
	CHECK_EQ(RulesetOwnerOf(d, "civs/dacia.json"), -1);
	CHECK_EQ(RulesetOwnerOf({}, "rulesets/cm/a"), -1);
}

static void Entries()
{
	// 0: rules only (layers, no civs); 1: civs and layers; 2: civs only; 3: civs only but forbidden; 4: nothing to offer.
	std::vector<RulesetPackFacts> packs(5);
	packs[0].layers = 2;
	packs[1].layers = 1;
	packs[1].hasCivs = true;
	packs[2].hasCivs = true;
	packs[3].hasCivs = true;
	packs[3].usable = false;
	const std::vector<RulesetEntry> e = RulesetEntries(packs);
	CHECK_EQ(e.size(), size_t(4));
	CHECK(e[0].pack == 0 && e[0].layer == 0);
	CHECK(e[1].pack == 0 && e[1].layer == 1);
	// A package with civs and a layer keeps its layer entry only; the civs-only package is offered alone.
	CHECK(e[2].pack == 1 && e[2].layer == 0);
	CHECK(e[3].pack == 2 && e[3].layer == -1);
	CHECK(RulesetEntries({}).empty());
}

static void CivPaths()
{
	CHECK(RulesetPathsHaveCivs({"data/const.ini", "civs/dacia.json"}));
	CHECK(!RulesetPathsHaveCivs({"data/const.ini", "civs/readme.txt", "civs/sub/dacia.json", "civs/.json", "rulesets/cm/civs/dacia.json"}));
	CHECK(!RulesetPathsHaveCivs({}));
}

static void MountedDirs()
{
	const std::string mods = "C:\\Game\\mods";
	CHECK_EQ(PackFolderOfDir(mods, "C:\\Game\\mods\\Dacia"), "Dacia");
	CHECK_EQ(PackFolderOfDir(mods, "C:\\Game\\mods\\Dacia\\rulesets\\cm"), "Dacia");
	CHECK_EQ(PackFolderOfDir(mods, "c:\\game\\MODS\\Dacia\\rulesets\\cm"), "Dacia");
	CHECK_EQ(ModDirVfsName(mods, "C:\\Game\\mods\\Dacia"), "Dacia");
	CHECK_EQ(ModDirVfsName(mods, "C:\\Game\\mods\\Dacia\\rulesets\\cm"), "Dacia/rulesets/cm");
	// A folder outside mods\ falls back to its last component, as the code did before rulesets.
	CHECK_EQ(PackFolderOfDir(mods, "D:\\Other\\Pack"), "Pack");
	CHECK_EQ(ModDirVfsName(mods, "Pack"), "Pack");
}

int main()
{
	Ids();
	Resolve();
	ResolveDropsWhatCannotBeUsed();
	ANestedLayerIsDroppedWhicheverComesFirst();
	Owner();
	Entries();
	CivPaths();
	MountedDirs();
	return test::Summary("rulesets");
}
