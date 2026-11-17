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
		{"events/ongamestart.vs"}, {"packs/units.pak"}, {"patches/units/rhastatus.ent.xml"}, {"patches/currentlang/game.bmp"},
		{"patches/data/readme.txt"}, {"patches/data/"}, {"data/"}, {"data/sounds/x.wav"}, {"data/readme.txt"}, {"local/spanish/game.pak"},
		{"rules/game.json"}, {"modes/sub/x.json"}, {"modes/x.xml"}, {"gameres/selections/x.rle"}, {"hidden.txt.bak"}, {"gameres/icons/x.wav"}, {"gameres/icons/"}};
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

void ASwitchChosenInAGameWaitsOnlyWhereTheNextGameIsLoadedThroughLoadGameData()
{
	CHECK(GameKindCanSwitchWhileRunning(GameKind::kSkirmish));
	CHECK(GameKindCanSwitchWhileRunning(GameKind::kAdventure));
	CHECK(GameKindCanSwitchWhileRunning(GameKind::kConquest));
	// The editor loads its files inside the same game object, a multiplayer game does not load saves, and an unknown game is not known.
	CHECK(!GameKindCanSwitchWhileRunning(GameKind::kEditor));
	CHECK(!GameKindCanSwitchWhileRunning(GameKind::kMultiplayer));
	CHECK(!GameKindCanSwitchWhileRunning(GameKind::kUnknown));
}

void AWaitingSwitchExpires()
{
	CHECK(PendingSwitchStillValid(1000, 1000));
	CHECK(PendingSwitchStillValid(1000, 1000 + kPendingSwitchMs));
	CHECK(!PendingSwitchStillValid(1000, 1000 + kPendingSwitchMs + 1));
	// The tick counter wraps around after 49 days.
	CHECK(PendingSwitchStillValid(0xfffffff0u, 20));
	CHECK(!PendingSwitchStillValid(0xfffffff0u, kPendingSwitchMs + 100));
}

}

void GlobalRootFilesCountAsFilesThatComeWithARuleset()
{
	const std::vector<std::string> layer = {"patches/data/const.ini"};
	const std::vector<std::string> art = {"ui/selections/circle.rle"}, data = {"data/classes/rhastatus.sc.xml"};
	// Another package's ruleset: the global root files of this one come or go with it.
	CHECK(!HotSwapLayerOk(SwapFilesOf(layer, art, false)));
	CHECK(HotSwapLayerOk(SwapFilesOf(layer, data, false)));
	CHECK(HotSwapLayerOk(SwapFilesOf(layer, {}, false)));
	// The same package: they are mounted before and after, nothing changes with them.
	CHECK(HotSwapLayerOk(SwapFilesOf(layer, art, true)));
	CHECK_EQ(SwapFilesOf(layer, art, true).size(), size_t(1));
	CHECK_EQ(SwapFilesOf(layer, art, false).size(), size_t(2));
	// The patches of a root that change the game's files come with the ruleset too: a data patch is hot-switchable, one of anything else is not.
	const std::vector<std::string> dataPatch = {"patches/data/commands/rbarracks.xml", "patches/data/const.ini"}, otherPatch = {"patches/units/rhastatus.ent.xml"};
	CHECK(HotSwapLayerOk(SwapFilesOf(layer, dataPatch, false)));
	CHECK(!HotSwapLayerOk(SwapFilesOf(layer, otherPatch, false)));
	CHECK(HotSwapLayerOk(SwapFilesOf(layer, otherPatch, true)));
	CHECK(SwapDecide(false, SwapFilesOf({}, {}, false), SwapFilesOf({}, dataPatch, false)) == HotSwap::kReload);
	// A package entry mounts no folder: only its global files change.
	CHECK(SwapDecide(false, SwapFilesOf({}, {}, false), SwapFilesOf({}, data, false)) == HotSwap::kReload);
	CHECK(SwapDecide(false, SwapFilesOf({}, {}, false), SwapFilesOf({}, art, false)) == HotSwap::kRestart);
	// The images of a match in the root do not keep the restart.
	CHECK(SwapDecide(false, SwapFilesOf({}, {}, false), SwapFilesOf({}, {"ui/icons/rhastatus.bmp", "ui/cmdbar/actions/train rhastatus.bmp"}, false)) == HotSwap::kReload);
}

void TheImagesOfAMatchAreSwitchedInPlaceAndTheOnesReadOnceAreNot()
{
	// The images of a match: icons, command buttons, info bar, notification icons, help, conversations, in either name of the volume.
	const char *const art[] = {"gameres/icons/RHastatus.bmp", "ui/icons/rhastatus.bmp", "gameres/cmdbar/Actions/train RHastatus.bmp", "gameres/cmdbar/corners/a1.rle",
		"gameres/infobar/common/gold_cost.bmp", "GAMERES\\Infobar\\Rome\\bar.bmp", "gameres/noteicons/a.bmp", "gameres/help/x.bmp", "gameres/conversations/p.bmp",
		"minimap/zoom16/entities/mapobjects-druins1-druins1.bmp", "minimap/zoom8/entities/x.bmp", "ui/icons/sub/deep.bmp"};
	for (const char *f : art) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotArt));
	// The ones that are read once, or are not images of a match, keep the restart and say why.
	const char *const once[] = {"gameres/selections/a.rle", "ui/cursors/arrow.cur", "ui/cursors/a.bmp", "gameres/menu/button.bmp", "ui/menu/profileicons/a.bmp",
		"ui/interface/currentlang/x.bmp", "gameres/common/check.bmp", "ui/editor/a.bmp", "gameres/frame.bmp", "fonts/tahoma13.apf", "data/cursors.xml",
		"data/interface/menu/mainmenu.ini", "gameini/cmdbar/frames.ini", "menuini/mainmenu.ini", "patches/data/interface/menu/template.ini", "patches/data/cursors.xml"};
	for (const char *f : once) {
		CHECK(HotSwapReadOnce(f));
		CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	}
	// Not read once, not covered yet.
	for (const char *f : {"packs/a.pak", "sprites/a.rle", "gameres/icons/x.db", "gameres/icons/x.png", "minimap/zoom16/terrain/a.bmp",
			 "minimap/zoom16/entities/sub/a.bmp", "minimap/zoomx/entities/a.bmp", "minimap/zoom16/entities/", "units/rhastatus.pass"}) {
		CHECK(!HotSwapReadOnce(f));
		CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	}
	// A layer of images alone, or with data, is reloaded; with a file read once it is not.
	const Files images = {"gameres/icons/RHastatus.bmp", "gameres/cmdbar/Actions/train RHastatus.bmp", "minimap/zoom8/entities/x.bmp"};
	CHECK(HotSwapLayerOk(images));
	CHECK(SwapDecide(false, images, {"patches/data/const.ini", "gameres/icons/RHastatus.bmp"}) == HotSwap::kReload);
	CHECK(SwapDecide(false, images, {"gameres/icons/RHastatus.bmp", "gameres/selections/a.rle"}) == HotSwap::kRestart);
	CHECK_EQ(HotSwapKindsText(HotSwapKinds(images)), "interface images");
	CHECK_EQ(HotSwapKindsText(kHotPatch | kHotArt), "patches of data/, interface images");
	// The game's paths of the images, once each, with the volume turned into its folder.
	const std::vector<std::string> real = HotSwapArtFiles({"gameres/icons/RHastatus.bmp", "UI/Icons/rhastatus.bmp", "data/const.ini", "gameres/selections/a.rle",
		"minimap/zoom8/entities/x.bmp", "gameres\\cmdbar\\Actions\\a b.bmp"});
	CHECK_EQ(real.size(), size_t(3));
	CHECK_EQ(real[0], "ui/icons/rhastatus.bmp");
	CHECK_EQ(real[1], "minimap/zoom8/entities/x.bmp");
	CHECK_EQ(real[2], "ui/cmdbar/actions/a b.bmp");
	CHECK(HotSwapArtFiles({}).empty());
}

void TextsAreSwitchedInPlaceExceptTheSplashScreens()
{
	const char *const text[] = {"loc/english.loc.xml", "loc/spanish/extra.loc.xml", "Loc\\Italian.loc.xml", "local/spanish/tips.xml", "local/english/help.xml",
		"local/spanish/copyrights.txt", "local/spanish/sub/more.ini", "patches/currentlang/game.loc.xml", "currentlang/tips.xml"};
	for (const char *f : text) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotText));
	// Read once at start, voices, paks, pictures and what is not a text keep the restart.
	const char *const other[] = {"local/spanish/splash.ini", "local/english/endsplash1.ini", "currentlang/endsplash2.ini",
		"local/spanish/game.pak", "local/spanish/conquestdefault.bmp", "loc/english.txt", "loc/", "local/spanish.xml", "local//a.xml", "patches/currentlang/a.bmp",
		"patches/currentlang/voices/a.xml"};
	for (const char *f : other) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	CHECK(HotSwapTextReadOnce("local/spanish/splash.ini"));
	CHECK(!HotSwapTextReadOnce("local/spanish/tips.xml"));
	CHECK(HotSwapReadOnce("currentlang/splash.ini"));
	// A layer of texts and images is reloaded; with a splash screen it is not and says why.
	const Files mixed = {"loc/english.loc.xml", "local/spanish/tips.xml", "gameres/icons/a.bmp", "patches/data/const.ini"};
	CHECK(SwapDecide(false, {}, mixed) == HotSwap::kReload);
	CHECK_EQ(HotSwapKinds(mixed), static_cast<unsigned>(kHotText | kHotArt | kHotPatch));
	CHECK_EQ(HotSwapKindsText(kHotText), "texts");
	CHECK(SwapDecide(false, {}, {"loc/english.loc.xml", "local/spanish/splash.ini"}) == HotSwap::kRestart);
	const RestartReason r = HotSwapRestartReason({}, {"loc/english.loc.xml", "local/spanish/splash.ini"});
	CHECK(r.why == RestartWhy::kReadOnce);
	CHECK_EQ(r.files.size(), size_t(1));
}

void SoundsAreSwitchedInPlaceAndTheEntityTableIsReadAgain()
{
	const char *const sound[] = {"sounds/notifications/barrack idle.wav", "Sounds\\clash\\chime.ogg", "music/gbr_track_9.ogg", "local/spanish/voices/rhastatus/1.wav",
		"local/english/voices/a/b.ogg", "currentlang/voices/rhastatus/2.wav"};
	for (const char *f : sound) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotSound));
	const char *const entity[] = {"data/sound entities/VoiceRHastatus.xml", "patches/data/sound entities/BarracksSelect.xml", "Data\\Sound Entities\\a.xml"};
	for (const char *f : entity) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotSoundData));
	// What is not an audio file of those folders, or is another kind of file in them.
	const char *const other[] = {"sounds/a.txt", "sounds/", "music/a.mp3", "local/spanish/a.wav", "local/spanish/voices/", "data/sounds/x.wav", "data/sound entities/a.txt",
		"local/voices/a.wav", "sounds"};
	for (const char *f : other) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	// The entity files were data (kHotLoose, kHotPatch) that the game never read again: they are now the kind that makes the reload build the table again.
	CHECK_EQ(HotSwapKinds({"data/sound entities/a.xml", "patches/data/sound entities/b.xml", "data/classes/c.xml"}), static_cast<unsigned>(kHotSoundData | kHotLoose));
	const Files audio = {"sounds/clash/chime.wav", "local/spanish/voices/rhastatus/1.wav", "patches/data/sound entities/barracksselect.xml", "loc/english.loc.xml"};
	CHECK(SwapDecide(false, {}, audio) == HotSwap::kReload);
	CHECK(SwapDecide(false, audio, {"gameres/icons/a.bmp", "music/a.ogg"}) == HotSwap::kReload);
	CHECK_EQ(HotSwapKindsText(kHotSound | kHotSoundData), "sounds, sound entities");
}

void EntityArtIsSwitchedInPlaceByFolder()
{
	const char *const art[] = {"units/RHastatus/Idle.rle", "Units\\RHastatus\\RHastatus.ent.xml", "buildings/BBarracks/BBarracks.ent.xml", "mapobjects/decors/tr1/tr1_s.rle",
		"visuals/arrow/arrow1.rle", "units/rhastatus/idle.rle.mmp", "units/x/y.bmp"};
	for (const char *f : art) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotEntity));
	// The passability and everything else of an entity folder is gameplay or not covered: it keeps the restart.
	const char *const other[] = {"units/rhastatus/rhastatus.pass", "units/rhastatus/readme.txt", "units/", "units/a.xml", "units/rhastatus/", "rle.mmp.tail", "unitsx/a.rle",
		"data/units/a.rle", "units/rhastatus/idle.mmp"};
	for (const char *f : other) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	const std::vector<std::string> folders = HotSwapEntityFolders({"units/RHastatus/Idle.rle", "Units\\RHastatus\\Walk.rle", "units/rhastatus/rhastatus.ent.xml",
		"buildings/bbarracks/bbarracks.ent.xml", "data/const.ini", "units/x.rle"});
	CHECK_EQ(folders.size(), size_t(3));
	CHECK_EQ(folders[0], "units/rhastatus");
	CHECK_EQ(folders[1], "buildings/bbarracks");
	CHECK_EQ(folders[2], "units");
	CHECK(HotSwapEntityFolders({}).empty());
	CHECK(SwapDecide(false, Files{"units/rhastatus/idle.rle"}, {"units/rhastatus/rhastatus.ent.xml", "sounds/a.wav"}) == HotSwap::kReload);
	CHECK_EQ(HotSwapKindsText(kHotEntity), "entity art");
}

void MapsAreSwitchedInPlace()
{
	const char *const maps[] = {"scenarios/Siege.bfhp", "scenarios/Siege.bmp", "Adventures\\Druid.bfhp", "conquests/Gaul.bfhp", "conquestmaps/x.bfhp", "scenarios/readme.txt",
		"packs/RandomMapSettlements.bfhp", "randommap/Anillo/010.bmp", "RandomMap\\Anillo\\120.bmp", "randommapsettlements/maps/1/map.obj.xml",
		"randommapsettlements/maps/1/terrain.decor.grid"};
	for (const char *f : maps) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotMap));
	// A pak is mounted into the engine's list once, at start; the DLL has no way to take one out: it keeps the restart. So do the other kinds in those folders.
	const char *const other[] = {"packs/units.pak", "packs/sub/a.bfhp", "scenarios/", "scenarios/a.exe", "scenarios/a.pak", "randommap/Anillo/a.xml", "randommap/", "adventures",
		"randommapsettlements/", "randommapsettlements/a.dll", "conquests/x.dll"};
	for (const char *f : other) CHECK_EQ(HotSwapKindOf(f), static_cast<unsigned>(kHotOther));
	// data\randommap\*.rm.xml (the types) and its tables are data, as before.
	CHECK_EQ(HotSwapKindOf("data/randommap/Anillo.rm.xml"), static_cast<unsigned>(kHotLoose));
	CHECK(SwapDecide(false, {}, {"scenarios/a.bfhp", "data/randommap/Anillo.rm.xml", "randommap/Anillo/010.bmp"}) == HotSwap::kReload);
	CHECK(SwapDecide(false, {"packs/a.bfhp"}, {"packs/a.pak"}) == HotSwap::kRestart);
	CHECK_EQ(HotSwapKindsText(kHotMap), "maps");
}

void TheGraphicsTailKeepsTheRestartAndSaysWhy()
{
	// Whatever else is in the layer, the tail names itself (unless something read once is also there: that is told first).
	RestartReason r = HotSwapRestartReason({}, {"rle.mmp.tail", "packs/a.pak", "civs/x.json"});
	CHECK(r.why == RestartWhy::kGraphicsTail);
	CHECK_EQ(r.files.size(), size_t(1));
	CHECK_EQ(r.files[0], "rle.mmp.tail");
	r = HotSwapRestartReason({"Rulesets\\a\\RLE.MMP.TAIL"}, {});
	CHECK(r.why == RestartWhy::kGraphicsTail);
	r = HotSwapRestartReason({}, {"rle.mmp.tail", "gameres/selections/a.rle"});
	CHECK(r.why == RestartWhy::kReadOnce);
	CHECK(HotSwapRestartReason({}, {"packs/a.pak"}).why == RestartWhy::kOtherFiles);
	CHECK(HotSwapRestartReason({}, {"xrle.mmp.tail"}).why == RestartWhy::kOtherFiles);
	CHECK_EQ(HotSwapKindOf("rle.mmp.tail"), static_cast<unsigned>(kHotOther));
	CHECK(SwapDecide(false, {}, {"rle.mmp.tail"}) == HotSwap::kRestart);
}

void TheRestartSaysWhyBeforeItHappens()
{
	CHECK(HotSwapRestartReason({}, {}).why == RestartWhy::kNone);
	CHECK(HotSwapRestartReason({"gameres/icons/a.bmp", "patches/data/const.ini"}, {"modes/a.json"}).why == RestartWhy::kNone);
	// A file read once is the reason even when a sound comes first.
	RestartReason r = HotSwapRestartReason({"packs/a.pak"}, {"gameres/icons/a.bmp", "gameres/selections/a.rle", "ui/cursors/b.cur"});
	CHECK(r.why == RestartWhy::kReadOnce);
	CHECK_EQ(r.files.size(), size_t(2));
	CHECK_EQ(r.files[0], "gameres/selections/a.rle");
	// Only the not covered kinds.
	r = HotSwapRestartReason({"packs/a.pak"}, {"civs/gaul.json", "sounds/a.wav", "packs/a.pak", "specials/a.json", "x/y.z"});
	CHECK(r.why == RestartWhy::kOtherFiles);
	CHECK_EQ(r.files.size(), size_t(kRestartReasonFiles));
	CHECK_EQ(r.files[0], "packs/a.pak");
}

int main()
{
	TheImagesOfAMatchAreSwitchedInPlaceAndTheOnesReadOnceAreNot();
	TextsAreSwitchedInPlaceExceptTheSplashScreens();
	SoundsAreSwitchedInPlaceAndTheEntityTableIsReadAgain();
	EntityArtIsSwitchedInPlaceByFolder();
	MapsAreSwitchedInPlace();
	TheGraphicsTailKeepsTheRestartAndSaysWhy();
	TheRestartSaysWhyBeforeItHappens();
	TheSameRulesetNeedsNothing();
	ConstIniAloneIsJustOneKindOfFile();
	TheTextNeedsAGamePlaySection();
	OnlyTheKindsOfFileTheGameCanReadAgainAreSwitched();
	ASavedGameSwitchesInPlaceOnlyWhenEverythingAgrees();
	EachKindOfFileIsAcceptedOnItsOwn();
	EveryKindOfGameThatTheEngineMakesIsKnown();
	SavesOfEveryKindOfGameButReplaysSwitchInPlace();
	ASwitchChosenInAGameWaitsOnlyWhereTheNextGameIsLoadedThroughLoadGameData();
	AWaitingSwitchExpires();
	GlobalRootFilesCountAsFilesThatComeWithARuleset();
	return test::Summary("hotswap");
}
