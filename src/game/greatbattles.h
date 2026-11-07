// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "game/battlelist.h"

void InstallGreatBattlesHooks(bool knownExe);

// The game's adventure screen (CVXUIPreAdventureMenu) stays open under the mod's one and keeps scanning, sorting and starting the
// adventures: these read and drive it. Everything but BattlesScreenWanted belongs on the game thread.
enum BattlesTab { kBattlesTabGreat, kBattlesTabConquests, kBattlesTabCustom, kBattlesTabCount };
// The game's three lists: Rome's victories (0x1001), Rome's defeats (0x1002) and the rest (0x1083, also the conquests).
enum BattlesList { kBattlesVictories, kBattlesDefeats, kBattlesOthers, kBattlesListCount };

struct BattlesEntry {
	std::string title, path;
};

struct BattlesSnapshot {
	bool open = false;
	bool switching = false;
	bool starting = false;
	int tab = kBattlesTabGreat;
	std::vector<BattlesEntry> lists[kBattlesListCount];
	int selList = -1, selIndex = -1;
	std::string description;
	std::vector<std::string> difficulties;
	int difficulty = -1;
	bool difficultyShown = false;
	bool intro = false;
	std::shared_ptr<const BattleImage> image;
};

void *BattlesDialogOpen();
// True while the screen is open or about to be replaced by its other mode; the drawing thread may ask.
bool BattlesScreenWanted();
bool BattlesIntroPlaying();
BattlesSnapshot BattlesRead();
void BattlesSelect(int list, int index);
void BattlesSetDifficulty(int index);
void BattlesSetTab(int tab);
void BattlesStart();
void BattlesBack();
void BattlesPlayIntro();
