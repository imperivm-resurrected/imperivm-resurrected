// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "i18n/tr.h"
#include "core/legacy.h"
#include "core/guard.h"
#include "mods/modvars.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

const char *ModsKindId(int kind)
{
	return kind == kModRules ? "rules" : kind == kModContent ? "content" : "cosmetic";
}

int ModsKindFromId(const std::string &id)
{
	const std::string s = Lower(id);
	if (s == "rules" || s == "gameplay") return kModRules;
	if (s == "content" || s == "maps") return kModContent;
	if (s == "cosmetic") return kModCosmetic;
	switch (LegacyModKind(s)) {
	case 0:
		return kModRules;
	case 1:
		return kModContent;
	case 2:
		return kModCosmetic;
	}
	return -1;
}

int ModsRulesSchema() { return kRulesSchema; }

static void ModsScanUnguarded()
{
	std::vector<ModInfo> mods;
	std::map<std::string, std::vector<ScanFile>> files;
	CreateDirectoryA(ModsDir().c_str(), nullptr);
	CreateDirectoryA(ProfilesDir().c_str(), nullptr);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((ModsDir() + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const std::string name = fd.cFileName;
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name[0] == '.' || name[0] == '_') {
				continue;
			}
			std::vector<ScanFile> f;
			mods.push_back(ScanMod(name, f));
			mods.back().installedTime = static_cast<uint64_t>(fd.ftCreationTime.dwHighDateTime) << 32 | fd.ftCreationTime.dwLowDateTime;
			files[name] = std::move(f);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(mods.begin(), mods.end(), [](const ModInfo &a, const ModInfo &b) { return Lower(a.name) < Lower(b.name); });
	ModsNoteScan(mods);
	{
		std::lock_guard<std::mutex> lock(g_lock);
		for (ModInfo &m : mods) {
			for (const ModInfo &old : g_mods) {
				if (old.folder == m.folder && old.totalBytes == m.totalBytes && old.fileCount == m.fileCount) {
					m.hash = old.hash;
					m.gameplayHash = old.gameplayHash;
					for (ModLayer &l : m.layers) {
						if (const ModLayer *was = ModsFindLayer(old, l.id)) l.gameplayHash = was->gameplayHash;
					}
				}
			}
		}
		g_mods = std::move(mods);
		g_scanFiles = std::move(files);
	}
	ModsBumpSerial();
	LogI("content", "%u mods in the mods folder", static_cast<unsigned>(g_mods.size()));
	for (const ModInfo &m : g_mods) LogI("content", "  %s -> %s", m.folder.c_str(), ModsKindId(m.kind));
	StartHashing();
}

void ModsScan()
{
	Guard("content scan", [] { ModsScanUnguarded(); });
}

std::vector<ModInfo> ModsList()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_mods;
}

std::vector<ModInfo> ModsSummaries()
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::vector<ModInfo> out;
	out.reserve(g_mods.size());
	for (ModInfo &m : g_mods) {
		std::vector<std::string> paths = std::move(m.paths), loose = std::move(m.loosePaths), hidden = std::move(m.hidden);
		std::vector<PatchedValue> values = std::move(m.patchValues);
		std::vector<ModLayer> layers = std::move(m.layers);
		out.push_back(m);
		m.paths = std::move(paths);
		m.loosePaths = std::move(loose);
		m.hidden = std::move(hidden);
		m.patchValues = std::move(values);
		for (ModLayer &l : layers) {
			std::vector<std::string> lp = std::move(l.paths), ll = std::move(l.loosePaths), lh = std::move(l.hidden);
			std::vector<PatchedValue> lv = std::move(l.patchValues);
			out.back().layers.push_back(l);
			l.paths = std::move(lp);
			l.loosePaths = std::move(ll);
			l.hidden = std::move(lh);
			l.patchValues = std::move(lv);
		}
		m.layers = std::move(layers);
	}
	return out;
}

bool ModsFind(const std::string &folder, ModInfo &out)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModInfo &m : g_mods) {
		if (_stricmp(m.folder.c_str(), folder.c_str()) == 0) {
			out = m;
			return true;
		}
	}
	return false;
}

std::vector<std::string> ModsProfileNames()
{
	std::vector<std::string> names;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((ProfilesDir() + "\\*.json").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			ModProfile p = ReadProfileFile(ProfilesDir() + "\\" + fd.cFileName);
			if (!p.name.empty() && _stricmp(p.name.c_str(), kOriginal) != 0 && _stricmp(p.name.c_str(), kNoProfile) != 0) {
				names.push_back(p.name);
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) { return Lower(a) < Lower(b); });
	names.insert(names.begin(), kNoProfile);
	names.insert(names.begin(), kOriginal);
	return names;
}

ModProfile ModsLoadProfile(const std::string &name)
{
	if (name.empty() || _stricmp(name.c_str(), kOriginal) == 0) {
		ModProfile p;
		p.name = kOriginal;
		return p;
	}
	ModProfile p = ReadProfileFile(ProfilesDir() + "\\" + SafeFileName(name) + ".json");
	if (p.name.empty() && _stricmp(name.c_str(), kNoProfile) == 0) {
		p.name = kNoProfile;
		return p;
	}
	if (p.name.empty()) {
		p.name = kOriginal;
		p.mods.clear();
	}
	return p;
}

bool ModsSaveProfile(const ModProfile &p)
{
	if (p.name.empty() || _stricmp(p.name.c_str(), kOriginal) == 0) {
		return false;
	}
	Json j = Json::MakeObject();
	j.Set("name", Json::MakeString(p.name));
	Json mods = Json::MakeArray();
	for (const std::string &m : ModsLoadOrder(p.mods)) {
		mods.arr.push_back(Json::MakeString(m));
	}
	j.Set("mods", mods);
	Json choices = Json::MakeArray();
	for (const ModChoice &c : p.choices) {
		Json o = Json::MakeObject();
		o.Set("use", Json::MakeString(c.use));
		o.Set("over", Json::MakeString(c.over));
		choices.arr.push_back(o);
	}
	if (!choices.arr.empty()) j.Set("choices", choices);
	if (!p.ruleset.Empty()) {
		Json r = Json::MakeObject();
		r.Set("pack", Json::MakeString(p.ruleset.pack));
		r.Set("id", Json::MakeString(p.ruleset.id));
		j.Set("ruleset", r);
	}
	CreateDirectoryA(ProfilesDir().c_str(), nullptr);
	return WriteJsonFile(ProfilesDir() + "\\" + SafeFileName(p.name) + ".json", j);
}

bool ModsDeleteProfile(const std::string &name)
{
	if (name.empty() || _stricmp(name.c_str(), kOriginal) == 0 || _stricmp(name.c_str(), kNoProfile) == 0) {
		return false;
	}
	return DeleteFileA((ProfilesDir() + "\\" + SafeFileName(name) + ".json").c_str()) != 0;
}

bool ModsRenameProfile(const std::string &from, const std::string &to)
{
	if (to.empty() || ModsIsReservedProfileName(to)) return false;
	if (_stricmp(from.c_str(), kOriginal) == 0 || _stricmp(from.c_str(), kNoProfile) == 0) return false;
	const bool caseOnly = _stricmp(from.c_str(), to.c_str()) == 0;
	if (!caseOnly) {
		for (const std::string &n : ModsProfileNames()) {
			if (_stricmp(n.c_str(), to.c_str()) == 0) return false;
		}
	}
	ModProfile p = ModsLoadProfile(from);
	p.name = to;
	if (!ModsSaveProfile(p)) return false;
	if (!caseOnly) ModsDeleteProfile(from);
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_mount.known && g_mount.profile.name == from) g_mount.profile.name = to;
	}
	if (ModsActiveProfileName() == from) ModsSetActiveProfileName(to);
	return true;
}

std::string ModsProfileLabel(const std::string &name)
{
	if (_stricmp(name.c_str(), kNoProfile) == 0) return Tr("My content");
	if (_stricmp(name.c_str(), kOriginal) == 0) return Tr("Original");
	return name;
}

bool ModsIsReservedProfileName(const std::string &name)
{
	for (const std::string &r : {std::string(kOriginal), std::string(kNoProfile), std::string("My content"), Tr("Original"), Tr("My content")}) {
		if (_stricmp(name.c_str(), r.c_str()) == 0) return true;
	}
	return false;
}

std::vector<std::string> ModsProfilesUsing(const std::string &folder)
{
	std::vector<std::string> out;
	for (const std::string &n : ModsProfileNames()) {
		const ModProfile p = ModsLoadProfile(n);
		for (const std::string &m : p.mods) {
			if (_stricmp(m.c_str(), folder.c_str()) == 0) {
				out.push_back(n);
				break;
			}
		}
	}
	return out;
}
