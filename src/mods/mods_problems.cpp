// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "i18n/tr.h"
#include "mods/mappack.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

std::string ModsActiveProfileName()
{
	char buf[256] = {};
	GetPrivateProfileStringA("Mods", "Profile", kOriginal, buf, sizeof(buf), IniPath().c_str());
	return buf[0] ? buf : kOriginal;
}

void ModsSetActiveProfileName(const std::string &name)
{
	WritePrivateProfileStringA("Mods", "Profile", name.c_str(), IniPath().c_str());
	const ModProfile p = ModsLoadProfile(name);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_mount.known && g_mount.profile.name != p.name && ModsSameSet(p.mods, g_mount.profile.mods)) g_mount.profile.name = p.name;
}

bool ModsScenarioProvider(const std::string &scenarioPath, std::string &id, std::string &version, std::string &name)
{
	const std::string want = NormPath(scenarioPath) + ".bfhp";
	std::lock_guard<std::mutex> lock(g_lock);
	for (const std::string &folder : g_mount.profile.mods) {
		for (const ModInfo &m : g_mods) {
			if (m.folder != folder || std::find(m.paths.begin(), m.paths.end(), want) == m.paths.end()) continue;
			id = m.id;
			version = m.version;
			name = m.name;
			return true;
		}
	}
	return false;
}

bool ModsScenarioPack(const std::string &scenarioPath, ModsMapPack &out)
{
	const std::string want = NormPath(scenarioPath) + ".bfhp";
	std::lock_guard<std::mutex> lock(g_lock);
	for (const std::string &folder : g_mount.profile.mods) {
		for (const ModInfo &m : g_mods) {
			if (m.folder != folder || std::find(m.paths.begin(), m.paths.end(), want) == m.paths.end()) continue;
			MapPackFacts facts;
			facts.rules = m.kind == kModRules;
			facts.rleTail = m.rleTail;
			facts.forbidden = m.forbidden.size();
			facts.patchValues = m.patchValues.size();
			facts.hidden = m.hidden.size();
			out = ModsMapPack();
			out.id = m.id;
			out.version = m.version;
			out.name = m.name;
			out.gbId = m.gbId;
			out.simple = MapPackIsSimple(facts, m.paths);
			out.shareable = m.redistributable && m.forbidden.empty();
			std::string rel = want;
			std::replace(rel.begin(), rel.end(), '/', '\\');
			out.file = ModsDir() + "\\" + m.folder + "\\" + rel;
			return true;
		}
	}
	return false;
}

std::string ModsInstalledPackMap(const std::string &id, const std::string &version, const std::string &scenarioPath)
{
	const std::string want = NormPath(scenarioPath) + ".bfhp";
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if (_stricmp(m.id.c_str(), id.c_str()) != 0 || m.version != version) continue;
		if (std::find(m.paths.begin(), m.paths.end(), want) == m.paths.end()) continue;
		std::string rel = want;
		std::replace(rel.begin(), rel.end(), '/', '\\');
		return ModsDir() + "\\" + m.folder + "\\" + rel;
	}
	return "";
}

bool ModsMountedHasExact(const std::string &id, const std::string &version)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const std::string &folder : g_mount.profile.mods) {
		for (const ModInfo &m : g_mods) {
			if (m.folder == folder && _stricmp(m.id.c_str(), id.c_str()) == 0 && m.version == version) return true;
		}
	}
	return false;
}

const ModProfile &ModsMountedProfile()
{
	static ModProfile original;
	if (original.name.empty()) {
		original.name = kOriginal;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	return g_mount.known ? g_mount.profile : original;
}

bool ModsLoadsWithoutRestart(const std::string &folder)
{
	ModInfo m;
	if (!ModsFind(folder, m)) return false;
	MapPackFacts facts;
	facts.rules = m.kind == kModRules;
	facts.rleTail = m.rleTail;
	facts.forbidden = m.forbidden.size();
	facts.patchValues = m.patchValues.size();
	facts.hidden = m.hidden.size();
	return PackLoadsWithoutRestart(facts, m.paths);
}

bool ModsRestartNeeded()
{
	ModProfile active = ModsLoadProfile(ModsActiveProfileName());
	active.mods.erase(std::remove_if(active.mods.begin(), active.mods.end(), [](const std::string &f) {
		ModInfo m;
		return !ModsFind(f, m);
	}), active.mods.end());
	const ModProfile &mounted = ModsMountedProfile();
	if (!ModsSameSet(active.mods, mounted.mods)) return true;
	if (!ModsSameRuleset(ModsEffectiveRuleset(active), mounted.ruleset)) return true;
	std::map<std::string, std::string> signatures;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		signatures = g_mount.signatures;
	}
	for (const auto &mountedPack : signatures) {
		ModInfo m;
		if (ModsFind(mountedPack.first, m) && ModSignature(m) != mountedPack.second && !ModsLoadsWithoutRestart(mountedPack.first)) return true;
		if (IsDir(ModsDir() + "\\_downloads\\pending\\" + mountedPack.first)) return true;
	}
	std::map<std::string, std::string> chosen;
	if (!active.choices.empty()) chosen = ChosenFolders(active, ModsList());
	std::lock_guard<std::mutex> lock(g_lock);
	return chosen != g_mount.chosen;
}

ModRuleset ModsEffectiveRuleset(const ModProfile &p)
{
	if (p.ruleset.pack.empty()) return ModRuleset();
	const bool inProfile = std::find_if(p.mods.begin(), p.mods.end(), [&](const std::string &f) { return _stricmp(f.c_str(), p.ruleset.pack.c_str()) == 0; }) != p.mods.end();
	ModInfo m;
	if (!inProfile || !ModsFind(p.ruleset.pack, m) || !m.forbidden.empty()) return ModRuleset();
	ModRuleset out;
	out.pack = m.folder;
	if (p.ruleset.id.empty()) {
		// The package without a layer is an entry only for a package that has civilizations and no ruleset; it mounts nothing.
		if (!m.layers.empty() || !RulesetPathsHaveCivs(m.paths)) return ModRuleset();
		return out;
	}
	const ModLayer *layer = ModsFindLayer(m, p.ruleset.id);
	if (!layer) return ModRuleset();
	out.id = layer->id;
	return out;
}

std::vector<ModRulesetEntry> ModsRulesetEntries(const std::vector<std::string> &folders)
{
	std::vector<ModInfo> packs;
	std::vector<RulesetPackFacts> facts;
	for (const std::string &folder : folders) {
		ModInfo m;
		if (!ModsFind(folder, m)) continue;
		RulesetPackFacts f;
		f.usable = m.forbidden.empty();
		f.hasCivs = RulesetPathsHaveCivs(m.paths);
		f.layers = m.layers.size();
		facts.push_back(f);
		packs.push_back(std::move(m));
	}
	std::vector<ModRulesetEntry> out;
	for (const RulesetEntry &e : RulesetEntries(facts)) {
		const ModInfo &m = packs[e.pack];
		ModRulesetEntry entry;
		entry.ruleset.pack = m.folder;
		if (e.layer >= 0) {
			entry.ruleset.id = m.layers[e.layer].id;
			entry.layerName = m.layers[e.layer].name;
		}
		entry.packName = m.name;
		out.push_back(entry);
	}
	return out;
}

std::string ModsRulesetCivSet(const ModRuleset &choice)
{
	ModInfo m;
	if (choice.pack.empty() || !ModsFind(choice.pack, m) || !RulesetPathsHaveCivs(m.paths)) return std::string();
	return ModsCivSetId(m);
}

ModRuleset ModsMountedChoice()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_mount.known ? g_mount.choice : ModRuleset();
}

void ModsMountedChoiceSet(const ModRuleset &choice)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_mount.known && ModsSameRuleset(g_mount.profile.ruleset, choice)) g_mount.choice = choice;
}

bool ModsSameSet(std::vector<std::string> a, std::vector<std::string> b)
{
	for (std::string &s : a) s = Lower(s);
	for (std::string &s : b) s = Lower(s);
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	return a == b;
}

std::vector<std::string> ModsProblems(const ModProfile &p)
{
	return ModsProblemsFrom(p, ModsIssuesForAll(p));
}

std::vector<std::string> ModsProblemsFrom(const ModProfile &p, const std::map<std::string, ModIssues> &issues)
{
	std::vector<std::string> out;
	for (const std::string &folder : p.mods) {
		ModInfo m;
		if (!ModsFind(folder, m)) {
			out.push_back(Tr("Missing content pack \"") + folder + Tr("\" (not in the content folder)"));
			continue;
		}
		const auto found = issues.find(m.folder);
		if (found == issues.end()) continue;
		const ModIssues &is = found->second;
		std::vector<std::string> clashes;
		for (const ModOverlap &ov : is.overlaps) {
			if (!ov.problem) continue;
			clashes.push_back(ClashProblemText(ov.name));
			if (_stricmp(folder.c_str(), ov.folder.c_str()) < 0) out.push_back(TrFmt("%s and %s change the same gameplay files: use only one", {m.name, ov.name}));
		}
		for (const std::string &problem : is.problems) {
			if (std::find(clashes.begin(), clashes.end(), problem) == clashes.end()) out.push_back(m.name + ": " + problem);
		}
	}
	return out;
}

std::string ModsProfileGameplayHash(const ModProfile &p)
{
	std::vector<RulesPart> parts;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const std::string &folder : p.mods) {
		for (const ModInfo &m : g_mods) {
			if (m.folder != folder) continue;
			if (m.gameplay) parts.push_back({m.id, m.gameplayHash, ""});
			// The ruleset the profile mounts is part of what the players share, hashed apart from the package's root.
			const ModLayer *layer = ModsActiveLayer(p, m);
			if (layer && layer->gameplay) parts.push_back({m.id, layer->gameplayHash, layer->id});
		}
	}
	return RulesFingerprint(parts);
}

std::string ModsRulesLevel()
{
	return std::string(kGameId) + "/sim" + std::to_string(kSimLevel) + "/esq" + std::to_string(kRulesSchema) + "/api" + std::to_string(kScriptApiVersion);
}
