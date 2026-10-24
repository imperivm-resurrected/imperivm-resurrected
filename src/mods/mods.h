// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "mods/patchxml.h"

inline constexpr int kScriptApiVersion = 7;

struct ModDependency {
	std::string id;
	std::string version;
};

struct ModLanguage {
	std::string code;
	std::string name;
	std::string base;
};

struct ModOrderLabel {
	std::string file;
	std::vector<std::pair<std::string, std::string>> names;
};

enum ModKind { kModCosmetic = 0, kModContent = 1, kModRules = 2 };
const char *ModsKindId(int kind);
int ModsKindFromId(const std::string &id);
int ModsRulesSchema();

struct ModInfo {
	std::string folder;
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::string license;
	std::vector<std::string> authors;
	bool redistributable = false;
	bool hasManifest = false;
	std::string manifestError;
	std::vector<ModDependency> requires;
	std::vector<std::string> incompatible;
	std::string sourceUrl;
	uint64_t gbId = 0;
	std::vector<std::string> loadAfter;
	std::vector<std::string> loadBefore;
	std::vector<std::string> tags;
	std::string category;
	std::vector<ModLanguage> languages;
	std::vector<ModOrderLabel> orderLabels;

	std::vector<std::string> paths;
	std::vector<std::string> loosePaths;
	std::vector<PatchedValue> patchValues;
	std::vector<std::string> forbidden;
	bool rleTail = false;
	uint64_t rleBaseSize = 0;
	std::string rleBaseSha256;
	std::string rleTailError;
	std::vector<std::string> hidden;
	bool hasLoose = false;
	bool gameplay = false;
	bool content = false;
	int kind = kModCosmetic;
	int declaredKind = -1;
	uint64_t totalBytes = 0;
	uint64_t installedTime = 0;
	bool newSinceStart = false;
	unsigned fileCount = 0;
	bool noGameFiles = false;
	std::string hash;
	std::string gameplayHash;
};

inline bool ModsIsMod(const ModInfo &m, const std::string &id, uint64_t gbId)
{
	if (m.gbId && gbId) return m.gbId == gbId;
	return !id.empty() && _stricmp(m.id.c_str(), id.c_str()) == 0;
}

inline bool ModsSameMod(const ModInfo &a, const ModInfo &b)
{
	return ModsIsMod(a, b.id, b.gbId);
}

struct ModChoice {
	std::string use;
	std::string over;
};

struct ModProfile {
	std::string name;
	std::vector<std::string> mods;
	std::vector<ModChoice> choices;
};

enum MapOriginKind { kMapBase, kMapLoose, kMapReceived, kMapMod };
struct MapOrigin {
	int kind = kMapBase;
	std::string modName;
	std::string modFolder;
};
MapOrigin ModsMapOrigin(const std::string &scenarioPath);
bool ModsDeleteMap(const std::string &scenarioPath, std::string &message);
void ModsScan();
uint64_t ModsListSerial();
void ModsWatchStart();
void ModsWatchStop();
bool ModsHashProgress(uint64_t &done, uint64_t &total);
std::vector<ModInfo> ModsList();
std::vector<ModInfo> ModsSummaries();
bool ModsFind(const std::string &folder, ModInfo &out);

std::vector<std::string> ModsProfileNames();
ModProfile ModsLoadProfile(const std::string &name);
bool ModsSaveProfile(const ModProfile &p);
bool ModsDeleteProfile(const std::string &name);
std::string ModsActiveProfileName();
void ModsSetActiveProfileName(const std::string &name);
const ModProfile &ModsMountedProfile();
bool ModsScenarioProvider(const std::string &scenarioPath, std::string &id, std::string &version, std::string &name);

struct ModsMapPack {
	std::string id, version, name, file;
	uint64_t gbId = 0;
	bool simple = false;
	bool shareable = false;
};
bool ModsScenarioPack(const std::string &scenarioPath, ModsMapPack &out);
std::string ModsInstalledPackMap(const std::string &id, const std::string &version, const std::string &scenarioPath);
bool ModsMountedHasExact(const std::string &id, const std::string &version);
bool ModsRestartNeeded();
bool ModsLoadsWithoutRestart(const std::string &folder);
bool ModsApplyWithoutRestart();
bool ModsRenameProfile(const std::string &from, const std::string &to);
std::string ModsProfileLabel(const std::string &name);
bool ModsIsReservedProfileName(const std::string &name);
std::vector<std::string> ModsProfilesUsing(const std::string &folder);
std::string ModsUninstall(const std::string &folder);
void ModsNoteReceivedMap(const std::string &fullPath);
bool ModsUninstallPending(const std::string &folder);
void ModsCancelUninstall(const std::string &folder);
void ModsApplyPendingUninstalls();
void ModsPruneMissing();
std::vector<std::string> ModsTakePruned();
int ModsCompareVersions(const std::string &a, const std::string &b);
void ModsQueueSwap(const std::string &newFolder, const std::string &oldFolder);
void ModsApplyPendingSwaps();

std::vector<std::string> ModsProblems(const ModProfile &p);
std::string ModsProfileGameplayHash(const ModProfile &p);

void ModsRestartGame(int largeAddress = -1, const std::string &args = "");
void ModsPatchExeAtExit(int largeAddress);
bool ModsExeIsLargeAddressAware();
void ModsOpenFolder(const std::string &folder);

bool ModsOnSaveWritten(const std::string &gamePath, bool online = false);
std::string ModsCheckSaveOnLoad(const std::string &gamePath);
std::string ModsCheckCivIndices(const std::string &gamePath);

struct ModNeed {
	std::string folder, id, name, version;
	bool installed = false;
	std::string installedFolder;
	bool inProfile = false;
};
struct ModNeeds {
	std::vector<ModNeed> mods;
	std::string profile;
	std::vector<std::string> folders;
	bool Satisfied() const;
	bool AllInstalled() const;
	std::vector<std::string> Names(bool onlyMissing) const;
};
ModNeeds ModsNeededFor(const std::string &gamePath, const std::string &profile = "");
std::string ModsActivateProfileFor(const ModNeeds &needs, const std::string &label);
ModProfile ModsLooseProfileWith(const std::vector<std::string> &folders);

std::vector<std::string> ModsMountedLocalDirs();
std::vector<std::string> ModsMountedDirs();
const std::string &ModsLanguage();
struct Json;
std::string ModsLocalizedText(const Json &j, const char *key, const std::string &def);

void InstallModsHooks(bool knownExe);

bool ModsAllowedFile(const std::string &relPath);
std::string ModsFolderPath();

std::vector<std::string> ModsLoadOrder(const std::vector<std::string> &folders);
bool ModsSameSet(std::vector<std::string> a, std::vector<std::string> b);
struct ModOverlap {
	std::string folder;
	std::string name;
	int files = 0;
	int gameplay = 0;
	int mine = 0;
	int theirs = 0;
	bool problem = false;
	bool declared = false;
	bool choosable = false;
	bool canUseMine = false;
	bool canUseTheirs = false;
	int chosen = 0;
};
struct ModOverlapFile {
	std::string path;
	int kind = kModCosmetic;
	std::string used;
	std::string usedName;
	bool chosen = false;
};
struct ModOverlapValue {
	std::string file;
	std::string label;
	int kind = kModCosmetic;
	std::string usedName;
};
struct ModIssues {
	std::vector<std::string> problems;
	std::vector<std::string> notices;
	int overwrites = 0;
	std::vector<ModOverlap> overlaps;
	std::vector<ModOverlapFile> files;
	std::vector<ModOverlapValue> values;
};
ModIssues ModsIssuesFor(const ModProfile &p, const std::string &folder);
std::map<std::string, ModIssues> ModsIssuesForAll(const ModProfile &p);
std::vector<std::string> ModsProblemsFrom(const ModProfile &p, const std::map<std::string, ModIssues> &issues);
void ModsReadPatchedFiles();
void ModsChooseFiles(ModProfile &p, const std::string &useFolder, const std::string &overFolder);
const std::string *ModsChosenFile(const std::string &rel);
