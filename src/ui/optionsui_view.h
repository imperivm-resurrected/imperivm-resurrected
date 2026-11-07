// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "ui/ornaments.h"
#include "ui/uikit.h"

namespace optionsui {

// The ids of the game's options dialog (gameoptions.ini); the mod's new settings, which it does not have, start at 0x9000.
enum ControlId {
	kResolution = 0x1004, kMusicOn = 0x100B, kMusicVolume = 0x100F, kEffectsOn = 0x1010, kNatureOn = 0x1011, kEffectsVolume = 0x1013, kSpeechOn = 0x1015,
	kConversationsOn = 0x1016, kSpeechVolume = 0x1018, kGameSpeed = 0x101D, kScrollSpeed = 0x101E, kWindowed = 0x1101, kVSync = 0x1102, kTrapMouse = 0x1104,
	kMuteBackground = 0x1105, kShowPerf = 0x1106, kAutosave = 0x1109, kAutosaveEvery = 0x110A, kBrightness = 0x110C, kMovieVolume = 0x1110, kMovieOn = 0x1111,
	kAlerts = 0x1113, kAutosaveCopies = 0x1116, kSaveOnExit = 0x1119, kShowNet = 0x111A, kLanguage = 0x111C, kMonitor = 0x111D, kUpdates = 0x111E,
	kPanelCompact = 0x9001, kPanelOpacity = 0x9002, kSmoothCamera = 0x9003, kModernAudio = 0x9004, kOpenLogs = 0x9101, kCreateReport = 0x9102,
	kModVersion = 0x9040, kLogLevel = 0x9042, kCheckUpdates = 0x9103,
};

enum RowKind { kRowToggle, kRowSlider, kRowToggleSlider, kRowCombo, kRowSegment, kRowAction, kRowInfo };

struct Row {
	std::string label, hint;
	RowKind kind = kRowToggle;
	int id = 0;
	int toggleId = 0;
	bool on = false;
	int value = 0, min = 0, max = 100;
	int shown = -1;
	std::vector<std::string> items;
	int selected = -1;
	std::string action;
	std::string text, badge;
	bool restart = false;
	bool enabled = true;
};

struct Card {
	std::string title;
	std::vector<Row> rows;
};

struct Page {
	std::string title, subtitle, blurb;
	Ornament icon = kOrnamentScreen;
	int leftW = 409;
	std::vector<Card> left, right;
};

struct Values {
	std::map<int, bool> checks;
	std::map<int, int> scrolls;
	std::map<int, int> combos;
	std::map<int, std::vector<std::string>> items;
	std::map<int, bool> disabled;
	std::map<int, std::string> texts;
	bool operator==(const Values &o) const { return checks == o.checks && scrolls == o.scrolls && combos == o.combos; }
};

std::vector<Page> BuildPages(const Values &v);

enum Action {
	kActNone = 0, kActPage, kActToggle, kActSlider, kActCombo, kActSegment, kActRow, kActPick, kActDropClose, kActOk, kActApply, kActCancel, kActReset, kActControls,
};

struct View {
	int page = 0;
	int mouseX = -10000, mouseY = -10000;
	int dropId = 0, dropScroll = 0;
	int dragId = 0;
	bool dirty = false;
	Values values;
};

constexpr int kScreenW = 1100, kScreenH = 680;
constexpr int kSliderMargin = 10;

void PaintOptionsScreen(ui::Painter &p, const View &v);

}
