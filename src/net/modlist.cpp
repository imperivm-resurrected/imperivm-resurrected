// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "net/modlist.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace {

std::string LowerId(std::string id)
{
	std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
	return id;
}

std::string OneLine(std::string s)
{
	std::replace_if(s.begin(), s.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
	return s;
}

}

std::vector<OnlineMod> DecodeModList(const std::string &text, std::string *rulesetPackage)
{
	std::vector<OnlineMod> out;
	if (rulesetPackage) rulesetPackage->clear();
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		start = end == std::string::npos ? text.size() : end + 1;
		std::vector<std::string> f;
		size_t p = 0;
		while (true) {
			size_t t = line.find('\t', p);
			f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
			if (t == std::string::npos) break;
			p = t + 1;
		}
		if (f.size() == 2 && f[0] == "@package") {
			if (rulesetPackage) *rulesetPackage = f[1];
			continue;
		}
		if (f.size() >= 5) {
			OnlineMod m;
			m.id = f[0];
			m.name = f[1];
			m.version = f[2];
			m.gameplay = f[3] == "1";
			m.bytes = _strtoui64(f[4].c_str(), nullptr, 10);
			m.redistributable = f.size() >= 6 && f[5] == "1";
			m.gbId = f.size() >= 7 ? _strtoui64(f[6].c_str(), nullptr, 10) : 0;
			m.kind = f.size() >= 8 ? atoi(f[7].c_str()) : (m.gameplay ? 2 : 0);
			m.gameplay = m.kind == 2;
			m.gbFile = f.size() >= 9 ? _strtoui64(f[8].c_str(), nullptr, 10) : 0;
			if (f.size() >= 10) m.copyKey = f[9];
			if (f.size() >= 11) m.layer = f[10];
			if (f.size() >= 12) m.layerKey = f[11];
			if (f.size() >= 13) m.layerName = f[12];
			out.push_back(m);
		}
	}
	return out;
}

std::string EncodeModList(const std::vector<ModInfo> &mounted, const std::string &scenModId, int &optional, const ModRuleset &ruleset)
{
	ModProfile profile;
	profile.ruleset = ruleset;
	std::set<std::string> need;
	for (const ModInfo &m : mounted) {
		if (m.kind == kModRules || m.gameplay || (!scenModId.empty() && LowerId(m.id) == LowerId(scenModId)) || ModsActiveLayer(profile, m)) need.insert(LowerId(m.id));
	}
	for (bool grew = true; grew;) {
		grew = false;
		for (const ModInfo &m : mounted) {
			if (!need.count(LowerId(m.id))) continue;
			for (const ModDependency &d : m.requires) grew = need.insert(LowerId(d.id)).second || grew;
		}
	}
	std::string out;
	optional = 0;
	for (const ModInfo &m : mounted) {
		if (!need.count(LowerId(m.id))) {
			optional++;
			continue;
		}
		// A package whose rules are in the ruleset the room mounts is rules content for the room, even if its root is not.
		const ModLayer *layer = ModsActiveLayer(profile, m);
		const int kind = layer && layer->gameplay ? kModRules : m.kind;
		out += m.id + "\t" + m.name + "\t" + m.version + "\t" + (m.gameplay ? "1" : "0") + "\t" + std::to_string(m.totalBytes) + "\t" +
			(m.redistributable && m.forbidden.empty() ? "1" : "0") + "\t" + std::to_string(m.gbId) + "\t" + std::to_string(kind) + "\t" + std::to_string(m.gbFile) + "\t" +
			ModsRoomCopyKey(m.gameplayHash) + "\t" + (layer ? layer->id : "") + "\t" + (layer && layer->gameplay ? ModsRoomCopyKey(layer->gameplayHash) : "") + "\t" +
			(layer ? OneLine(layer->name) : "") + "\n";
	}
	// The room's ruleset is a package that has no ruleset of its own: say which, so the guests mount its root files that replace the game's too.
	if (!ruleset.pack.empty() && ruleset.id.empty()) {
		for (const ModInfo &m : mounted) {
			if (_stricmp(m.folder.c_str(), ruleset.pack.c_str()) == 0 && !m.globalLoose.empty() && need.count(LowerId(m.id))) out += "@package\t" + m.id + "\n";
		}
	}
	return out;
}
