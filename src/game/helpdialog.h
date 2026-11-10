// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <string>

#include "core/helpdoc.h"
#include "game/battlelist.h"

// The game's help (CHelpDlg, F1) stays open under the mod's screen, which follows the topic the game opens and closes it with
// its own Close button. Null when it is not open. All of this belongs on the game thread.
void *HelpDialogOpen();
// The topic the dialog shows: its index in the game's document, to notice when the game opens another one, and its path.
int HelpDialogTopic(void *dlg);
std::string HelpDialogTopicPath(void *dlg);
bool HelpDialogClose(void *dlg);

// currentlang/help.xml in the player's language, as the game reads it: from the mods and packs, with their patches.
bool HelpGameText(std::string &out);
// data/CounterUnits.xml, the counters the computer players recruit.
bool HelpCounterText(std::string &out);
// A picture of the help (UI/icons/*.bmp) through the game's files.
bool HelpGameImage(const std::string &path, BattleImage &out);

// What the classes the game loaded say about the topics of the document: the unit pages get their class's numbers.
struct HelpGameData {
	std::map<int, HelpUnitStats> stats;
	// Lower-case class id -> its topic, for the units that counter each other.
	std::map<std::string, int> classTopic;
};
HelpGameData HelpCollectGameData(const HelpDoc &doc);
