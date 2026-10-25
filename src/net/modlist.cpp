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

}

std::vector<OnlineMod> DecodeModList(const std::string &text)
{
	std::vector<OnlineMod> out;
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
			out.push_back(m);
		}
	}
	return out;
}

std::string EncodeModList(const std::vector<ModInfo> &mounted, const std::string &scenModId, int &optional)
{
	std::set<std::string> need;
	for (const ModInfo &m : mounted) {
		if (m.kind == kModRules || m.gameplay || (!scenModId.empty() && LowerId(m.id) == LowerId(scenModId))) need.insert(LowerId(m.id));
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
		out += m.id + "\t" + m.name + "\t" + m.version + "\t" + (m.gameplay ? "1" : "0") + "\t" + std::to_string(m.totalBytes) + "\t" +
			(m.redistributable && m.forbidden.empty() ? "1" : "0") + "\t" + std::to_string(m.gbId) + "\t" + std::to_string(m.kind) + "\t" + std::to_string(m.gbFile) + "\t" +
			ModsRoomCopyKey(m.gameplayHash) + "\n";
	}
	return out;
}
