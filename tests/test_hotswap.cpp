// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>
#include <vector>

#include "addr/net.h"
#include "addr/world.h"
#include "mods/hotswap.h"
#include "mods/savelib.h"

namespace {

using Files = std::vector<std::string>;
const Files kConst = {"patches/data/const.ini"};
const Files kNone;

void TheSameRulesetNeedsNothing()
{
	CHECK(SwapDecide(true, kConst, kConst) == HotSwap::kNothing);
	CHECK(SwapDecide(true, kNone, kNone) == HotSwap::kNothing);
	// Even a big ruleset: nothing changes, so nothing is asked.
	CHECK(SwapDecide(true, {"patches/data/commands/rbarracks.xml"}, {"patches/data/commands/rbarracks.xml"}) == HotSwap::kNothing);
	CHECK(SwapDecide(true, {"gameres/x.bmp"}, {"patches/data/commands/rbarracks.xml"}) == HotSwap::kNothing);
}

void ConstIniAloneIsJustOneKindOfFile()
{
	// What the first version switched by itself (two rulesets that only differ in data/const.ini) is the same mechanism now.
	CHECK(SwapDecide(false, kConst, kConst) == HotSwap::kReload);
	CHECK(SwapDecide(false, kNone, kConst) == HotSwap::kReload);
	CHECK(SwapDecide(false, kConst, kNone) == HotSwap::kReload);
	CHECK(SwapDecide(false, kNone, kNone) == HotSwap::kReload);
	CHECK(SwapDecide(false, {"Patches\\Data\\CONST.INI"}, {"patches/data/const.ini"}) == HotSwap::kReload);
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
	CHECK_EQ(HotSwapKindOf("patches/data/commands/RBarracks.xml"), static_cast<unsigned>(kHotPatch));
	CHECK_EQ(HotSwapKindOf("patches\\data\\const.ini"), static_cast<unsigned>(kHotPatch));
	CHECK_EQ(HotSwapKindOf("data/GameScripts/6 Dominio.vs"), static_cast<unsigned>(kHotLoose));
	CHECK_EQ(HotSwapKindOf("data/itemscripts/Sword.vs"), static_cast<unsigned>(kHotLoose));
	CHECK_EQ(HotSwapKindOf("data/classes/RHastatus.sc.xml"), static_cast<unsigned>(kHotLoose));
	CHECK_EQ(HotSwapKindOf("modes/arena.json"), static_cast<unsigned>(kHotModes));
	CHECK_EQ(HotSwapKindOf("hidden.txt"), static_cast<unsigned>(kHotHidden));
	CHECK_EQ(HotSwapKindOf("Hidden.TXT"), static_cast<unsigned>(kHotHidden));
	CHECK_EQ(HotSwapKindOf("gameres/x.bmp"), static_cast<unsigned>(kHotOther));
	CHECK_EQ(HotSwapKinds({"patches/data/const.ini", "data/GameScripts/a.vs", "hidden.txt"}), static_cast<unsigned>(kHotPatch | kHotLoose | kHotHidden));
	CHECK_EQ(HotSwapKinds({}), static_cast<unsigned>(kHotNone));
	CHECK(HotSwapKinds({"modes/a.json", "gameres/x.bmp"}) & kHotOther);
	CHECK_EQ(HotSwapKindsText(kHotPatch | kHotHidden), "patches of data/, hidden.txt");
	CHECK_EQ(HotSwapKindsText(kHotNone), "nothing");
	// A layer with all the accepted kinds is switched in place; with one more kind it is not.
	const Files all = {"patches/data/const.ini", "data/GameScripts/9 Arena.vs", "modes/arena.json", "hidden.txt"};
	CHECK(SwapDecide(false, kNone, all) == HotSwap::kReload);
	CHECK(SwapDecide(false, all, kConst) == HotSwap::kReload);
	CHECK(SwapDecide(false, kNone, {"data/GameScripts/9 Arena.vs", "gameres/x.bmp"}) == HotSwap::kRestart);
}

void ASavedGameSwitchesInPlaceOnlyWhenEverythingAgrees()
{
	CHECK(SaveSwitchesInPlace(true, true, true, false, true));
	// Each condition on its own makes it the restart it was before.
	CHECK(!SaveSwitchesInPlace(false, true, true, false, true)); // a campaign, a conquest or a replay
	CHECK(!SaveSwitchesInPlace(true, false, true, false, true)); // nothing to activate
	CHECK(!SaveSwitchesInPlace(true, true, false, false, true)); // a package has to be enabled
	CHECK(!SaveSwitchesInPlace(true, true, true, true, true));   // the restart would switch other packs off
	CHECK(!SaveSwitchesInPlace(true, true, true, false, false)); // the layer carries paks, art, sounds...
}

void OnlyTheKindsOfFileTheGameCanReadAgainAreSwitched()
{
	const Files commands = {"patches/data/commands/rbarracks.xml"};
	const Files both = {"patches/data/const.ini", "patches/data/commands/RBarracks.xml"};
	// The example pack of the SDK: two rulesets that differ in a command and in CONST.INI.
	CHECK(SwapDecide(false, both, both) == HotSwap::kReload);
	CHECK(SwapDecide(false, kConst, both) == HotSwap::kReload);
	CHECK(SwapDecide(false, kNone, commands) == HotSwap::kReload);
	CHECK(SwapDecide(false, {"patches/data/classes/rhastatus.sc.xml", "patches/data/ai/main.vs"}, commands) == HotSwap::kReload);
	CHECK(SwapDecide(true, both, commands) == HotSwap::kNothing);
	// Anything else restarts, whichever side has it.
	const Files bad[] = {{"patches/data/const.ini", "rle.mmp.tail"}, {"civs/gaul.json"}, {"specials/x.json"}, {"options/speed.json"},
		{"events/ongamestart.vs"}, {"packs/units.pak"}, {"patches/units/rhastatus.ent.xml"}, {"patches/currentlang/game.loc.xml"},
		{"patches/data/readme.txt"}, {"patches/data/"}, {"data/"}, {"data/sounds/x.wav"}, {"data/readme.txt"}, {"local/spanish/game.pak"},
		{"rules/game.json"}, {"modes/sub/x.json"}, {"modes/x.xml"}, {"gameres/icons/x.bmp"}, {"hidden.txt.bak"}};
	for (const Files &b : bad) {
		CHECK(SwapDecide(false, both, b) == HotSwap::kRestart);
		CHECK(SwapDecide(false, b, both) == HotSwap::kRestart);
		CHECK(!HotSwapLayerOk(b));
	}
	CHECK(HotSwapLayerOk({}));
	CHECK(HotSwapLayerOk({"Patches\\Data\\Commands\\RBarracks.XML"}));
}

void EveryKindOfGameThatTheEngineMakesIsKnown()
{
	CHECK(GameKindOfVtable(addr::kGameSingleVtable) == GameKind::kSkirmish);
	CHECK(GameKindOfVtable(addr::kMultiplayerVtable) == GameKind::kMultiplayer);
	CHECK(GameKindOfVtable(addr::kGameAdvVtable) == GameKind::kAdventure);
	CHECK(GameKindOfVtable(addr::kGameConqVtable) == GameKind::kConquest);
	CHECK(GameKindOfVtable(addr::kGameEditVtable) == GameKind::kEditor);
	CHECK(GameKindOfVtable(0) == GameKind::kUnknown);
	CHECK(GameKindOfVtable(addr::kGameSingleVtable + 4) == GameKind::kUnknown);
	CHECK(GameKindOfVtable(addr::kShipVtable) == GameKind::kUnknown);
	// The five vtables are five different addresses, and each has its own name for the log.
	const uintptr_t all[] = {addr::kGameSingleVtable, addr::kMultiplayerVtable, addr::kGameAdvVtable, addr::kGameConqVtable, addr::kGameEditVtable};
	for (size_t i = 0; i < 5; i++) {
		for (size_t j = i + 1; j < 5; j++) CHECK(all[i] != all[j]);
		CHECK(std::string(GameKindName(GameKindOfVtable(all[i]))) != "unknown");
	}
	CHECK_EQ(std::string(GameKindName(GameKind::kAdventure)), "adventure");
	CHECK_EQ(std::string(GameKindName(GameKind::kUnknown)), "unknown");
}

void SavesOfEveryKindOfGameButReplaysSwitchInPlace()
{
	CHECK(SaveKindSwitchesInPlace(kSaveStrategy));
	CHECK(SaveKindSwitchesInPlace(kSaveAdventure));
	CHECK(SaveKindSwitchesInPlace(kSaveConquest));
	CHECK(!SaveKindSwitchesInPlace(kSaveReplay));
	CHECK(!SaveKindSwitchesInPlace(-1));
	// An adventure save under another ruleset of an enabled package is the same case as a skirmish save.
	CHECK(SaveSwitchesInPlace(SaveKindSwitchesInPlace(kSaveAdventure), true, true, false, true));
	CHECK(!SaveSwitchesInPlace(SaveKindSwitchesInPlace(kSaveReplay), true, true, false, true));
}

}

int main()
{
	TheSameRulesetNeedsNothing();
	ConstIniAloneIsJustOneKindOfFile();
	TheTextNeedsAGamePlaySection();
	OnlyTheKindsOfFileTheGameCanReadAgainAreSwitched();
	ASavedGameSwitchesInPlaceOnlyWhenEverythingAgrees();
	EachKindOfFileIsAcceptedOnItsOwn();
	EveryKindOfGameThatTheEngineMakesIsKnown();
	SavesOfEveryKindOfGameButReplaysSwitchInPlace();
	return test::Summary("hotswap");
}
