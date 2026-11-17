// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <unordered_set>
#include <vector>

namespace mods_impl {

// The root of a package may ADD files to the game but may not REPLACE the game's. A loose file of the root of a package that has civilizations,
// at the path of a file the game already has, is *global*: it changes the game for every room. It is mounted only when the ruleset in use is
// that package's own (one of its rulesets, or the package itself when it declares none); under any other ruleset, or the game's own, it is
// ignored and the game's file is the one used. A file with a name the game does not have is always mounted, and a package with no
// civilizations (cosmetic art, content, a rules package with no civilizations) is not affected.

// The names of the files the game has, as game paths: lowercase, "/", with the real folder of a translated volume ("ui/icons/x.bmp").
using GameFileSet = std::unordered_set<std::string>;

// A path as a package writes it (any case, "\" or "/") in the form the game files are kept in: lowercase, "/", the volume replaced by its real folder
// ("gameres/icons/x.bmp" is "ui/icons/x.bmp").
std::string RootGamePath(const std::string &modPath);

// A file of the menu skin (menu INIs, buttons, backgrounds, logos, tips and help). It is a feature of its own that is not there yet, and the
// skin of a civilization pack keeps working from its root: it is never global. gamePath is the form RootGamePath gives.
bool RootIsMenuSkin(const std::string &gamePath);

// The files of a package root (its loose files, as the package names them) that are global: the game has a file at that path and it is not part
// of the menu skin. Lowercase, "/", sorted, without repeats.
std::vector<std::string> RootGlobalFiles(const std::vector<std::string> &rootLoose, const GameFileSet &game);

// The sidecar of a saved game (<save>.mods.json) names the ruleset of each package by the id of its layer ("ruleset": "plain"). The entry of a package
// that has root files that replace the game's and no layers (the package itself) has no layer: it is recorded as "rulesetPackage": true on that package's
// line, which an older version ignores (it then sees what it always saw: no ruleset). A room says the same with the "@package" line of its mods list.
const char kSaveRulesetPackageField[] = "rulesetPackage";

// Whether the package entry is what is mounted, as opposed to the game's own rules: the ruleset names this package with no layer, and the package has
// global root files (without them the entry mounts nothing more than the game's own rules, and is not worth recording).
bool RootEntryMounted(const std::string &packFolder, const std::string &rulesetPack, const std::string &rulesetLayer, bool packHasGlobal, bool packHasLayers);

// Whether the global files of the package are mounted: the ruleset in use belongs to it. rulesetPack is empty for the game's own rules.
bool RootGlobalApplies(const std::string &packFolder, const std::string &rulesetPack);

// What the loose layer of a package must answer "not here" to: the global files under both names (the package's and the real one), lowercase.
std::unordered_set<std::string> RootRefusals(const std::vector<std::string> &global);

// Whether a path the engine asks (any case, "\" or "/") is one of the refusals.
bool RootRefuses(const std::unordered_set<std::string> &refusals, const std::string &asked);

}
