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

void EachKindOfFileIsAcceptedOnItsOwn()
{
	// Phase B: one kind at a time, each with the number the log prints.
	CHECK_EQ(FullSwapKindOf("patches/data/commands/RBarracks.xml"), static_cast<unsigned>(kFullPatch));
	CHECK_EQ(FullSwapKindOf("patches\\data\\const.ini"), static_cast<unsigned>(kFullPatch));
	CHECK_EQ(FullSwapKindOf("data/GameScripts/6 Dominio.vs"), static_cast<unsigned>(kFullLoose));
	CHECK_EQ(FullSwapKindOf("data/itemscripts/Sword.vs"), static_cast<unsigned>(kFullLoose));
	CHECK_EQ(FullSwapKindOf("data/classes/RHastatus.sc.xml"), static_cast<unsigned>(kFullLoose));
	CHECK_EQ(FullSwapKindOf("modes/arena.json"), static_cast<unsigned>(kFullModes));
	CHECK_EQ(FullSwapKindOf("hidden.txt"), static_cast<unsigned>(kFullHidden));
	CHECK_EQ(FullSwapKindOf("Hidden.TXT"), static_cast<unsigned>(kFullHidden));
	CHECK_EQ(FullSwapKindOf("gameres/x.bmp"), static_cast<unsigned>(kFullOther));
	CHECK_EQ(FullSwapKinds({"patches/data/const.ini", "data/GameScripts/a.vs", "hidden.txt"}), static_cast<unsigned>(kFullPatch | kFullLoose | kFullHidden));
	CHECK_EQ(FullSwapKinds({}), static_cast<unsigned>(kFullNone));
	CHECK(FullSwapKinds({"modes/a.json", "gameres/x.bmp"}) & kFullOther);
	CHECK_EQ(FullSwapKindsText(kFullPatch | kFullHidden), "patches of data/, hidden.txt");
	CHECK_EQ(FullSwapKindsText(kFullNone), "nothing");
	// A layer with all the accepted kinds is switched in place; with one more kind it is not.
	const Files all = {"patches/data/const.ini", "data/GameScripts/9 Arena.vs", "modes/arena.json", "hidden.txt"};
	CHECK(SwapDecide(false, true, kNone, all) == ConstSwap::kFull);
	CHECK(SwapDecide(false, true, all, kConst) == ConstSwap::kFull);
	CHECK(SwapDecide(false, false, kNone, all) == ConstSwap::kRestart);
	CHECK(SwapDecide(false, true, kNone, {"data/GameScripts/9 Arena.vs", "gameres/x.bmp"}) == ConstSwap::kRestart);
}

void ASavedGameSwitchesInPlaceOnlyWhenEverythingAgrees()
{
	CHECK(SaveSwitchesInPlace(true, true, true, false, true));
	// Each condition on its own makes it the restart it was before.
	CHECK(!SaveSwitchesInPlace(false, true, true, false, true)); // a campaign, a conquest or a replay
	CHECK(!SaveSwitchesInPlace(true, false, true, false, true)); // nothing to activate
	CHECK(!SaveSwitchesInPlace(true, true, false, false, true)); // a package has to be enabled
	CHECK(!SaveSwitchesInPlace(true, true, true, true, true));   // the restart would switch other packs off
	CHECK(!SaveSwitchesInPlace(true, true, true, false, false)); // flag off, or the layer carries paks, art, sounds...
}

void FullSwapNeedsOnlyPatchesOfData()
{
	const Files commands = {"patches/data/commands/rbarracks.xml"};
	const Files both = {"patches/data/const.ini", "patches/data/commands/RBarracks.xml"};
	// The example pack of the SDK: two rulesets that differ in a command and in CONST.INI.
	CHECK(SwapDecide(false, true, both, both) == ConstSwap::kFull);
	CHECK(SwapDecide(false, true, kConst, both) == ConstSwap::kFull);
	CHECK(SwapDecide(false, true, kNone, commands) == ConstSwap::kFull);
	CHECK(SwapDecide(false, true, {"patches/data/classes/rhastatus.sc.xml", "patches/data/ai/main.vs"}, commands) == ConstSwap::kFull);
	// Without the flag it is the restart, as before.
	CHECK(SwapDecide(false, false, both, both) == ConstSwap::kRestart);
	CHECK(SwapDecide(false, false, kConst, both) == ConstSwap::kRestart);
	// The const-only path is kept when it applies, with the flag or not.
	CHECK(SwapDecide(false, true, kConst, kConst) == ConstSwap::kReload);
	CHECK(SwapDecide(false, false, kConst, kNone) == ConstSwap::kReload);
	CHECK(SwapDecide(true, true, both, commands) == ConstSwap::kNothing);
	// Anything else restarts, whichever side has it.
	const Files bad[] = {{"patches/data/const.ini", "rle.mmp.tail"}, {"civs/gaul.json"}, {"specials/x.json"}, {"options/speed.json"},
		{"events/ongamestart.vs"}, {"packs/units.pak"}, {"patches/units/rhastatus.ent.xml"}, {"patches/currentlang/game.loc.xml"},
		{"patches/data/readme.txt"}, {"patches/data/"}, {"data/"}, {"data/sounds/x.wav"}, {"data/readme.txt"}, {"local/spanish/game.pak"},
		{"rules/game.json"}, {"modes/sub/x.json"}, {"modes/x.xml"}, {"gameres/icons/x.bmp"}, {"hidden.txt.bak"}};
	for (const Files &b : bad) {
		CHECK(SwapDecide(false, true, both, b) == ConstSwap::kRestart);
		CHECK(SwapDecide(false, true, b, both) == ConstSwap::kRestart);
		CHECK(!FullSwapLayerOk(b));
	}
	CHECK(FullSwapLayerOk({}));
	CHECK(FullSwapLayerOk({"Patches\\Data\\Commands\\RBarracks.XML"}));
}

}

int main()
{
	TheSameRulesetNeedsNothing();
	RulesetsThatDifferOnlyInConstIniReload();
	AnythingElseRestarts();
	OnlyThePatchOfConstIniCounts();
	TheTextNeedsAGamePlaySection();
	FullSwapNeedsOnlyPatchesOfData();
	ASavedGameSwitchesInPlaceOnlyWhenEverythingAgrees();
	EachKindOfFileIsAcceptedOnItsOwn();
	return test::Summary("constswap");
}
