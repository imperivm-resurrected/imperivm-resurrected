// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/unitcatalog.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "core/strutil.h"

namespace {

using strutil::Lower;

bool Usable(const std::string &s)
{
	return !s.empty() && s != "**Invalid**";
}

}

bool EditTreeUnit(const std::string &path, EditTreeSpot &out)
{
	std::vector<std::string> parts;
	size_t start = 0;
	for (;;) {
		const size_t slash = path.find('/', start);
		const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (part.empty()) return false;
		parts.push_back(part);
		if (slash == std::string::npos) break;
		start = slash + 1;
	}
	if (parts.size() < 3 || Lower(parts[0]) != "units") return false;
	out.folder = parts[1];
	out.leaf = parts.back();
	out.leafContext = "editortree";
	for (size_t i = 0; i + 1 < parts.size(); i++) out.leafContext += "/" + parts[i];
	return true;
}

UnitCatalog BuildUnitCatalog(const std::vector<CatalogClass> &classes)
{
	struct Group {
		std::string name;
		int order;
	};
	std::vector<Group> groups;
	std::map<std::string, size_t> groupIndex;
	std::vector<CatalogUnit> units;
	std::vector<std::string> leaves;
	for (const CatalogClass &c : classes) {
		if (c.id.empty() || c.groupName.empty()) continue;
		const std::string key = Lower(c.groupName);
		auto it = groupIndex.find(key);
		if (it == groupIndex.end()) {
			it = groupIndex.emplace(key, groups.size()).first;
			groups.push_back({c.groupName, c.groupOrder});
		}
		else {
			groups[it->second].order = std::min(groups[it->second].order, c.groupOrder);
		}
		const std::string leaf = Usable(c.leafName) ? c.leafName : c.id;
		units.push_back({c.id, Usable(c.name) ? c.name : leaf, static_cast<int>(it->second), c.hero});
		leaves.push_back(leaf);
	}
	for (int pass = 0; pass < 2; pass++) {
		std::map<std::pair<int, std::string>, int> count;
		for (const CatalogUnit &u : units) count[{u.group, Lower(u.name)}]++;
		for (size_t i = 0; i < units.size(); i++) {
			if (count[{units[i].group, Lower(units[i].name)}] < 2) continue;
			units[i].name = pass == 0 ? leaves[i] : units[i].name + " (" + units[i].id + ")";
		}
	}
	std::vector<size_t> order(groups.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = i;
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		if (groups[a].order != groups[b].order) return groups[a].order < groups[b].order;
		return Lower(groups[a].name) < Lower(groups[b].name);
	});
	std::vector<int> rank(groups.size());
	UnitCatalog out;
	for (size_t i = 0; i < order.size(); i++) {
		rank[order[i]] = static_cast<int>(i);
		out.groups.push_back(groups[order[i]].name);
	}
	for (CatalogUnit &u : units) u.group = rank[u.group];
	std::sort(units.begin(), units.end(), [](const CatalogUnit &a, const CatalogUnit &b) {
		if (a.group != b.group) return a.group < b.group;
		if (a.hero != b.hero) return !a.hero;
		const std::string la = Lower(a.name), lb = Lower(b.name);
		return la != lb ? la < lb : a.id < b.id;
	});
	out.units = std::move(units);
	return out;
}
