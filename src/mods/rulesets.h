// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

namespace mods_impl {

// A ruleset is a folder of a content package shaped like a package root (rulesets/<id>/patches, data, modes...). Only the
// one the host chooses is mounted, so the rules of different packages never overlap.
struct RulesetDecl {
	std::string id;
	std::string path;
};

enum class RulesetProblem { kBadId, kDuplicate, kBadPath, kOverlap };

struct RulesetIssue {
	RulesetProblem problem{};
	std::string id;
	std::string other;
};

const size_t kRulesetIdMax = 24;
const char kRulesetsFolder[] = "rulesets/";

bool RulesetIdValid(const std::string &id);

// Gives each path its normal form (lower case, '/', no trailing '/'; a missing one becomes rulesets/<id>) and drops the
// layers that cannot be used: a bad id, an id declared before, a path outside rulesets/, a path inside or equal to the one
// of an earlier layer. What stays never overlaps.
std::vector<RulesetIssue> RulesetResolve(std::vector<RulesetDecl> &decls);

// The index of the layer that holds rel (a normalized path relative to the package), or -1 when it belongs to the root.
int RulesetOwnerOf(const std::vector<RulesetDecl> &layers, const std::string &rel);

// What the Rules combo may offer for a package: the facts about it, then one entry per ruleset it declares, or, when it declares
// none and has civilizations, one entry for the package alone (layer -1). A package that cannot be used (forbidden files) offers nothing.
struct RulesetPackFacts {
	bool usable = true;
	bool hasCivs = false;
	// The root has loose files that replace the game's (mods/rootrules.h): they are mounted only under the package's own ruleset, so a package
	// with none of its own and no civilizations needs the entry that bears its name to be chosen.
	bool hasGlobal = false;
	size_t layers = 0;
};

struct RulesetEntry {
	size_t pack = 0;
	int layer = -1;
};

std::vector<RulesetEntry> RulesetEntries(const std::vector<RulesetPackFacts> &packs);

// Whether the paths of a package (normalized, relative to its root) hold a civilization: civs/<name>.json.
bool RulesetPathsHaveCivs(const std::vector<std::string> &paths);

// What a package is, from what its root and its rulesets hold: rules if its root or any of its rulesets holds gameplay files, else
// content if the root holds maps or battles, else cosmetic. The files of a ruleset count: a package whose only gameplay is in
// rulesets/<id>/patches/data/const.ini is a rules package (the numbers are those of ModKind in mods.h).
enum class PackKind { kCosmetic = 0, kContent = 1, kRules = 2 };
PackKind PackKindOf(bool rootGameplay, bool rootContent, bool rulesetGameplay);

// A mounted folder is mods\<pack> or mods\<pack>\rulesets\<id>. Both give the folder of the pack...
std::string PackFolderOfDir(const std::string &modsDir, const std::string &dir);

// ...and the path below mods/ with '/' separators, which is how the game's file system names it.
std::string ModDirVfsName(const std::string &modsDir, const std::string &dir);

}
