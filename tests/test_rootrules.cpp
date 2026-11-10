// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "mods/rootrules.h"

using namespace mods_impl;

static GameFileSet Game()
{
	return {"ui/icons/rhastatus.bmp", "ui/menu/button.bmp", "data/classes/rhastatus.sc.xml", "currentlang/tips.xml", "currentlang/voices/rhastatus/1.wav", "units/rhastatus/rhastatus.ent.xml"};
}

static void PathsAreComparedInTheGamesForm()
{
	CHECK_EQ(RootGamePath("gameres\\Icons\\RHastatus.bmp"), "ui/icons/rhastatus.bmp");
	CHECK_EQ(RootGamePath("UI/icons/RHastatus.bmp"), "ui/icons/rhastatus.bmp");
	CHECK_EQ(RootGamePath("/Data/Classes/X.sc.xml"), "data/classes/x.sc.xml");
	CHECK_EQ(RootGamePath("menuini/Menu.ini"), "data/interface/menu/menu.ini");
}

static void TheMenuSkinIsNeverGlobal()
{
	CHECK(RootIsMenuSkin("data/interface/menu/menu.ini"));
	CHECK(RootIsMenuSkin("ui/menu/button.bmp"));
	CHECK(RootIsMenuSkin("currentlang/menubackground.bmp"));
	CHECK(RootIsMenuSkin("currentlang/logo_hd.bmp"));
	CHECK(RootIsMenuSkin("currentlang/tips.xml"));
	CHECK(RootIsMenuSkin("currentlang/help.xml"));
	CHECK(!RootIsMenuSkin("currentlang/voices/rhastatus/1.wav"));
	CHECK(!RootIsMenuSkin("currentlang/game.loc.xml"));
	CHECK(!RootIsMenuSkin("ui/icons/rhastatus.bmp"));
	CHECK(!RootIsMenuSkin("currentlang/sub/tips.xml"));
}

static void OnlyWhatTheGameHasIsGlobal()
{
	const std::vector<std::string> root = {"gameres/icons/RHastatus.bmp", "gameres/icons/MyUnit.bmp", "ui\\icons\\rhastatus.bmp", "data/classes/RHastatus.sc.xml",
		"data/classes/GaulHero.sc.xml", "gameres/menu/button.bmp", "currentlang/tips.xml", "currentlang/voices/RHastatus/1.wav", "units/MyUnit/walk.rle"};
	const std::vector<std::string> global = RootGlobalFiles(root, Game());
	CHECK_EQ(global.size(), size_t(4));
	CHECK_EQ(global[0], "currentlang/voices/rhastatus/1.wav");
	CHECK_EQ(global[1], "data/classes/rhastatus.sc.xml");
	CHECK_EQ(global[2], "gameres/icons/rhastatus.bmp");
	CHECK_EQ(global[3], "ui/icons/rhastatus.bmp");
	CHECK(RootGlobalFiles({"gameres/icons/MyUnit.bmp"}, Game()).empty());
	CHECK(RootGlobalFiles(root, GameFileSet()).empty());
}

static void TheGlobalFilesApplyOnlyUnderTheOwnRuleset()
{
	CHECK(RootGlobalApplies("clash-a", "clash-a"));
	CHECK(RootGlobalApplies("Clash-A", "clash-a"));
	CHECK(!RootGlobalApplies("clash-a", "clash-b"));
	CHECK(!RootGlobalApplies("clash-a", ""));
}

static void TheLayerRefusesTheGlobalFilesUnderBothNames()
{
	const std::unordered_set<std::string> refusals = RootRefusals({"gameres/icons/rhastatus.bmp", "data/classes/rhastatus.sc.xml"});
	CHECK(RootRefuses(refusals, "ui/icons/RHastatus.bmp"));
	CHECK(RootRefuses(refusals, "gameres\\icons\\rhastatus.bmp"));
	CHECK(RootRefuses(refusals, "Data\\Classes\\RHastatus.sc.xml"));
	CHECK(!RootRefuses(refusals, "ui/icons/other.bmp"));
	CHECK(!RootRefuses(refusals, "data/classes/gaulhero.sc.xml"));
	CHECK(!RootRefuses(std::unordered_set<std::string>(), "ui/icons/rhastatus.bmp"));
}

int main()
{
	PathsAreComparedInTheGamesForm();
	TheMenuSkinIsNeverGlobal();
	OnlyWhatTheGameHasIsGlobal();
	TheGlobalFilesApplyOnlyUnderTheOwnRuleset();
	TheLayerRefusesTheGlobalFilesUnderBothNames();
	return test::Summary("rootrules");
}
