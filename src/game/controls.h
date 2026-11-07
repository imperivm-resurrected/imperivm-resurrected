// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HKL__;

void InstallControls(bool knownExe);

enum ModKey {
	kModKeyPanel = 0,
	kModKeySpecPause,
	kModKeySpecFaster,
	kModKeySpecSlower,
	kModKeySpecBack,
	kModKeySpecForward,
	kModKeySpecLive,
	kModKeySpecUi,
	kModKeySpecBar,
	kModKeyCheats,
};
bool ControlsModKeyPressed(int id, unsigned vk);
bool ControlsMouseButton(unsigned msg, unsigned wp, unsigned &vk, bool &down);
bool ControlsEatChar(unsigned msg, unsigned wp);

enum : unsigned char { kKeyCtrl = 1, kKeyShift = 2, kKeyAlt = 4 };

struct ControlKey {
	unsigned short vk = 0;
	unsigned char mods = 0;
	bool operator==(const ControlKey &o) const { return vk == o.vk && mods == o.mods; }
	bool operator!=(const ControlKey &o) const { return !(*this == o); }
};
ControlKey ControlsModKey(int id);

enum KeyRule {
	kRuleAny,
	kRulePlain,
	kRuleOrder,
	kRuleShortcut,
};

enum ControlPage {
	kPageCamera, kPageSelection, kPageGroups, kPageGame, kPageInterface, kPageMod,
	kPageUnits, kPageHero, kPageDruid, kPageBuildings, kPageModOrders, kPageCount
};
const char *ControlsPageName(int page);
bool ControlsPageIsOrders(int page);

struct ControlInfo {
	std::string id;
	std::string name;
	std::string hint;
	std::string section;
	int page = 0;
	bool order = false;
	int context = 0;
	KeyRule rule = kRuleAny;
	bool withOrders = false;
	ControlKey key, def;
	ControlKey key2;
	std::vector<std::string> ids;
};

std::vector<ControlInfo> ControlsList();
void ControlsSetKey(const std::string &id, bool order, int slot, ControlKey key);
void ControlsResetRow(const std::string &id, bool order);
void ControlsResetAll();
bool ControlsGridMode();
std::string ControlsSetGridMode(bool on);
std::string ControlsGridKeys();
std::string ControlsSnapshot();
void ControlsRestore(const std::string &snapshot);
void ControlsGameTick();
void ControlsDrawBarKeys(uint32_t *buf, int w, int h);
bool ControlsBarKeysChanged();
bool ControlsBarLetters();
void ControlsSetBarLetters(bool on);
enum ZoomBoxMode { kZoomBoxCtrl, kZoomBoxAlt, kZoomBoxShift, kZoomBoxOff, kZoomBoxModes };
int ControlsZoomBoxMode();
void ControlsSetZoomBoxMode(int mode);
unsigned ControlsZoomBoxVk();
unsigned ControlsZoomBoxAddVk();
std::string ControlsExport();
bool ControlsImport(const std::string &text);
void ControlsNoteLayout();
HKL__ *ControlsLayout();
std::string ControlsKeyLabel(ControlKey key);
bool ControlsKeyAllowed(KeyRule rule, ControlKey key, std::string &why);
