// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game/bonuses.h"

const unsigned kSiteCaves = 0x2;
const unsigned kSiteRuins = 0x10;
const unsigned kSiteStrongholds = 0x80;
const unsigned kSiteItems = 0x200;
const unsigned kSiteMask = kSiteCaves | kSiteRuins | kSiteStrongholds | kSiteItems;

const unsigned kRuleBlindPick = 0x10000;
const unsigned kRuleFreeAttach = 0x20000;
const unsigned kRuleLockDiplomacy = 0x40000;
const unsigned kRuleCheats = 0x8000000;
const unsigned kRuleBaseMask = kRuleBlindPick | kRuleFreeAttach | kRuleLockDiplomacy | kRuleCheats;
unsigned GameRuleMask();
#define kRuleMask (GameRuleMask())

struct GameRoomSlot {
	bool used = false;
	bool local = false;
	std::string name;
	int type = 0;
	int color = 0;
	bool ready = false;
	int team = 0xff;
	int race = 0;
	int bonus = 0;
	std::vector<std::string> raceNames;
	std::vector<std::string> nameItems;
	int nameSel = -1;
	bool aiAllowed = true;
	bool exists = true;
	bool joining = false;
	int fixedRace = -1;
};

struct GameRoomOption {
	int id = 0;
	std::string label;
	bool checkbox = false;
	bool value = false;
	std::vector<std::string> items;
	std::vector<char> itemDisabled;
	int sel = -1;
};

struct GameRoomSnapshot {
	bool active = false;
	bool host = false;
	bool online = false;
	bool busy = false;
	bool randomMap = false;
	unsigned siteFlags = 0;
	unsigned rules = 0;
	int localSlot = 0;
	GameRoomSlot slots[8];
	std::vector<GameRoomOption> options;
	uint32_t colors[12] = {};
	std::string optionsText;
	std::string scenario;
	bool scenarioMissing = false;
	std::string mode;
	bool picker = false;
	std::vector<std::string> pickerItems;
	std::string pickerDescription;
	std::vector<std::string> pickerPaths;
	std::vector<BonusDef> bonuses;
};

void GameRoomInstall(bool knownExe);
void GameRoomTick();
GameRoomSnapshot GameRoomGet();
bool GameRoomActive();
std::string GameRoomTakeReplacedScenario();

void GameRoomSetRace(int slot, int index);
void GameRoomSetSlotItem(int slot, int index);
void GameRoomSetSlotType(int slot, int type);
void GameRoomKick(int slot);
void GameRoomPlayerLeft(const std::string &name);
void GameRoomSetSiteFlags(unsigned bits);
void GameRoomSetRules(unsigned bits);
void GameRoomRefreshChoices();

void GameRulesOnGameStart();
void GameRulesOnGameEnd();
void GameRoomCycleTeam(int slot);
void GameRoomSetBonus(int slot, int index);
void GameRoomToggleReady();
void GameRoomStart();
void GameRoomCancel();
void GameRoomSetOption(int id, int index);
void GameRoomToggleOption(int id);
void GameRoomCycleColor(int slot, int dir);
void GameRoomSetColor(int slot, int color);
void GameRoomSwapSlots(int a, int b);
void GameRoomUseOnlineName(const std::string &name);
void GameRoomRestoreProfileName();
bool GameRoomOnlineNameActive();
void GameRoomPickerSelect(int index);
void GameRoomPickerAccept();
void GameRoomPickerCancel();
