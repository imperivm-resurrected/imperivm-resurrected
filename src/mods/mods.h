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

// A ruleset: a folder of the package (rulesets/<id>) shaped like a package root, mounted only when the profile chooses it.
// It is scanned as a root of its own, so its paths have the folder's prefix cut off.
struct ModLayer {
	std::string id;
	std::string name;
	std::string description;
	std::string path;
	std::vector<std::string> paths;
	std::vector<std::string> loosePaths;
	std::vector<PatchedValue> patchValues;
	std::vector<std::string> hidden;
	bool rleTail = false;
	uint64_t rleBaseSize = 0;
	std::string rleBaseSha256;
	std::string rleTailError;
	bool hasLoose = false;
	bool gameplay = false;
	bool content = false;
	bool folderMissing = false;
	unsigned fileCount = 0;
	uint64_t totalBytes = 0;
	std::string gameplayHash;
};

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
	uint64_t gbFile = 0;
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
	// Of the package root only; each ruleset has its own in layers.
	std::string gameplayHash;
	std::vector<ModLayer> layers;
	std::vector<std::string> rulesetProblems;
};

inline const ModLayer *ModsFindLayer(const ModInfo &m, const std::string &id)
{
	for (const ModLayer &l : m.layers) {
		if (!id.empty() && _stricmp(l.id.c_str(), id.c_str()) == 0) return &l;
	}
	return nullptr;
}

inline bool ModsIsMod(const ModInfo &m, const std::string &id, uint64_t gbId)
{
	if (m.gbId && gbId) return m.gbId == gbId;
	return !id.empty() && _stricmp(m.id.c_str(), id.c_str()) == 0;
}

inline bool ModsSameMod(const ModInfo &a, const ModInfo &b)
{
	return ModsIsMod(a, b.id, b.gbId);
}

// A room gives the start of each gameplay mod's fingerprint: two copies with the same version can still have other files.
inline std::string ModsRoomCopyKey(const std::string &gameplayHash)
{
	return gameplayHash.substr(0, 16);
}

enum class RoomCopy { kSame, kOther, kNotHashed };

inline RoomCopy ModsCompareRoomCopy(const ModInfo &have, const std::string &roomKey)
{
	if (roomKey.empty()) return RoomCopy::kSame;
	if (have.gameplay && have.gameplayHash.empty()) return RoomCopy::kNotHashed;
	return have.gameplayHash.size() >= roomKey.size() && have.gameplayHash.compare(0, roomKey.size(), roomKey) == 0 ? RoomCopy::kSame : RoomCopy::kOther;
}

// How a room checks a ruleset: the layer id from the room, and the start of its gameplay hash.
inline RoomCopy ModsCompareRoomLayer(const ModInfo &have, const std::string &layerId, const std::string &layerKey)
{
	if (layerId.empty()) return RoomCopy::kSame;
	const ModLayer *l = ModsFindLayer(have, layerId);
	if (!l) return RoomCopy::kOther;
	if (layerKey.empty()) return RoomCopy::kSame;
	if (l->gameplay && l->gameplayHash.empty()) return RoomCopy::kNotHashed;
	return l->gameplayHash.size() >= layerKey.size() && l->gameplayHash.compare(0, layerKey.size(), layerKey) == 0 ? RoomCopy::kSame : RoomCopy::kOther;
}

struct ModChoice {
	std::string use;
	std::string over;
};

// The ruleset a profile mounts: layer `id` of the package in folder `pack`. Empty is the game's own rules.
struct ModRuleset {
	std::string pack;
	std::string id;
	bool Empty() const { return pack.empty() || id.empty(); }
};

inline bool ModsSameRuleset(const ModRuleset &a, const ModRuleset &b)
{
	if (a.Empty() || b.Empty()) return a.Empty() && b.Empty();
	return _stricmp(a.pack.c_str(), b.pack.c_str()) == 0 && _stricmp(a.id.c_str(), b.id.c_str()) == 0;
}

struct ModProfile {
	std::string name;
	std::vector<std::string> mods;
	std::vector<ModChoice> choices;
	ModRuleset ruleset;
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
std::string ModsRulesLevel();

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
// A mounted folder is a package's or one of its rulesets'. These give the package's folder and the folder's path below mods/.
std::string ModsPackFolder(const std::string &mountedDir);
std::string ModsDirVfsName(const std::string &mountedDir);
// The ruleset of the profile if it can be mounted (its package is in the profile, installed, and has that layer); empty otherwise.
ModRuleset ModsEffectiveRuleset(const ModProfile &p);
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
