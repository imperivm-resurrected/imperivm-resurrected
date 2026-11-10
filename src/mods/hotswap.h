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
// layer that is mounted in place), the lobby's mode files modes/*.json (kHotModes) and hidden.txt (kHotHidden). kHotOther is anything else.
enum HotSwapKind : unsigned { kHotNone = 0, kHotPatch = 1, kHotLoose = 2, kHotModes = 4, kHotHidden = 8, kHotOther = 16 };
unsigned HotSwapKindOf(const std::string &file);
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

// The kinds of game the engine makes (CreateGameFromSettings) and which one a game object is, by its vtable. Every one of them runs
// CVXBasicGame::LoadGameData on its empty game object, where the DLL reads the game's data again: single-player (also saved games and
// replays), multiplayer, adventure (campaign), conquest and editor. kUnknown is any other vtable: the data is not read again for it.
enum class GameKind { kUnknown, kSkirmish, kMultiplayer, kAdventure, kConquest, kEditor };
GameKind GameKindOfVtable(uintptr_t vtable);
const char *GameKindName(GameKind kind);

// Whether a saved game of this kind (SaveKind in savelib.h) is loaded through one of those games, so the saves screen can say the ruleset
// it was saved with is switched in place: strategy, adventure and conquest saves. A replay has no ruleset to switch to.
bool SaveKindSwitchesInPlace(int saveKind);
