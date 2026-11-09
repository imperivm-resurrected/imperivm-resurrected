// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "mods/constswap.h"

namespace {

using Files = std::vector<std::string>;
const Files kConst = {"patches/data/const.ini"};
const Files kNone;

void TheSameRulesetNeedsNothing()
{
	CHECK(ConstSwapDecide(true, kConst, kConst) == ConstSwap::kNothing);
	CHECK(ConstSwapDecide(true, kNone, kNone) == ConstSwap::kNothing);
	// Even a big ruleset: nothing changes, so nothing is asked.
	CHECK(ConstSwapDecide(true, {"patches/data/commands/rbarracks.xml"}, {"patches/data/commands/rbarracks.xml"}) == ConstSwap::kNothing);
}

void RulesetsThatDifferOnlyInConstIniReload()
{
	CHECK(ConstSwapDecide(false, kConst, kConst) == ConstSwap::kReload);
	CHECK(ConstSwapDecide(false, kNone, kConst) == ConstSwap::kReload);
	CHECK(ConstSwapDecide(false, kConst, kNone) == ConstSwap::kReload);
	CHECK(ConstSwapDecide(false, kNone, kNone) == ConstSwap::kReload);
	CHECK(ConstSwapDecide(false, {"Patches\\Data\\CONST.INI"}, {"patches/data/const.ini"}) == ConstSwap::kReload);
}

void AnythingElseRestarts()
{
	const Files commands = {"patches/data/commands/rbarracks.xml"};
	const Files both = {"patches/data/const.ini", "patches/data/commands/rbarracks.xml"};
	// The example pack of the SDK: a ruleset that also changes a command.
	CHECK(ConstSwapDecide(false, both, both) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, both) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, both, kConst) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kNone, commands) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, commands, kNone) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/classes/rhastatus.sc.xml"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "hidden.txt"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "rle.mmp.tail"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "civs/gaul.json"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "modes/fast.json"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "options/speed.json"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "events/ongamestart.vs"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "packs/units.pak"}) == ConstSwap::kRestart);
	CHECK(ConstSwapDecide(false, kConst, {"patches/data/const.ini", "local/spanish/game.pak"}) == ConstSwap::kRestart);
}

void OnlyThePatchOfConstIniCounts()
{
	// A loose file of the same name replaces the game's file instead of patching it: it is not the supported form.
	CHECK(!ConstSwapLayerOk({"data/const.ini"}));
	CHECK(!ConstSwapLayerOk({"patches/data/const.ini.bak"}));
	CHECK(!ConstSwapLayerOk({"patches/data/const.ini", "data/const.ini"}));
	CHECK(!ConstSwapLayerOk({"patches/const.ini"}));
	CHECK(ConstSwapLayerOk({}));
	CHECK(ConstSwapLayerOk(kConst));
}

void TheTextNeedsAGamePlaySection()
{
	CHECK(ConstIniHasGamePlay("[GamePlay]\r\nDamageAmplify = 200\r\n"));
	CHECK(ConstIniHasGamePlay("; comment\r\n[Other]\r\nA=1\r\n\r\n  [gameplay]  \r\nB=2"));
	CHECK(ConstIniHasGamePlay("\xEF\xBB\xBF[GAMEPLAY]\nA=1\n"));
	CHECK(ConstIniHasGamePlay("[GamePlay]"));
	CHECK(!ConstIniHasGamePlay(""));
	CHECK(!ConstIniHasGamePlay("\r\n\r\n"));
	CHECK(!ConstIniHasGamePlay("; [GamePlay]\r\nA=1"));
	CHECK(!ConstIniHasGamePlay("[GamePlayX]\nA=1"));
	CHECK(!ConstIniHasGamePlay("GamePlay\nA=1"));
	CHECK(!ConstIniHasGamePlay("A=1 [GamePlay]"));
	CHECK(!ConstIniHasGamePlay("<html>not found</html>"));
}

}

int main()
{
	TheSameRulesetNeedsNothing();
	RulesetsThatDifferOnlyInConstIniReload();
	AnythingElseRestarts();
	OnlyThePatchOfConstIniCounts();
	TheTextNeedsAGamePlaySection();
	return test::Summary("constswap");
}
