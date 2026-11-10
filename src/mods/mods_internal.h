// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <windows.h>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cstdint>
#include "MinHook.h"
#include "core/sha256.h"
#include "core/versioncmp.h"
#include "mods/modclash.h"
#include "mods/modrules.h"
#include "mods/rulesets.h"
#include "mods/mods.h"
#include "mods/savelib.h"
#include "addr/state.h"

namespace mods_impl {

const char kOriginal[] = "Original";

const char kNoProfile[] = "No profile";

const uintptr_t kLanguageName = addr::kLanguageSettings + 0x20;

typedef void(__cdecl *MountPakFilesInDir_t)(const char *, void *, const char *);

typedef void *(__cdecl *OperatorNew_t)(size_t);

typedef void *(__fastcall *Ctor0_t)(void *self);

typedef void *(__fastcall *CTransFSCtor_t)(void *self, void *edx, const char *prefix, void *target);

extern MountPakFilesInDir_t g_origMountPakFilesInDir;
extern std::mutex g_lock;
extern std::vector<ModInfo> g_mods;
void ModsNoteScan(std::vector<ModInfo> &mods);
void ModsBumpSerial();
extern std::string g_language;
struct MountedState {
	ModProfile profile;
	ModRuleset choice;
	bool known = false;
	std::vector<std::string> localDirs;
	std::vector<std::string> dirs;
	std::map<std::string, std::string> chosen;
	std::map<std::string, std::string> signatures;
	void *layerList = nullptr;
	std::map<std::string, void *> looseLayers;
};
extern MountedState g_mount;
// The game files the packs of the profile hide, set in the VFS (the same as MountProfileLayers does at start-up).
void ApplyHiddenFiles(const ModProfile &p);
// Mount (or take out) the loose files of a folder of mods\, as the VFS name "mods/<pack>/rulesets/<id>/", in place. For an in-place switch.
bool MountLooseFolder(const std::string &vfsPrefix);
bool UnmountLooseFolder(const std::string &vfsPrefix);
inline std::string ModSignature(const ModInfo &m)
{
	return m.version + "|" + std::to_string(m.fileCount) + "|" + std::to_string(m.totalBytes);
}
extern std::atomic<bool> g_hashing;
std::string GameDir();
std::string ModsDir();
std::string ProfilesDir();
std::string IniPath();
bool IsDir(const std::string &p);
struct ScanFile {
	std::string rel;
	uint64_t size = 0;
	uint64_t mtime = 0;
	bool gameplay = false;
	bool content = false;
	std::string text;
	// The ruleset this file belongs to ("" for the package root) and how many characters of rel are the folder of that ruleset.
	std::string layer;
	size_t layerStrip = 0;
};

extern std::map<std::string, std::vector<ScanFile>> g_scanFiles;
void Walk(const std::string &base, const std::string &rel, std::vector<ScanFile> &out, int depth = 0);
std::string LanguageLower();
std::string LocalizedText(const Json &j, const char *key, const std::string &def);
ModInfo ScanMod(const std::string &folder, std::vector<ScanFile> &files);
bool PatchTextOf(const std::string &folder, const std::string &target, std::string &out, const std::string &layer = "");
void StartHashing();
ModProfile ReadProfileFile(const std::string &path);
ClashReport ClashesOf(const ModProfile &p, const std::vector<ModInfo> &all);
void PrepareRleMmp(const ModProfile &p, const std::vector<ModInfo> &all);
std::string RleProblemOf(const std::string &folder);
uint64_t GameRleSize();
void InstallRleHook();
std::string ClashProblemText(const std::string &other);
std::map<std::string, std::string> ChosenFolders(const ModProfile &p, const std::vector<ModInfo> &all);
const char kReadOnlyError[] = "MODREADONLY";

struct ReadOnlyLayerState {
	void *vtable[40];
	bool vtableReady = false;
	void *origTransOpen = nullptr;
};
extern ReadOnlyLayerState g_readOnly;
void __cdecl HookMountPakFilesInDir(const char *dir, void *list, const char *lang);

}
