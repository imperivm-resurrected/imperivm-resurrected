// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/config.h"
#include "core/legacy.h"
#include "core/log.h"
#include "core/logcore.h"
#include "core/version.h"

static Config g_config;

const Config &GetConfig()
{
	return g_config;
}

void LoadConfig()
{
	LegacyMigrate();
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) {
		strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	}
	char buf[32] = {};

	g_config.renderEnabled = GetPrivateProfileIntA("Render", "Enabled", 1, path) != 0;
	g_config.vsync = GetPrivateProfileIntA("Render", "VSync", 1, path) != 0;
	g_config.movieVolume = static_cast<int>(GetPrivateProfileIntA("Sound", "MovieVolume", 50, path));
	if (g_config.movieVolume < 0 || g_config.movieVolume > 100) g_config.movieVolume = 50;
	g_config.movieMute = GetPrivateProfileIntA("Sound", "MovieMute", 0, path) != 0;
	g_config.largeAddress = true;
	g_config.brightness = GetPrivateProfileIntA("Render", "Brightness", 100, path);
	if (g_config.brightness < 50 || g_config.brightness > 200) {
		g_config.brightness = 100;
	}
	g_config.fakeModeChange = GetPrivateProfileIntA("Render", "FakeModeChange", 1, path) != 0;

	GetPrivateProfileStringA("Display", "Mode", "fullscreen", buf, sizeof(buf), path);
	g_config.windowed = _stricmp(buf, "windowed") == 0;
	g_config.windowW = GetPrivateProfileIntA("Display", "WindowWidth", 1600, path);
	g_config.windowH = GetPrivateProfileIntA("Display", "WindowHeight", 900, path);
	g_config.trapMouse = GetPrivateProfileIntA("Display", "TrapMouse", 1, path) != 0;
	g_config.monitor = (std::max)(0, (std::min)(15, static_cast<int>(GetPrivateProfileIntA("Display", "Monitor", 0, path))));
	GetPrivateProfileStringA("Display", "Resolution", "", buf, sizeof(buf), path);
	int resW = 0, resH = 0;
	if (sscanf_s(buf, "%dx%d", &resW, &resH) == 2 && resW >= 640 && resH >= 480 && resW <= 8192 && resH <= 8192) {
		g_config.resolutionW = resW;
		g_config.resolutionH = resH;
	}
	GetPrivateProfileStringA("Display", "ResolutionScreen", "", buf, sizeof(buf), path);
	if (sscanf_s(buf, "%dx%d", &resW, &resH) == 2 && resW > 0 && resH > 0 && resW <= 16384 && resH <= 16384) {
		g_config.resolutionScreenW = resW;
		g_config.resolutionScreenH = resH;
	}
	g_config.extendedView = GetPrivateProfileIntA("Display", "ExtendedView", 0, path) != 0;
	g_config.showPerf = GetPrivateProfileIntA("Display", "ShowPerformance", 0, path) != 0;
	g_config.showNet = GetPrivateProfileIntA("Display", "ShowNetwork", 0, path) != 0;
	g_config.panelX = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "PanelX", 1000, path))));
	g_config.panelY = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "PanelY", 0, path))));
	g_config.panelCompact = GetPrivateProfileIntA("Display", "PanelCompact", 0, path) != 0;
	g_config.panelOpacity = (std::max)(30, (std::min)(100, static_cast<int>(GetPrivateProfileIntA("Display", "PanelOpacity", 90, path))));
	g_config.dpiAware = GetPrivateProfileIntA("Display", "DpiAware", 1, path) != 0;
	g_config.muteInBackground = GetPrivateProfileIntA("Sound", "MuteInBackground", 1, path) != 0;
	g_config.modernAudio = GetPrivateProfileIntA("Sound", "ModernAudio", 1, path) != 0;
	g_config.fixVoices = GetPrivateProfileIntA("Sound", "FixVoices", 1, path) != 0;
	g_config.onlineAlerts = GetPrivateProfileIntA("Sound", "OnlineAlerts", 1, path) != 0;

	GetPrivateProfileStringA("Game", "ScrollSpeed", "1.0", buf, sizeof(buf), path);
	g_config.scrollSpeed = static_cast<float>(atof(buf));
	if (g_config.scrollSpeed < 0.25f || g_config.scrollSpeed > 8.0f) {
		g_config.scrollSpeed = 1.0f;
	}
	g_config.autosave = GetPrivateProfileIntA("Game", "Autosave", 1, path) != 0;
	g_config.autosaveMinutes = GetPrivateProfileIntA("Game", "AutosaveMinutes", 10, path);
	if (g_config.autosaveMinutes < 1 || g_config.autosaveMinutes > 240) {
		g_config.autosaveMinutes = 10;
	}
	g_config.autosaveOnExit = GetPrivateProfileIntA("Game", "AutosaveOnExit", 0, path) != 0;
	g_config.autosaveSlots = GetPrivateProfileIntA("Game", "AutosaveSlots", 3, path);
	if (g_config.autosaveSlots < 1 || g_config.autosaveSlots > 20) {
		g_config.autosaveSlots = 3;
	}
	g_config.crashFix = GetPrivateProfileIntA("Game", "CrashFix", 1, path) != 0;
	g_config.smoothScroll = true;

	g_config.modOverrides = GetPrivateProfileIntA("Mods", "Override", 1, path) != 0;
	g_config.logFiles = GetPrivateProfileIntA("Debug", "LogFiles", 0, path) != 0;
	g_config.rejoinTrace = GetPrivateProfileIntA("Debug", "RejoinTrace", IsBetaVersion(ModVersionText()) ? 1 : 0, path) != 0;
	GetPrivateProfileStringA("Online", "RejoinMode", "reload", buf, sizeof(buf), path);
	g_config.rejoinReload = _stricmp(buf, "hot") != 0;
	char dump[1024] = {};
	GetPrivateProfileStringA("Debug", "Dump", "", dump, sizeof(dump), path);
	g_config.dumpFiles = dump;
	char dumpStart[2048] = {};
	GetPrivateProfileStringA("Debug", "DumpAtStartup", "", dumpStart, sizeof(dumpStart), path);
	g_config.dumpAtStartup = dumpStart;

	LogI("boot", "config: Render(Enabled=%d VSync=%d Brightness=%d FakeModeChange=%d) Display(Mode=%s "
		"%dx%d TrapMouse=%d ExtendedView=%d) Game(ScrollSpeed=%.2f Autosave=%d every %d min, %d slots) Online(RejoinMode=%s)",
		g_config.renderEnabled, g_config.vsync, g_config.brightness, g_config.fakeModeChange,
		g_config.windowed ? "windowed" : "fullscreen", g_config.windowW, g_config.windowH, g_config.trapMouse,
		g_config.extendedView, g_config.scrollSpeed, g_config.autosave, g_config.autosaveMinutes,
		g_config.autosaveSlots, g_config.rejoinReload ? "reload" : "hot");
}

Config &MutableConfig()
{
	return g_config;
}

void SaveConfig()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) {
		strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	}
	WritePrivateProfileStringA("Display", "Mode", g_config.windowed ? "windowed" : "fullscreen", path);
	WritePrivateProfileStringA("Display", "TrapMouse", g_config.trapMouse ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "Monitor", std::to_string(g_config.monitor).c_str(), path);
	if (g_config.resolutionW > 0) {
		WritePrivateProfileStringA("Display", "Resolution", (std::to_string(g_config.resolutionW) + "x" + std::to_string(g_config.resolutionH)).c_str(), path);
	}
	if (g_config.resolutionScreenW > 0) {
		WritePrivateProfileStringA("Display", "ResolutionScreen",
			(std::to_string(g_config.resolutionScreenW) + "x" + std::to_string(g_config.resolutionScreenH)).c_str(), path);
	}
	WritePrivateProfileStringA("Render", "VSync", g_config.vsync ? "1" : "0", path);
	WritePrivateProfileStringA("Render", "Scaling", nullptr, path);
	WritePrivateProfileStringA("Render", "Filter", nullptr, path);
	WritePrivateProfileStringA("Render", "Brightness", std::to_string(g_config.brightness).c_str(), path);
	WritePrivateProfileStringA("Sound", "MovieVolume", std::to_string(g_config.movieVolume).c_str(), path);
	WritePrivateProfileStringA("Sound", "MovieMute", g_config.movieMute ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "Use4GB", g_config.largeAddress ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "Autosave", g_config.autosave ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "AutosaveMinutes", std::to_string(g_config.autosaveMinutes).c_str(), path);
	WritePrivateProfileStringA("Game", "AutosaveSlots", std::to_string(g_config.autosaveSlots).c_str(), path);
	WritePrivateProfileStringA("Game", "AutosaveOnExit", g_config.autosaveOnExit ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "SmoothScroll", g_config.smoothScroll ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ShowPerformance", g_config.showPerf ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ShowNetwork", g_config.showNet ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "PanelX", std::to_string(g_config.panelX).c_str(), path);
	WritePrivateProfileStringA("Display", "PanelY", std::to_string(g_config.panelY).c_str(), path);
	WritePrivateProfileStringA("Display", "PanelCompact", g_config.panelCompact ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "PanelOpacity", std::to_string(g_config.panelOpacity).c_str(), path);
	WritePrivateProfileStringA("Sound", "MuteInBackground", g_config.muteInBackground ? "1" : "0", path);
	WritePrivateProfileStringA("Sound", "ModernAudio", g_config.modernAudio ? "1" : "0", path);
	WritePrivateProfileStringA("Sound", "OnlineAlerts", g_config.onlineAlerts ? "1" : "0", path);
}
