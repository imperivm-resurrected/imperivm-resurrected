// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <string>

struct Config {
	bool renderEnabled = true;
	bool vsync = true;
	int brightness = 100;
	int movieVolume = 50;
	bool movieMute = false;
	bool largeAddress = true;
	bool fakeModeChange = true;

	bool windowed = false;
	int windowW = 1600;
	int windowH = 900;
	bool trapMouse = true;
	int monitor = 0;
	int resolutionW = 0;
	int resolutionH = 0;
	int resolutionScreenW = 0;
	int resolutionScreenH = 0;
	bool extendedView = false;
	bool showPerf = false;
	bool showNet = false;
	int panelX = 1000;
	int panelY = 0;
	bool panelCompact = false;
	int panelOpacity = 90;
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

	bool modOverrides = true;

	bool logFiles = false;
	bool rejoinTrace = false;
	bool rejoinReload = true;
	std::string dumpFiles;
	std::string dumpAtStartup;
};

const Config &GetConfig();
Config &MutableConfig();
void SaveConfig();
void LoadConfig();
