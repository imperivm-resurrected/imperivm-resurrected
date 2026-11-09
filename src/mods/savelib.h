// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

const char kSaveOnlineFolder[] = "Online";

enum SaveKind { kSaveStrategy = 0, kSaveAdventure = 1, kSaveConquest = 2, kSaveReplay = 3 };

struct SaveMod {
	std::string folder, id, name, version;
	std::string hash;
	int kind = -1;
	bool oldGameplay = false;
	// The ruleset of this package the game was saved with and its gameplay hash. Empty if it used the package's root alone.
	std::string ruleset, rulesetHash;
};

struct SaveRec {
	std::string name;
	std::string campaign;
	std::string baseName;
	std::string profile;
	std::string vfsPath;
	std::string diskPath;
	int kind = kSaveStrategy;
	uint64_t time = 0;
	uint64_t size = 0;
	bool online = false;
	bool autosave = false;
	std::string session;
	std::string modProfile;
	std::string modHash;
	int modSchema = 1;
	std::vector<SaveMod> mods;
	int playSeconds = -1;
	std::vector<int> playerStates;
	bool hasThumb = false;
};

std::vector<SaveRec> SaveLibScan(const std::string &profile = "");
std::string SaveLibActiveProfile();
bool SaveLibEnsureOnlineDir(const std::string &profile);

bool SaveLibLoadThumb(const SaveRec &rec, std::vector<uint32_t> &bgra, int &w, int &h);
bool SaveLibDelete(const SaveRec &rec);
std::string SaveLibRename(const SaveRec &rec, const std::string &newName);
void SaveLibCleanOrphans(const std::string &profile);
bool SaveLibValidName(const std::string &name);
bool SaveLibExists(const SaveRec &rec, const std::string &newName);
void SaveLibPruneAutosaves(const std::string &profile, const std::string &session, int keep);

std::map<std::string, std::string> SaveLibLoadGameNames(const std::string &profile);
bool SaveLibSetGameName(const std::string &profile, const std::string &key, const std::string &name);

void SaveSessionBegin();
void SaveSessionOnLoad(const std::string &rel);
void SaveSessionIgnoreLoads(bool ignore);
bool SaveSessionLoadsIgnored();
std::string SaveSessionCurrent();
int SaveLibGameSeconds();
bool SaveLibPlayerStates(int out[8]);
std::string SaveLibGroupKey(const SaveRec &rec);
bool SaveLibIsAutosaveName(const std::string &baseName);
