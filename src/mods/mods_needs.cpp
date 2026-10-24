// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <algorithm>
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/legacy.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "core/sha256.h"
#include "core/pathsafe.h"
#include "mods/modvars.h"
#include "mods/civindices.h"
#include "addr/engine.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

bool ModNeeds::Satisfied() const
{
	for (const ModNeed &m : mods) {
		if (!m.inProfile) return false;
	}
	return true;
}

bool ModNeeds::AllInstalled() const
{
	for (const ModNeed &m : mods) {
		if (!m.installed) return false;
	}
	return true;
}

std::vector<std::string> ModNeeds::Names(bool onlyMissing) const
{
	std::vector<std::string> out;
	for (const ModNeed &m : mods) {
		if (onlyMissing && m.inProfile) continue;
		out.push_back((m.name.empty() ? m.folder : m.name) + (m.version.empty() ? "" : " " + m.version));
	}
	return out;
}

ModNeeds ModsNeededFor(const std::string &gamePath, const std::string &profile)
{
	ModNeeds out;
	Json j;
	if (!ReadJsonFile(GameDir() + "\\" + gamePath + ".mods.json", j)) return out;
	out.profile = LegacyProfileName(j.GetString("profile"));
	const ModProfile p = profile.empty() ? ModsMountedProfile() : ModsLoadProfile(profile);
	const std::vector<ModInfo> installed = ModsList();
	const Json *mods = j.Get("mods");
	if (!mods) return out;
	for (const Json &e : mods->arr) {
		ModNeed n;
		n.folder = e.GetString("folder");
		n.id = e.GetString("id");
		n.name = e.GetString("name");
		n.version = e.GetString("version");
		out.folders.push_back(n.folder);
		const Json *tp = e.Get("kind");
		const bool typed = tp && tp->type == Json::String && ModsKindFromId(tp->str) >= 0;
		if (typed) {
			if (ModsKindFromId(tp->str) != kModRules) continue;
		}
		else {
			const Json *gp = e.Get("gameplay");
			if (gp ? !gp->b : e.GetString("fingerprint").empty()) continue;
		}
		bool notRulesNow = false;
		for (const ModInfo &m : installed) {
			if ((!n.id.empty() && _stricmp(m.id.c_str(), n.id.c_str()) == 0) || _stricmp(m.folder.c_str(), n.folder.c_str()) == 0) {
				n.installed = true;
				n.installedFolder = m.folder;
				notRulesNow = !m.gameplay;
				break;
			}
		}
		if (!typed && notRulesNow) continue;
		for (const std::string &f : p.mods) {
			ModInfo m;
			if (_stricmp(f.c_str(), n.folder.c_str()) == 0 || (ModsFind(f, m) && !n.id.empty() && _stricmp(m.id.c_str(), n.id.c_str()) == 0)) {
				n.inProfile = true;
				break;
			}
		}
		out.mods.push_back(n);
	}
	return out;
}

std::string ModsActivateProfileFor(const ModNeeds &needs, const std::string &label)
{
	if (!needs.AllInstalled()) return "";
	auto covers = [&](const std::string &name) {
		const ModProfile p = ModsLoadProfile(name);
		for (const ModNeed &n : needs.mods) {
			bool found = false;
			for (const std::string &f : p.mods) {
				if (_stricmp(f.c_str(), n.installedFolder.c_str()) == 0) found = true;
			}
			if (!found) return false;
		}
		return true;
	};
	const std::vector<std::string> names = ModsProfileNames();
	if (!needs.profile.empty() && std::find(names.begin(), names.end(), needs.profile) != names.end() && covers(needs.profile)) {
		ModsSetActiveProfileName(needs.profile);
		return needs.profile;
	}
	std::vector<std::string> folders;
	const std::vector<ModInfo> installed = ModsList();
	for (const std::string &f : needs.folders) {
		for (const ModInfo &m : installed) {
			if (_stricmp(m.folder.c_str(), f.c_str()) == 0) folders.push_back(m.folder);
		}
	}
	for (const ModNeed &n : needs.mods) {
		if (std::find(folders.begin(), folders.end(), n.installedFolder) == folders.end()) folders.push_back(n.installedFolder);
	}
	const ModProfile p = ModsLooseProfileWith(folders);
	if (!ModsSaveProfile(p)) return "";
	ModsSetActiveProfileName(p.name);
	LogI("content", "\"%s\" with %u mods to load \"%s\"", p.name.c_str(), static_cast<unsigned>(p.mods.size()), label.c_str());
	return p.name;
}

ModProfile ModsLooseProfileWith(const std::vector<std::string> &folders)
{
	ModProfile p;
	p.name = kNoProfile;
	p.mods = folders;
	const std::vector<ModInfo> installed = ModsList();
	const ModProfile active = ModsLoadProfile(ModsActiveProfileName());
	p.choices = active.choices;
	for (const std::string &f : active.mods) {
		bool cosmetic = false;
		for (const ModInfo &m : installed) {
			if (_stricmp(m.folder.c_str(), f.c_str()) == 0) cosmetic = !m.gameplay && m.forbidden.empty();
		}
		if (cosmetic && std::find(p.mods.begin(), p.mods.end(), f) == p.mods.end()) p.mods.push_back(f);
	}
	return p;
}

std::string ModsCheckCivIndices(const std::string &gamePath)
{
	const bool absolute = gamePath.size() > 1 && (gamePath[1] == ':' || gamePath[0] == '\\' || gamePath[0] == '/');
	const std::string file = absolute ? gamePath : GameDir() + "\\" + gamePath;
	Json side;
	ReadJsonFile(file + ".mods.json", side);
	const Json *table = side.Get("civIndices");
	switch (CivIndicesCheckSaved(table, side.GetString("civSaveHash"), table ? HashFile(file) : "")) {
	case CivIndicesStatus::Missing:
		return Tr("This saved game has no civilization index table. Activate its original content profile; automatic civilizations require the .mods.json file beside the save.");
	case CivIndicesStatus::Invalid:
		return Tr("This saved game has an invalid civilization index table. Restore its .mods.json file before loading it.");
	case CivIndicesStatus::Mismatch:
		return Tr("The enabled civilizations do not match this saved game's index table. Activate its original content profile before loading it.");
	case CivIndicesStatus::DifferentFile:
		return Tr("The civilization index table belongs to a different or incomplete save file. Restore its matching .mods.json file, or save again.");
	case CivIndicesStatus::Compatible:
		return "";
	}
	return "";
}

std::string ModsCheckSaveOnLoad(const std::string &gamePath)
{
	const std::string side = GameDir() + "\\" + gamePath + ".mods.json";
	const ModProfile p = ModsMountedProfile();
	Json j;
	if (!ReadJsonFile(side, j)) {
		return "";
	}
	const std::string savedName = LegacyProfileName(j.GetString("profile"));
	const std::string savedHash = j.GetString("gameplayFingerprint");
	if (static_cast<int>(j.GetNumber("schema", 1)) != kRulesSchema) {
		return "";
	}
	const std::string now = ModsProfileGameplayHash(p);
	if (savedHash.empty() || now.empty() || savedHash == now) {
		return "";
	}
	if (savedName == p.name) {
		return Tr("This game was saved with different content than the one you have enabled. It may not load properly.");
	}
	return Tr("This game was saved with the profile \"") + savedName + Tr("\" and you are now using \"") + p.name +
		Tr("\". It may not load properly; switch profiles in Options > Content.");
}

std::vector<std::string> ModsMountedLocalDirs()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_mount.localDirs;
}

std::vector<std::string> ModsMountedDirs()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_mount.dirs;
}

std::string ModsLocalizedText(const Json &j, const char *key, const std::string &def)
{
	return LocalizedText(j, key, def);
}

const std::string &ModsLanguage()
{
	static std::string lang;
	lang = LanguageLower();
	return lang;
}

void InstallModsHooks(bool knownExe)
{
	GameDir();
	if (!knownExe) {
		ModsScan();
		return;
	}
	void *target = reinterpret_cast<void *>(addr::kMountPakFilesInDir);
	if (HookCreate(target, reinterpret_cast<void *>(&HookMountPakFilesInDir),
			reinterpret_cast<void **>(&g_origMountPakFilesInDir)) == MH_OK &&
		HookEnable(target) == MH_OK) {
		LogI("content", "ready to mount profile \"%s\"", ModsActiveProfileName().c_str());
	}
	else {
		LogE("content", "could not hook MountPakFilesInDir; mods will not be loaded");
	}
	InstallRleHook();
}

bool ModsAllowedFile(const std::string &relPath)
{
	return pathsafe::SafeRelative(relPath) && AllowedExtension(NormPath(relPath));
}

std::string ModsFolderPath()
{
	return ModsDir();
}
