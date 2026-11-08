// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/modclash.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "mods/modrules.h"

namespace {

struct Owner {
	size_t mod;
	bool loose;
};

struct Shared {
	std::string path;
	int kind = kClashCosmetic;
	std::vector<Owner> owners;
	size_t fallback = 0;
};

std::string Folded(const std::string &s)
{
	std::string out = s;
	for (char &c : out) {
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
	}
	return out;
}

bool Names(const std::vector<std::string> &list, const std::string &id)
{
	if (id.empty()) return false;
	const std::string want = Folded(id);
	for (const std::string &x : list) {
		if (Folded(x) == want) return true;
	}
	return false;
}

bool Declared(const ClashMod &a, const ClashMod &b)
{
	return Names(a.declares, b.id) || Names(b.declares, a.id);
}

std::pair<size_t, size_t> Key(size_t a, size_t b)
{
	return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

std::vector<size_t> ModsWithId(const std::vector<ClashMod> &ordered, const std::string &id)
{
	std::vector<size_t> out;
	if (id.empty()) return out;
	const std::string want = Folded(id);
	for (size_t i = 0; i < ordered.size(); i++) {
		if (Folded(ordered[i].id) == want) out.push_back(i);
	}
	return out;
}

bool SameValue(const PatchedValue &a, const PatchedValue &b)
{
	return a.removed == b.removed && (a.removed || a.value == b.value);
}

std::vector<ValueClash> ValueClashes(const std::vector<ClashMod> &ordered)
{
	typedef std::pair<std::string, std::string> ValueKey;
	std::map<ValueKey, std::vector<std::pair<size_t, const PatchedValue *>>> setters;
	for (size_t i = 0; i < ordered.size(); i++) {
		std::map<ValueKey, const PatchedValue *> last;
		for (const PatchedValue &v : ordered[i].values) last[{v.file, v.key}] = &v;
		for (const auto &kv : last) setters[kv.first].push_back({i, kv.second});
	}
	std::map<std::pair<size_t, size_t>, ValueClash> lost;
	for (const auto &kv : setters) {
		const size_t used = kv.second[0].first;
		const PatchedValue &kept = *kv.second[0].second;
		for (size_t k = 1; k < kv.second.size(); k++) {
			const size_t other = kv.second[k].first;
			if (SameValue(kept, *kv.second[k].second) || Declared(ordered[used], ordered[other])) continue;
			lost[{used, other}].values.push_back(kept);
		}
	}
	std::vector<ValueClash> out;
	for (auto &kv : lost) {
		kv.second.used = ordered[kv.first.first].folder;
		kv.second.lost = ordered[kv.first.second].folder;
		out.push_back(std::move(kv.second));
	}
	return out;
}

}

int ClashKindOf(const std::string &path)
{
	if (mods_impl::IsGameplayPath(path)) return kClashGameplay;
	if (mods_impl::IsContentPath(path)) return kClashContent;
	return kClashCosmetic;
}

std::vector<RulesetShared> RulesetCompare(const std::vector<RulesetClashMod> &rulesets)
{
	std::vector<ClashMod> mods;
	for (const RulesetClashMod &r : rulesets) {
		ClashMod m;
		m.folder = r.pack + "\n" + r.layer;
		m.paths = r.paths;
		m.loose = r.loose;
		m.values = r.values;
		mods.push_back(std::move(m));
	}
	const ClashReport report = ClashAnalyze(mods, {});
	auto indexOf = [&](const std::string &folder) {
		for (size_t i = 0; i < mods.size(); i++) {
			if (mods[i].folder == folder) return i;
		}
		return mods.size();
	};
	std::map<std::pair<size_t, size_t>, RulesetShared> found;
	auto entry = [&](const std::string &x, const std::string &y) -> RulesetShared * {
		const size_t a = indexOf(x), b = indexOf(y);
		if (a >= mods.size() || b >= mods.size() || rulesets[a].pack == rulesets[b].pack) return nullptr;
		RulesetShared &s = found[Key(a, b)];
		const size_t lo = std::min(a, b), hi = std::max(a, b);
		s.packA = rulesets[lo].pack;
		s.layerA = rulesets[lo].layer;
		s.packB = rulesets[hi].pack;
		s.layerB = rulesets[hi].layer;
		return &s;
	};
	for (const ClashPair &p : report.pairs) {
		if (p.gameplay == 0) continue;
		if (RulesetShared *s = entry(p.a, p.b)) s->files = p.gameplay;
	}
	for (const ValueClash &v : report.values) {
		if (RulesetShared *s = entry(v.used, v.lost)) s->values += static_cast<int>(v.values.size());
	}
	std::vector<RulesetShared> out;
	for (auto &kv : found) {
		if (kv.second.files || kv.second.values) out.push_back(kv.second);
	}
	return out;
}

ClashReport ClashAnalyze(const std::vector<ClashMod> &ordered, const std::vector<ClashChoice> &choices)
{
	const size_t n = ordered.size();
	std::map<std::string, std::vector<Owner>> owners;
	for (size_t i = 0; i < n; i++) {
		std::vector<std::string> loose = ordered[i].loose;
		std::sort(loose.begin(), loose.end());
		std::vector<std::string> paths = ordered[i].paths;
		std::sort(paths.begin(), paths.end());
		paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
		for (const std::string &p : paths) owners[p].push_back({i, std::binary_search(loose.begin(), loose.end(), p)});
	}
	std::vector<Shared> shared;
	std::map<std::pair<size_t, size_t>, ClashPair> pairs;
	for (auto &o : owners) {
		if (o.second.size() < 2) continue;
		Shared s;
		s.path = o.first;
		s.kind = ClashKindOf(o.first);
		s.owners = std::move(o.second);
		s.fallback = s.owners[0].mod;
		for (const Owner &w : s.owners) {
			if (w.loose) {
				s.fallback = w.mod;
				break;
			}
		}
		for (size_t x = 0; x < s.owners.size(); x++) {
			for (size_t y = x + 1; y < s.owners.size(); y++) {
				ClashPair &pr = pairs[Key(s.owners[x].mod, s.owners[y].mod)];
				pr.files++;
				if (s.kind == kClashGameplay) pr.gameplay++;
				if (s.kind == kClashContent) pr.content++;
			}
		}
		if (s.kind == kClashGameplay) {
			for (const Owner &l : s.owners) {
				if (l.mod != s.fallback && !Declared(ordered[s.fallback], ordered[l.mod])) pairs[Key(s.fallback, l.mod)].problem = true;
			}
		}
		shared.push_back(std::move(s));
	}
	ClashReport out;
	for (auto &kv : pairs) {
		ClashPair &pr = kv.second;
		pr.a = ordered[kv.first.first].folder;
		pr.b = ordered[kv.first.second].folder;
		pr.declared = Declared(ordered[kv.first.first], ordered[kv.first.second]);
		pr.choosable = pr.gameplay == 0 && pr.content == 0;
		out.pairs.push_back(pr);
	}
	std::set<std::pair<size_t, size_t>> allowed;
	for (const ClashChoice &c : choices) {
		for (size_t u : ModsWithId(ordered, c.use)) {
			for (size_t v : ModsWithId(ordered, c.over)) {
				const auto it = pairs.find(Key(u, v));
				if (u != v && it != pairs.end() && it->second.choosable) allowed.insert({u, v});
			}
		}
	}
	for (const Shared &s : shared) {
		size_t used = s.fallback;
		if (s.kind == kClashCosmetic) {
			for (size_t step = 0; step < s.owners.size(); step++) {
				size_t next = used;
				for (const Owner &w : s.owners) {
					if (w.mod != used && w.loose && allowed.count({w.mod, used})) {
						next = w.mod;
						break;
					}
				}
				if (next == used) break;
				used = next;
			}
		}
		ClashFile f;
		f.path = s.path;
		f.kind = s.kind;
		for (const Owner &w : s.owners) {
			f.folders.push_back(ordered[w.mod].folder);
			f.loose.push_back(w.loose);
		}
		f.fallback = ordered[s.fallback].folder;
		f.used = ordered[used].folder;
		out.files.push_back(std::move(f));
	}
	out.values = ValueClashes(ordered);
	return out;
}
