// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <map>
#include <string>
#include <vector>

struct Civ {
	std::string id;
	std::string constant;
	int index = 0;
	bool automaticIndex = false;
	int base = 0;
	std::string internalName;
	std::string letterUp, letterLow;
	bool ownLetter = false;
	int biome = -1;
	std::string templatePrefix;
	bool random = true;
	bool ai = false;
	bool ownAi = false;
	bool variant = false;
	bool replacesBase = false;
	std::map<std::string, std::string> aliasClasses, aliasOrders;
	bool AiAlias() const { return ai && !ownAi; }
	std::string folder;
	std::vector<std::pair<std::string, std::string>> names;
	std::string modFolder;
	// The id of the content pack (its set in a room). The folder can differ between computers; the id does not.
	std::string modId;
};

inline constexpr char kGameCivPrefixes[] = "grcimbet";
bool CivSetPrefix(Civ &c, const std::string &prefix);
// Returns the owner's civilization index, or -1 when the prefix is available.
int CivPrefixClash(const Civ &c, const std::vector<const Civ *> &accepted);

void CivsRebuild();
const Civ *CivByIndex(int index);
const Civ *CivByName(const std::string &name);
std::vector<Civ> CivsList();
std::string CivDisplayName(const Civ &c);

const char *GameCivName(int index);
int GameCivByName(const std::string &name);
int CivsPlayerCiv(int player);

void *CivsLocalPlayer();
struct CivRaceAsBase {
	explicit CivRaceAsBase(void *player);
	~CivRaceAsBase();
	CivRaceAsBase(const CivRaceAsBase &) = delete;
	CivRaceAsBase &operator=(const CivRaceAsBase &) = delete;
	int *slot = nullptr;
	int real = 0;
};

std::vector<std::string> CivsErrorsFor(const std::string &folder);
void CivsRegisterConstants(void *engine);

void CivsOnRoomShown(void *dlg);
const char *CivsRoomSeparator();
std::vector<int> CivsRoomOrder(int items);
// Whether the room's sets (CivRoomSets) allow the civilization. "Random" is always allowed.
bool CivsRoomAllows(int race);
struct CivSet {
	std::string id, name;
};
// The sets the host can pick for a room: the game's, and one per content pack that has civilizations.
std::vector<CivSet> CivsSets();
int CivsRoomReplacement(int item, int items);
int CivsRaceOfRoomItem(int item);
int CivsRoomItemOfRace(int race);
void CivsBuildBegin();
void CivsBuildEnd(void *settings);

const char *CivsCityClassName(void *registry, const char *name, std::string &buf);

void InstallCivs(bool knownExe);
void InstallCivUi(bool knownExe);

void InstallCivAi(bool knownExe);
void CivAiRebuild();
