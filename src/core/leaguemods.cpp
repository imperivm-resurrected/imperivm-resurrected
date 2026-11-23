// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/leaguemods.h"

#include <cstdlib>

namespace {

uint64_t IdOf(const Json &j, const char *key)
{
	const std::string text = j.GetIdText(key);
	return text.empty() ? 0 : _strtoui64(text.c_str(), nullptr, 10);
}

bool SameFile(const LeagueModRef &a, const LeagueModRef &b)
{
	return a.gbId && a.fileId && a.gbId == b.gbId && a.fileId == b.fileId;
}

}

LeagueModsRule LeagueParseModsRule(const Json *v)
{
	LeagueModsRule r;
	if (!v || v->type != Json::Array) return r;
	r.set = true;
	for (const Json &m : v->arr) {
		if (m.type != Json::Object) continue;
		LeagueModRef ref;
		ref.gbId = IdOf(m, "gbId");
		ref.fileId = IdOf(m, "fileId");
		if (!ref.gbId || !ref.fileId) continue;
		ref.name = m.GetString("name");
		ref.file = m.GetString("file");
		ref.version = m.GetString("version");
		r.mods.push_back(ref);
	}
	return r;
}

Json LeagueModsOwnJson(const std::vector<LeagueModRef> &own)
{
	Json list = Json::MakeArray();
	for (const LeagueModRef &m : own) {
		const bool known = m.gbId && m.fileId;
		Json o = Json::MakeObject();
		o.Set("gbId", Json::MakeNumber(known ? static_cast<double>(m.gbId) : 0));
		o.Set("fileId", Json::MakeNumber(known ? static_cast<double>(m.fileId) : 0));
		list.arr.push_back(o);
	}
	return list;
}

std::string LeagueModLabel(const LeagueModRef &m)
{
	std::string label = m.name.empty() ? "GameBanana #" + std::to_string(m.gbId) : m.name;
	if (!m.version.empty() && label.find(m.version) == std::string::npos) label += " " + m.version;
	return label;
}

std::string LeagueModsNames(const std::vector<LeagueModRef> &mods)
{
	std::string out;
	for (const LeagueModRef &m : mods) out += (out.empty() ? "" : ", ") + LeagueModLabel(m);
	return out;
}

LeagueModsCheck LeagueModsCompare(const LeagueModsRule &rule, const std::vector<LeagueModRef> &own)
{
	LeagueModsCheck c;
	if (!rule.set) return c;
	if (rule.mods.empty()) {
		if (!own.empty()) c.fit = LeagueModsFit::Extra;
		return c;
	}
	for (const LeagueModRef &need : rule.mods) {
		bool have = false;
		for (const LeagueModRef &o : own) have = have || SameFile(need, o);
		if (!have) c.missing.push_back(need);
	}
	if (!c.missing.empty()) {
		c.fit = LeagueModsFit::Missing;
		return c;
	}
	bool unknown = false, extra = false;
	for (const LeagueModRef &o : own) {
		if (!o.gbId || !o.fileId) {
			unknown = true;
			continue;
		}
		bool asked = false;
		for (const LeagueModRef &need : rule.mods) asked = asked || SameFile(need, o);
		if (!asked) extra = true;
	}
	if (unknown) c.fit = LeagueModsFit::NotFromGameBanana;
	else if (extra) c.fit = LeagueModsFit::Extra;
	return c;
}
