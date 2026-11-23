// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/uikit.h"

#include "core/objectivelog.h"
#include "game/greatbattles.h"
#include "mods/campaignpacks.h"
#include "ui/fullscreen.h"
#include "ui/listbox.h"

namespace battlesui {

// kActOverview: back from a campaign to the overview; kActOpenTab: a campaign's card (arg: its tab); kActContinue: arg 0 the newest
// game of all, 1 the newest of the chosen adventure; kActAllSaves: the load screen; kActPage: the adventure's briefing (0) or its
// diary (1); kActContent: the content packs screen; kActDownload: downloads the
// official campaign of a tab (arg); kActPureSwitch, kActPureCancel: the answers to the dialog of a campaign that is played without other
// gameplay mods.
enum Action { kActNone = 0, kActTab, kActRow, kActDifficulty, kActStart, kActIntro, kActBack, kActOverview, kActOpenTab, kActContinue, kActAllSaves, kActPage, kActContent, kActDownload, kActPureSwitch, kActPureCancel };

inline int RowArg(int list, int index) { return list << 16 | index; }

// A saved game of one of the screen's adventures.
struct SaveInfo {
	std::string vfsPath, name, title, when, played;
	int list = -1, index = -1, tab = -1;
	std::shared_ptr<const BattleImage> picture;
	// The objectives the game had completed when it was saved, newest first; null when the save kept none.
	std::shared_ptr<const std::vector<ObjectiveNote>> diary;
};

struct View {
	BattlesSnapshot snap;
	int mouseX = -10000, mouseY = -10000;
	int listScroll = 0, descScroll = 0;
	// What the player has won, for a later version: each one is drawn only when it has text.
	std::string badge, progress;
	// The overview of the campaigns comes first; a card opens one. The conquests never have it.
	bool overview = true;
	// The newest game of all and the newest of each adventure (by RowArg), and how many adventures with a game each tab has.
	bool hasLatest = false;
	SaveInfo latest;
	std::map<int, SaveInfo> lastByRow;
	int savedInTab[kBattlesTabCount] = {};
	// The chosen adventure shows the diary of its newest game instead of its briefing; another choice goes back to the briefing.
	bool diary = false;
	// Set once a Continue has started a load: nothing else is pressed meanwhile.
	bool loading = false;
	// The content packs of the Imperivm I and II campaigns, by tab, for a game whose list has none of their adventures because its
	// pack is not active; and the picture next to each one's adventure.
	BattleCampaignPack packs[kBattlesTabCount];
	std::shared_ptr<const BattleImage> packCovers[kBattlesTabCount];
	// The official download of the Imperivm I and II campaigns, by tab: whether it is installed, and its progress.
	CampaignPackView offers[kBattlesTabCount];
	// The adventures won by the active profile, with the hardest difficulty of each.
	std::vector<BattleLaurel> laurels;
	// How much of each tab's campaign is done: its laurels against three for each adventure, in percent; -1 when it has no adventures.
	int done[kBattlesTabCount] = {-1, -1, -1, -1, -1};
	// The tab of Imperivm I or II whose start was held back because other gameplay mods are active, and their names; -1 when none.
	int pureTab = -1;
	std::string pureMods;
};

inline bool ShowsOverview(const View &v) { return v.overview && v.snap.tab != kBattlesTabConquests; }

struct PaintOut {
	ui::ListGeom list, desc;
	// A campaign card goes through its pictures: the game must keep drawing frames for it.
	bool turning = false;
};

// The screen fills the frame: its height is always 768 and its width follows the frame's shape. Its header is the full screens'
// one, drawn in their units (`brand`, ScreenViewFor) so that it matches theirs.
constexpr int kScreenH = 768;
constexpr int kListRowH = 38, kDescLineH = 20;

float ScreenScale(int frameW, int frameH);
int ScreenWidth(int frameW, int frameH);
// Where a row starts inside the list's content, for keeping the chosen one in view; -1 when it is not there.
int RowTop(const BattlesSnapshot &s, int list, int index);
void PaintBattlesScreen(ui::Painter &p, int screenW, const ui::ScreenView &brand, const View &v, PaintOut &out);

}
