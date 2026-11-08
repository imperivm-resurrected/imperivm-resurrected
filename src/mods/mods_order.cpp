// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include <map>
#include <set>
#include "MinHook.h"
#include "i18n/tr.h"
#include "mods/features.h"
#include "mods/modclash.h"
#include "mods/modorder.h"
#include "mods/patch.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace mods_impl {
namespace {
bool SameId(const std::string &a, const std::string &b) { return !a.empty() && _stricmp(a.c_str(), b.c_str()) == 0; }

bool SameFolder(const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

bool HasFolder(const std::vector<std::string> &list, const std::string &folder)
{
	return std::find_if(list.begin(), list.end(), [&](const std::string &f) { return SameFolder(f, folder); }) != list.end();
}

int KindOf(int clashKind) { return clashKind == kClashGameplay ? kModRules : clashKind == kClashContent ? kModContent : kModCosmetic; }

std::string FilesOf(const std::vector<PatchedValue> &values)
{
	std::vector<std::string> files;
	for (const PatchedValue &v : values) {
		if (std::find(files.begin(), files.end(), v.file) == files.end()) files.push_back(v.file);
	}
	std::string out;
	for (size_t i = 0; i < files.size() && i < 2; i++) out += (i ? ", " : "") + files[i];
	if (files.size() > 2) out += " (+" + std::to_string(files.size() - 2) + ")";
	return out;
}

bool LooseIn(const ClashFile &f, const std::string &folder)
{
	for (size_t i = 0; i < f.folders.size() && i < f.loose.size(); i++) {
		if (SameFolder(f.folders[i], folder)) return f.loose[i];
	}
	return false;
}

const ModInfo *FindIn(const std::vector<ModInfo> &all, const std::string &folder)
{
	for (const ModInfo &m : all) {
		if (SameFolder(m.folder, folder)) return &m;
	}
	return nullptr;
}

std::vector<std::string> OrderOf(const std::vector<std::string> &folders, const std::vector<ModInfo> &all)
{
	std::vector<OrderItem> items;
	for (const std::string &folder : folders) {
		OrderItem item;
		item.folder = folder;
		if (const ModInfo *m = FindIn(all, folder)) {
			item.id = m->id;
			item.kind = m->kind;
			for (const ModDependency &d : m->requires) item.above.push_back(d.id);
			item.above.insert(item.above.end(), m->loadAfter.begin(), m->loadAfter.end());
			item.below = m->loadBefore;
		}
		items.push_back(item);
	}
	return CanonicalOrder(items);
}

// The ruleset a profile mounts counts as part of its package: what it shares with other packs is a clash, what it shares with its own root is not.
void AddUnique(std::vector<std::string> &into, const std::vector<std::string> &more)
{
	into.insert(into.end(), more.begin(), more.end());
	std::sort(into.begin(), into.end());
	into.erase(std::unique(into.begin(), into.end()), into.end());
}

ClashMod ClashModOf(const ModInfo &m, const ModLayer *layer)
{
	ClashMod c;
	c.folder = m.folder;
	c.id = m.id;
	c.paths = m.paths;
	c.loose = m.loosePaths;
	if (layer) {
		AddUnique(c.paths, layer->paths);
		AddUnique(c.loose, layer->loosePaths);
	}
	for (const ModDependency &d : m.requires) c.declares.push_back(d.id);
	c.declares.insert(c.declares.end(), m.loadAfter.begin(), m.loadAfter.end());
	c.declares.insert(c.declares.end(), m.loadBefore.begin(), m.loadBefore.end());
	c.values = m.patchValues;
	if (layer) c.values.insert(c.values.end(), layer->patchValues.begin(), layer->patchValues.end());
	return c;
}

void ResolvePatchedValues(std::vector<ClashMod> &mods, const ModRuleset &ruleset)
{
	std::map<std::string, int> setters;
	for (const ClashMod &m : mods) {
		std::set<std::string> files;
		for (const PatchedValue &v : m.values) {
			if (EndsWith(v.file, ".xml")) files.insert(v.file);
		}
		for (const std::string &file : files) setters[file]++;
	}
	for (const auto &kv : setters) {
		if (kv.second < 2) continue;
		std::vector<std::string> patches;
		std::vector<size_t> owners;
		for (size_t i = mods.size(); i-- > 0;) {
			std::string text;
			if (!PatchTextOf(mods[i].folder, kv.first, text, SameFolder(mods[i].folder, ruleset.pack) ? ruleset.id : std::string())) continue;
			patches.push_back(text);
			owners.push_back(i);
		}
		const std::vector<std::vector<PatchedValue>> resolved = PatchResolve(kv.first, patches);
		if (resolved.size() != owners.size()) continue;
		for (size_t k = 0; k < owners.size(); k++) {
			std::vector<PatchedValue> &values = mods[owners[k]].values;
			values.erase(std::remove_if(values.begin(), values.end(), [&](const PatchedValue &v) { return v.file == kv.first; }), values.end());
			values.insert(values.end(), resolved[k].begin(), resolved[k].end());
		}
	}
}
}

std::string ClashProblemText(const std::string &other)
{
	return TrFmt("Changes the same gameplay files as %s: use only one", {other});
}

ClashReport ClashesOf(const ModProfile &p, const std::vector<ModInfo> &all)
{
	std::vector<ClashMod> mods;
	for (const std::string &folder : OrderOf(p.mods, all)) {
		const ModInfo *m = FindIn(all, folder);
		if (m && m->forbidden.empty()) mods.push_back(ClashModOf(*m, ModsActiveLayer(p, *m)));
	}
	ResolvePatchedValues(mods, p.ruleset);
	std::vector<ClashChoice> choices;
	for (const ModChoice &c : p.choices) choices.push_back({c.use, c.over});
	return ClashAnalyze(mods, choices);
}

std::map<std::string, std::string> ChosenFolders(const ModProfile &p, const std::vector<ModInfo> &all)
{
	std::map<std::string, std::string> out;
	if (p.choices.empty()) return out;
	for (const ClashFile &f : ClashesOf(p, all).files) {
		if (f.used != f.fallback) out[f.path] = f.used;
	}
	return out;
}
}

std::vector<std::string> ModsLoadOrder(const std::vector<std::string> &folders)
{
	return OrderOf(folders, ModsList());
}

namespace {
struct ClashCache {
	const ModProfile &profile;
	const std::vector<ModInfo> &all;
	bool done = false;
	ClashReport report;

	const ClashReport &Get()
	{
		if (!done) {
			report = ClashesOf(profile, all);
			done = true;
		}
		return report;
	}
};

ModIssues IssuesOf(const ModProfile &p, const std::vector<ModInfo> &all, const std::string &folder, ClashCache &cache)
{
	ModIssues out;
	const ModInfo *m = FindIn(all, folder);
	if (!m) return out;
	const bool active = HasFolder(p.mods, folder);
	if (!m->forbidden.empty()) out.problems.push_back(Tr("Contains disallowed files (") + m->forbidden[0] + Tr("): it will not be loaded"));
	if (!m->manifestError.empty()) out.problems.push_back(Tr("mod.json has errors: ") + m->manifestError);
	if (m->noGameFiles) out.problems.push_back(Tr("This content pack is empty: it brings nothing for the game."));
	if (!m->rleTailError.empty()) out.problems.push_back(Tr(m->rleTailError));
	for (const std::string &e : m->rulesetProblems) out.problems.push_back(e);
	for (const std::string &e : PatchErrorsFor(m->folder)) {
		if (out.problems.size() >= 8) break;
		out.problems.push_back(Tr("Patch ") + e);
	}
	for (const std::string &e : ModsFeatureProblems(m->folder)) out.problems.push_back(e);
	if (!active) return out;
	const ModLayer *layer = ModsActiveLayer(p, *m);
	if (layer && !layer->rleTailError.empty()) out.problems.push_back(Tr(layer->rleTailError));
	// The tail of the ruleset in use counts as its package's.
	const bool rleTail = (m->rleTail && m->rleTailError.empty()) || (layer && layer->rleTail && layer->rleTailError.empty());
	const uint64_t rleBase = m->rleTail ? m->rleBaseSize : layer ? layer->rleBaseSize : 0;
	if (rleTail) {
		const std::string rle = RleProblemOf(m->folder);
		if (!rle.empty()) out.problems.push_back(Tr(rle));
		else if (GameRleSize() && GameRleSize() != rleBase) {
			out.problems.push_back(Tr("Made for another version of the game's graphics (rle.mmp): its new graphics cannot be shown"));
		}
	}
	std::vector<const ModInfo *> on;
	for (const std::string &f : p.mods) {
		if (const ModInfo *a = FindIn(all, f)) on.push_back(a);
	}
	for (const ModDependency &d : m->requires) {
		const ModInfo *dep = nullptr;
		for (const ModInfo *a : on) {
			if (SameId(a->id, d.id)) dep = a;
		}
		bool installed = false;
		for (const ModInfo &a : all) {
			if (SameId(a.id, d.id)) installed = true;
		}
		if (!dep) out.problems.push_back(Tr("Needs \"") + d.id + "\"" + (installed ? Tr(" (enable it)") : Tr(" (it is not installed)")));
		else if (!VersionMatches(dep->version, d.version)) out.problems.push_back(Tr("Needs ") + d.id + " " + d.version + Tr(" (you have ") + dep->version + ")");
	}
	std::vector<std::string> reported;
	for (const ModInfo *a : on) {
		if (a == m) continue;
		const size_t before = out.problems.size();
		if (ModsSameMod(*a, *m)) out.problems.push_back(Tr("Another version of this content pack is active (") + a->version + Tr("): keep only one"));
		const ModLayer *otherLayer = ModsActiveLayer(p, *a);
		if ((m->rleTail || (layer && layer->rleTail)) && (a->rleTail || (otherLayer && otherLayer->rleTail))) out.problems.push_back(TrFmt("Only one content pack can add graphics to rle.mmp, and %s also does", {a->name}));
		for (const std::string &x : m->incompatible) {
			if (SameId(x, a->id)) out.problems.push_back(Tr("Not compatible with ") + a->name);
		}
		for (const std::string &x : a->incompatible) {
			if (SameId(x, m->id)) out.problems.push_back(a->name + Tr(" is not compatible with this content pack"));
		}
		if (out.problems.size() > before) reported.push_back(a->folder);
	}
	if (!m->forbidden.empty()) return out;
	const ClashReport &clashes = cache.Get();
	for (const ClashPair &c : clashes.pairs) {
		const bool first = SameFolder(c.a, folder);
		if (!first && !SameFolder(c.b, folder)) continue;
		ModOverlap ov;
		ov.folder = first ? c.b : c.a;
		const ModInfo *o = FindIn(all, ov.folder);
		ov.name = o ? o->name : ov.folder;
		ov.files = c.files;
		ov.gameplay = c.gameplay;
		ov.declared = c.declared;
		ov.choosable = c.choosable;
		ov.problem = c.problem && !HasFolder(reported, ov.folder);
		if (ov.problem) out.problems.push_back(ClashProblemText(ov.name));
		out.overlaps.push_back(ov);
	}
	for (const ClashFile &f : clashes.files) {
		if (!HasFolder(f.folders, folder)) continue;
		out.overwrites++;
		ModOverlapFile file;
		file.path = f.path;
		file.kind = KindOf(f.kind);
		file.used = f.used;
		const ModInfo *u = FindIn(all, f.used);
		file.usedName = u ? u->name : f.used;
		file.chosen = f.used != f.fallback;
		out.files.push_back(file);
		for (ModOverlap &ov : out.overlaps) {
			if (!HasFolder(f.folders, ov.folder)) continue;
			const bool mine = SameFolder(f.used, folder), theirs = SameFolder(f.used, ov.folder);
			if (mine) ov.mine++;
			else if (theirs) ov.theirs++;
			if (!ov.choosable) continue;
			if (theirs && LooseIn(f, folder)) ov.canUseMine = true;
			if (mine && LooseIn(f, ov.folder)) ov.canUseTheirs = true;
			if (file.chosen && (mine || theirs)) ov.chosen++;
		}
	}
	for (const ValueClash &c : clashes.values) {
		const bool mine = SameFolder(c.used, folder);
		if (!mine && !SameFolder(c.lost, folder)) continue;
		const std::string other = mine ? c.lost : c.used;
		if (HasFolder(reported, other)) continue;
		const ModInfo *o = FindIn(all, other);
		const std::string name = o ? o->name : other;
		const std::string files = FilesOf(c.values);
		if (mine) out.notices.push_back(TrFmt("Changes the same values as %s in %s: the ones from this pack are used", {name, files}));
		else out.notices.push_back(TrFmt("Changes the same values as %s in %s: the ones from %s are used", {name, files, name}));
		for (const PatchedValue &v : c.values) {
			const auto same = [&](const ModOverlapValue &x) { return x.file == v.file && x.label == v.label; };
			if (std::find_if(out.values.begin(), out.values.end(), same) != out.values.end()) continue;
			ModOverlapValue value;
			value.file = v.file;
			value.label = v.label;
			value.kind = KindOf(ClashKindOf(v.file));
			value.usedName = mine ? m->name : name;
			out.values.push_back(value);
		}
	}
	return out;
}
}

ModIssues ModsIssuesFor(const ModProfile &p, const std::string &folder)
{
	const std::vector<ModInfo> all = ModsList();
	ClashCache cache{p, all};
	return IssuesOf(p, all, folder, cache);
}

std::map<std::string, ModIssues> ModsIssuesForAll(const ModProfile &p)
{
	const std::vector<ModInfo> all = ModsList();
	ClashCache cache{p, all};
	std::map<std::string, ModIssues> out;
	for (const ModInfo &m : all) out[m.folder] = IssuesOf(p, all, m.folder, cache);
	return out;
}

void ModsReadPatchedFiles()
{
	std::map<std::string, int> packs;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		for (const auto &kv : g_scanFiles) {
			for (const ScanFile &f : kv.second) {
				if (StartsWith(f.rel, "patches/") && EndsWith(f.rel, ".xml")) packs[f.rel.substr(8)]++;
			}
		}
	}
	std::vector<std::string> rels;
	for (const auto &kv : packs) {
		if (kv.second >= 2) rels.push_back(kv.first);
	}
	if (!rels.empty()) PatchReadBases(rels);
}

void ModsChooseFiles(ModProfile &p, const std::string &useFolder, const std::string &overFolder)
{
	ModInfo use, over;
	if (!ModsFind(useFolder, use) || !ModsFind(overFolder, over) || SameId(use.id, over.id)) return;
	const auto samePair = [&](const ModChoice &c) {
		return (SameId(c.use, use.id) && SameId(c.over, over.id)) || (SameId(c.use, over.id) && SameId(c.over, use.id));
	};
	p.choices.erase(std::remove_if(p.choices.begin(), p.choices.end(), samePair), p.choices.end());
	p.choices.push_back({use.id, over.id});
}
