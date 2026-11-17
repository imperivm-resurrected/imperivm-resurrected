// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/profilecode.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

namespace {

const size_t kMaxProfileName = 60;

bool SameText(const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

bool HasText(const std::vector<std::string> &list, const std::string &s)
{
	return std::find_if(list.begin(), list.end(), [&](const std::string &x) { return SameText(x, s); }) != list.end();
}

std::string CleanProfileName(std::string name)
{
	for (char &c : name) {
		if (static_cast<unsigned char>(c) < 0x20 || strchr("\\/:*?\"<>|", c)) c = ' ';
	}
	name.erase(0, name.find_first_not_of(' '));
	name.erase(name.find_last_not_of(' ') + 1);
	if (name.size() > kMaxProfileName) {
		size_t cut = kMaxProfileName;
		while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xc0) == 0x80) cut--;
		name.resize(cut);
		name.erase(name.find_last_not_of(' ') + 1);
	}
	return name;
}

bool ProfileNameTaken(const std::string &name, const std::vector<std::string> &names)
{
	if (ModsIsReservedProfileName(name) || HasText(names, name)) return true;
	return GetFileAttributesA((ProfilesDir() + "\\" + SafeFileName(name) + ".json").c_str()) != INVALID_FILE_ATTRIBUTES;
}

const ModInfo *BestCopy(const std::vector<ModInfo> &all, const ModsSharedMod &want)
{
	const ModInfo *best = nullptr;
	bool bestSame = false;
	for (const ModInfo &m : all) {
		if (!ModsIsMod(m, want.id, want.gbId)) continue;
		const bool same = !want.version.empty() && ModsCompareVersions(m.version, want.version) == 0;
		if (!best || (same && !bestSame) || (same == bestSame && ModsCompareVersions(m.version, best->version) > 0)) {
			best = &m;
			bestSame = same;
		}
	}
	return best;
}

}

std::string ModsProfileCode(const ModProfile &p)
{
	const std::vector<ModInfo> all = ModsSummaries();
	ModsSharedProfile sp;
	sp.name = ModsProfileLabel(p.name);
	std::vector<std::string> ids;
	for (const std::string &folder : ModsLoadOrder(p.mods)) {
		const auto m = std::find_if(all.begin(), all.end(), [&](const ModInfo &x) { return SameText(x.folder, folder); });
		if (m == all.end() || m->id.empty()) continue;
		ModsSharedMod sm;
		sm.id = m->id;
		sm.name = m->name;
		sm.version = m->version;
		sm.gbId = m->gbId;
		sp.mods.push_back(sm);
		ids.push_back(m->id);
	}
	for (const ModChoice &c : p.choices) {
		if (HasText(ids, c.use) && HasText(ids, c.over)) sp.choices.push_back(c);
	}
	for (const ModFileChoice &f : p.files) {
		if (HasText(ids, f.use)) sp.files.push_back(f);
	}
	return EncodeProfileCode(sp);
}

bool ModsParseProfileCode(const std::string &code, ModsSharedProfile &out, std::string &error)
{
	error.clear();
	int problem = kProfileCodeOk;
	if (!DecodeProfileCode(code, out, &problem)) {
		if (problem == kProfileCodeNewer) error = Tr("This content profile code needs a newer Imperivm Resurrected: update it first");
		else if (problem == kProfileCodeDamaged) error = Tr("This content profile code is damaged or incomplete: copy it again");
		else error = Tr("This is not a content profile code");
		return false;
	}
	const std::vector<ModInfo> all = ModsSummaries();
	for (ModsSharedMod &sm : out.mods) {
		const ModInfo *m = BestCopy(all, sm);
		sm.installed = m != nullptr;
		sm.folder = m ? m->folder : "";
	}
	return true;
}

std::string ModsImportProfile(const ModsSharedProfile &sp)
{
	std::string base = CleanProfileName(sp.name);
	if (base.empty()) base = Tr("Shared profile");
	const std::vector<std::string> names = ModsProfileNames();
	std::string name = base;
	for (int n = 2; ProfileNameTaken(name, names); n++) {
		if (n > 999) return "";
		name = base + " " + std::to_string(n);
	}
	ModProfile p;
	p.name = name;
	for (const ModsSharedMod &sm : sp.mods) {
		ModInfo m;
		if (sm.installed && ModsFind(sm.folder, m) && !HasText(p.mods, m.folder)) p.mods.push_back(m.folder);
	}
	for (const ModChoice &c : sp.choices) {
		if (!c.use.empty() && !c.over.empty()) p.choices.push_back(c);
	}
	for (const ModFileChoice &f : sp.files) {
		const std::string rel = NormPath(f.path);
		if (!rel.empty() && !f.use.empty()) p.files.push_back({rel, f.use});
	}
	if (!ModsSaveProfile(p)) {
		LogE("content", "could not save the shared profile \"%s\"", name.c_str());
		return "";
	}
	LogI("content", "shared profile saved as \"%s\": %u of %u packs installed", name.c_str(), static_cast<unsigned>(p.mods.size()),
		static_cast<unsigned>(sp.mods.size()));
	return name;
}

bool ModsAddToProfile(const std::string &profile, const std::string &folder)
{
	ModInfo m;
	if (profile.empty() || !ModsFind(folder, m)) return false;
	ModProfile p = ModsLoadProfile(profile);
	if (SameText(p.name, kOriginal)) return false;
	if (HasText(p.mods, m.folder)) return true;
	p.mods.push_back(m.folder);
	if (!ModsSaveProfile(p)) return false;
	LogI("content", "%s added to the profile \"%s\"", m.folder.c_str(), p.name.c_str());
	return true;
}
