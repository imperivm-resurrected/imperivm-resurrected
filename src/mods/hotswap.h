// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// What a change of ruleset needs. The game reads most of what a ruleset carries once per process, so the change restarts the game,
// except when the ruleset holds only the kinds of file below: then the DLL makes the game read its data again between two matches
// (game/hotreload.cpp), with no restart.
enum class HotSwap {
	kNothing, // the same ruleset
	kRestart, // anything else
	kReload   // only the kinds of file below change: the game is made to read its data again
};

// files: everything a ruleset folder holds (paths relative to the folder, '/' separators, documents left out). A ruleset can be switched
// in place only if it holds nothing but the kinds below. Anything else (paks, art, sounds, texts, rle.mmp.tail, civs, specials, options,
// events, rules, patches to units/ or buildings/...) keeps the restart. Nothing at all (the game's own rules, a package without a layer) passes.
bool HotSwapLayerOk(const std::vector<std::string> &files);

// What kinds of files a ruleset has, to say it in the log and to test each one: patches to data/ (kHotPatch), new or
// replacing files under data/ (kHotLoose: .xml, .ini, .vs; the game reads them when it starts or when the match does, from the loose
// layer that is mounted in place), the lobby's mode files modes/*.json (kHotModes), hidden.txt (kHotHidden) and the images of the interface
// that belong to a match (kHotArt: .bmp and .rle in gameres/ (ui/) icons, cmdbar, infobar, noteicons, help and conversations, and the
// minimap/zoomN/entities pictures; the game keeps them in caches that the reload empties, see game/artcache.h) and the texts (kHotText: the
// loc\<language>.loc.xml files, the files of local\<language>\ and currentlang\ that are .xml, .txt or .ini, patches to currentlang\; the reload
// makes the game read its table of texts again), the sounds (kHotSound: .wav and .ogg in sounds\, music\ and the voices folders; the game opens them each time
// it plays one) and the sound entities (kHotSoundData: data\sound entities\*.xml and its patches, which the game reads once into a table that the reload
// builds again), and the art of the entities (kHotEntity: .ent.xml, .rle and .bmp in units\, buildings\, mapobjects\ and visuals\; the game keeps each
// entity it loaded in a cache, and the reload takes out of it those whose folder holds a file that changed) and the maps (kHotMap: the .bfhp, .bmp and documents of
// scenarios\, adventures\, conquests\ and conquestmaps\, the packs\*.bfhp, the random map pictures of randommap\ and the city templates of
// randommapsettlements\; the game lists them from disk and opens them when a game is made: nothing is kept). kHotOther is anything else.
enum HotSwapKind : unsigned { kHotNone = 0, kHotPatch = 1, kHotLoose = 2, kHotModes = 4, kHotHidden = 8, kHotOther = 16, kHotArt = 32, kHotText = 64, kHotSound = 128, kHotSoundData = 256, kHotEntity = 512, kHotMap = 1024 };
unsigned HotSwapKindOf(const std::string &file);

// Whether a file is one the game reads once, when it starts, or when a menu window is built, and never again for a match: the selection circles,
// the cursors and their table, the fonts, the pictures of the menus and of the other windows that are not a match's, the layouts of the
// interface (gameini/, menuini/, data/interface/). A ruleset that has one keeps the restart, and says so.
bool HotSwapReadOnce(const std::string &file);

// Whether a file is a text of the game that is read once, at start (the splash screens): it keeps the restart although it is a text.
bool HotSwapTextReadOnce(const std::string &file);

// The folders (units/rhastatus, lowercase, "/") of the entity art among files (kHotEntity), once each. Every file of an entity (its .ent.xml and the images it
// names) is in the folder of the entity's .ent.xml, so a changed file means the entity of that folder has to be loaded again.
std::vector<std::string> HotSwapEntityFolders(const std::vector<std::string> &files);

// A .pak of a ruleset's folder and what its index says (names lowercase, "/").
struct HotSwapPak {
	std::string rel; // packs/x.pak or local/x.pak
	bool language = false;
	bool unreadable = false;
	std::vector<std::string> members;
};

// What a ruleset's folder brings, with each of its paks replaced by the files inside: the DLL mounts the paks of the ruleset in place (the engine's
// CPackFS objects are taken out of its list of file systems and put back), so what counts for the switch is what is inside, each member by the same
// rules as a loose file. A language pak (local/<language>*.pak) of another language is not mounted by the game: it brings nothing. A pak that is not in
// paks (a pak of patches/, one that could not be read, one that is not in packs/ or local/) stays in the list as itself: it keeps the restart.
// The members of a language pak are relative to the language folder: currentlang/... as is, the rest are voices (local/<language>/voices/<member>).
std::vector<std::string> HotSwapExpandPaks(const std::vector<std::string> &files, const std::vector<HotSwapPak> &paks, const std::string &language);

// The game's own paths (ui/..., lowercase, "/") of the images among files (kHotArt), as the caches name them after the volume gameres/ is
// turned into ui/. These are the files whose cached copy the reload has to be sure it replaced.
std::vector<std::string> HotSwapArtFiles(const std::vector<std::string> &files);

// Why a change of ruleset keeps the restart, to tell the player before it happens: the first files that are the reason, and which of the two
// reasons it is (the file is read once, see HotSwapReadOnce; or it is one of the kinds the reload does not cover yet: sounds, voices, texts,
// packs, sprites...). kNone: nothing keeps it.
// A third one has its own sentence: the graphics tail (rle.mmp.tail), which the game maps once and every sprite keeps pointers into.
enum class RestartWhy { kNone, kReadOnce, kGraphicsTail, kOtherFiles };
struct RestartReason {
	RestartWhy why = RestartWhy::kNone;
	std::vector<std::string> files; // at most kRestartReasonFiles
};
const unsigned kRestartReasonFiles = 3;
RestartReason HotSwapRestartReason(const std::vector<std::string> &from, const std::vector<std::string> &to);
unsigned HotSwapKinds(const std::vector<std::string> &files);
std::string HotSwapKindsText(unsigned kinds);

// Whether loading a saved game needs no restart although the ruleset it was saved with is not the mounted one: the game is one whose load
// the full reload covers (a skirmish or an online game, not a campaign, a conquest or a replay), every package it needs is enabled and only
// the ruleset differs, there is nothing the restart would have switched off, and the switch itself can be made in place. The saves screen
// (list mark, details, button) and the load action both ask this, so what they say is what happens.
bool SaveSwitchesInPlace(bool plainGame, bool needsActivation, bool rulesetOnly, bool somethingExtra, bool switchQualifies);

// Whether the text of a const.ini can be loaded: it has a [GamePlay] section (every constant the game reads is in it). A file that
// cannot be read, or an empty one, would leave the game's table empty.
bool ConstIniHasGamePlay(const std::string &text);

// What a change from the rules the game holds to the ones mounted needs: the same rules need nothing, two that pass HotSwapLayerOk are
// reloaded, anything else restarts.
HotSwap SwapDecide(bool sameRuleset, const std::vector<std::string> &from, const std::vector<std::string> &to);

// The files that come or go with a ruleset: those of its folder, and the global files of the root of its package (the ones that replace the game's
// and are mounted only under a ruleset of that package, see mods/rootrules.h), unless the ruleset it is compared with is of the same package: then
// they are mounted both before and after and change nothing. Art among them is as much an art change as art in the folder.
std::vector<std::string> SwapFilesOf(const std::vector<std::string> &layerFiles, const std::vector<std::string> &packGlobal, bool samePackAsOther);

// The kinds of game the engine makes (CreateGameFromSettings) and which one a game object is, by its vtable. Every one of them runs
// CVXBasicGame::LoadGameData on its empty game object, where the DLL reads the game's data again: single-player (also saved games and
// replays), multiplayer, adventure (campaign), conquest and editor. kUnknown is any other vtable: the data is not read again for it.
enum class GameKind { kUnknown, kSkirmish, kMultiplayer, kAdventure, kConquest, kEditor };
GameKind GameKindOfVtable(uintptr_t vtable);
const char *GameKindName(GameKind kind);

// Whether a saved game of this kind (SaveKind in savelib.h) is loaded through one of those games, so the saves screen can say the ruleset
// it was saved with is switched in place: strategy, adventure and conquest saves. A replay has no ruleset to switch to.
bool SaveKindSwitchesInPlace(int saveKind);

// A switch chosen while a game is running (the Load screen of the in-game menu) is not mounted at once: the running game is still alive until
// the engine tears it down to load the other one. It waits, and is mounted at the start of the next game's LoadGameData, when no part of the old
// one exists. Only the games whose load goes through that point can wait for it: a skirmish, a campaign or a conquest (also a replay in progress).
// The editor loads its files in the same game object and multiplayer games do not load saves: for those the restart is asked before.
bool GameKindCanSwitchWhileRunning(GameKind kind);
// How long a waiting switch is good for. A load that never reaches LoadGameData (it was cancelled, the file is bad) must not change the rules of
// a game that starts much later. Ticks are milliseconds of one clock, compared with wrap-around.
const unsigned kPendingSwitchMs = 15000;
bool PendingSwitchStillValid(unsigned chosenTick, unsigned nowTick);
