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

// A mounted folder is mods\<pack> or mods\<pack>\rulesets\<id>. Both give the folder of the pack...
std::string PackFolderOfDir(const std::string &modsDir, const std::string &dir);

// ...and the path below mods/ with '/' separators, which is how the game's file system names it.
std::string ModDirVfsName(const std::string &modsDir, const std::string &dir);

}
