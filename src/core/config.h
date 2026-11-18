// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <string>

#include "core/log.h"

struct Config {
	bool renderEnabled = true;
	bool vsync = true;
	int brightness = 100;
	int movieVolume = 50;
	bool movieMute = false;
	bool largeAddress = true;
	bool fakeModeChange = true;

	bool windowed = false;
	bool trapMouse = true;
	int monitor = 0;
	int resolutionW = 0;
	int resolutionH = 0;
	int resolutionScreenW = 0;
	int resolutionScreenH = 0;
	// The window's size and the screen it was chosen on: resolutionW/H is the full screen's.
	int windowW = 0;
	int windowH = 0;
	int windowScreenW = 0;
	int windowScreenH = 0;
	bool extendedView = false;
	int fpsLimit = 0;
	int imageScaling = 0;
	bool showPerf = false;
	bool showNet = false;
	int panelX = 1000;
	int panelY = 0;
	bool panelCompact = false;
	int panelOpacity = 90;
	// Where the chat sits, in thousandths of the screen: its left edge and its bottom edge. -1 is the default place.
	int chatX = -1;
	int chatY = -1;
	bool dpiAware = true;

	bool muteInBackground = true;
	bool modernAudio = true;
	bool fixVoices = true;
	bool onlineAlerts = true;

	float scrollSpeed = 1.0f;
	bool autosave = true;
	int autosaveMinutes = 10;
	bool autosaveOnExit = false;
	int autosaveSlots = 3;
	bool crashFix = true;
	bool smoothScroll = true;
	int healthBarOwners = 0;
	bool healthBarHeroesOnly = false;
	// [Game] AllyColors: on this PC, the player blue, allies yellow, neutrals grey and enemies red in the game (game/allycolors.h).
	bool allyColors = false;
	bool pauseInBackground = false;
	bool zoomExtras = true;
	bool zoomFog = true;
	int zoomRefreshMs = 16;

	bool modOverrides = true;

	LogLevel logLevel = kLogInfo;
	bool logFiles = false;
	bool rejoinTrace = false;
	bool rejoinReload = true;
	std::string dumpFiles;
	std::string dumpAtStartup;

	int masterVolume = 100;
	int natureVolume = -1;
	int conversationVolume = 100;
	std::string outputDevice;

	// [League]: the server's address (empty: the public one) and the account linked to this computer. The token is kept encrypted
	// for this Windows user; here it is plain. Only the league client touches these, under its own lock.
	std::string leagueUrl;
	std::string leagueToken, leagueSteamId, leagueName;
	// [League] OfferLink: Ranked and Tournaments offer to link the account until the player says "Not now".
	bool leagueOfferLink = true;
	// [League] ChatBadges: the league badge after each name in the chat; MatchFoundSound: the sound when a ranked match is found.
	bool chatBadges = true;
	bool matchFoundSound = true;
};

const Config &GetConfig();
// The size saved for full screen or for a window, and the screen it was chosen on; 0 when none was.
struct SavedSize {
	int w = 0, h = 0, screenW = 0, screenH = 0;
};
SavedSize ConfigSavedSize(const Config &c, bool windowed);
void ConfigSetSavedSize(Config &c, bool windowed, const SavedSize &s);
Config &MutableConfig();
void SaveConfig();
void LoadConfig();
// Writes only the league account ([League] Token, SteamId and Name), or removes it when the token is empty.
void SaveLeagueAccount();
// Writes only [League] OfferLink.
void SaveLeagueOffer();
