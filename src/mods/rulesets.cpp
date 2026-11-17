// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/rulesets.h"

#include <algorithm>

#include "core/pathsafe.h"
#include "mods/modrules.h"

namespace mods_impl {

namespace {
bool Inside(const std::string &path, const std::string &parent)
{
	return path.size() > parent.size() && path.compare(0, parent.size(), parent) == 0 && path[parent.size()] == '/';
}

std::string Normal(std::string path)
{
	path = NormPath(path);
	while (!path.empty() && path.back() == '/') path.pop_back();
	return path;
}
}

bool RulesetIdValid(const std::string &id)
{
	if (id.empty() || id.size() > kRulesetIdMax) return false;
	for (char c : id) {
		if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') return false;
	}
	return true;
}

std::vector<RulesetIssue> RulesetResolve(std::vector<RulesetDecl> &decls)
{
	std::vector<RulesetIssue> issues;
	std::vector<RulesetDecl> kept;
	for (const RulesetDecl &d : decls) {
		if (!RulesetIdValid(d.id)) {
			issues.push_back({RulesetProblem::kBadId, d.id, ""});
			continue;
		}
		const auto same = std::find_if(kept.begin(), kept.end(), [&](const RulesetDecl &k) { return k.id == d.id; });
		if (same != kept.end()) {
			issues.push_back({RulesetProblem::kDuplicate, d.id, ""});
			continue;
		}
		RulesetDecl layer = d;
		layer.path = Normal(d.path.empty() ? std::string(kRulesetsFolder) + d.id : d.path);
		if (!pathsafe::SafeRelative(layer.path) || !Inside(layer.path, "rulesets")) {
			issues.push_back({RulesetProblem::kBadPath, d.id, ""});
			continue;
		}
		const auto clash = std::find_if(kept.begin(), kept.end(), [&](const RulesetDecl &k) {
			return k.path == layer.path || Inside(k.path, layer.path) || Inside(layer.path, k.path);
		});
		if (clash != kept.end()) {
			issues.push_back({RulesetProblem::kOverlap, d.id, clash->id});
			continue;
		}
		kept.push_back(layer);
	}
	decls = kept;
	return issues;
}

std::vector<RulesetEntry> RulesetEntries(const std::vector<RulesetPackFacts> &packs)
{
	std::vector<RulesetEntry> out;
	for (size_t i = 0; i < packs.size(); i++) {
		if (!packs[i].usable) continue;
		for (size_t l = 0; l < packs[i].layers; l++) out.push_back({i, static_cast<int>(l)});
		if (packs[i].layers == 0 && (packs[i].hasCivs || packs[i].hasGlobal)) out.push_back({i, -1});
	}
	return out;
}

bool RulesetPathsHaveCivs(const std::vector<std::string> &paths)
{
	static const char kFolder[] = "civs/", kExt[] = ".json";
	const size_t folder = sizeof(kFolder) - 1, ext = sizeof(kExt) - 1;
	for (const std::string &p : paths) {
		if (p.size() > folder + ext && p.compare(0, folder, kFolder) == 0 && p.compare(p.size() - ext, ext, kExt) == 0 && p.find('/', folder) == std::string::npos) return true;
	}
	return false;
}

int RulesetOwnerOf(const std::vector<RulesetDecl> &layers, const std::string &rel)
{
	for (size_t i = 0; i < layers.size(); i++) {
		if (Inside(rel, layers[i].path)) return static_cast<int>(i);
	}
	return -1;
}

PackKind PackKindOf(bool rootGameplay, bool rootContent, bool rulesetGameplay)
{
	return rootGameplay || rulesetGameplay ? PackKind::kRules : rootContent ? PackKind::kContent : PackKind::kCosmetic;
}

std::string ModDirVfsName(const std::string &modsDir, const std::string &dir)
{
	const std::string base = modsDir + "\\";
	std::string rest;
	if (dir.size() > base.size() && Lower(dir.substr(0, base.size())) == Lower(base)) rest = dir.substr(base.size());
	else rest = dir.substr(dir.find_last_of("\\/") == std::string::npos ? 0 : dir.find_last_of("\\/") + 1);
	std::replace(rest.begin(), rest.end(), '\\', '/');
	return rest;
}

std::string PackFolderOfDir(const std::string &modsDir, const std::string &dir)
{
	const std::string rest = ModDirVfsName(modsDir, dir);
	return rest.substr(0, rest.find('/'));
}

}
