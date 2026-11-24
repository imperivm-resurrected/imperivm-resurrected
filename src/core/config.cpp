// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/config.h"
#include "core/legacy.h"
#include "core/log.h"
#include "core/logcore.h"
#include "core/version.h"
#include "core/winpath.h"

static Config g_config;

namespace {

std::string IniPath()
{
	return winpath::GameDir() + "\\imperivm-mod.ini";
}

// The buffers the texts are read into: a longer value is cut to one character less.
const size_t kShortText = 32, kDeviceText = 512, kDumpText = 1024, kLongText = 2048;

// imperivm-mod.ini. A missing key reads as the default given.
struct Ini {
	std::string path = IniPath();

	bool Flag(const char *section, const char *key, bool def) const { return GetPrivateProfileIntA(section, key, def ? 1 : 0, path.c_str()) != 0; }

	int Number(const char *section, const char *key, int def) const { return static_cast<int>(GetPrivateProfileIntA(section, key, def, path.c_str())); }

	int Clamped(const char *section, const char *key, int def, int low, int high) const
	{
		return (std::max)(low, (std::min)(high, Number(section, key, def)));
	}

	std::string Text(const char *section, const char *key, const char *def, size_t size) const
	{
		char buf[kLongText] = {};
		GetPrivateProfileStringA(section, key, def, buf, static_cast<DWORD>((std::min)(size, sizeof(buf))), path.c_str());
		return buf;
	}

	void Write(const char *section, const char *key, const char *value) const { WritePrivateProfileStringA(section, key, value, path.c_str()); }

	void Write(const char *section, const char *key, const std::string &value) const { Write(section, key, value.c_str()); }

	void WriteFlag(const char *section, const char *key, bool value) const { Write(section, key, value ? "1" : "0"); }

	void WriteNumber(const char *section, const char *key, int value) const { Write(section, key, std::to_string(value)); }

	void Remove(const char *section, const char *key) const { Write(section, key, nullptr); }

	// Without the key, the setting takes its default.
	void WriteNumberOrRemove(const char *section, const char *key, int value, bool keep) const
	{
		if (keep) WriteNumber(section, key, value);
		else Remove(section, key);
	}
};

std::string SizeText(int w, int h)
{
	return std::to_string(w) + "x" + std::to_string(h);
}

const char kProtectedPrefix[] = "dpapi:";

// The token is encrypted for this Windows user, so a copied imperivm-mod.ini doesn't carry a usable one. A token written by hand stays
// as it is.
std::string ProtectSecret(const std::string &plain)
{
	DATA_BLOB in = {static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
	DATA_BLOB out = {};
	if (!CryptProtectData(&in, L"Imperivm Resurrected league", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return plain;
	DWORD n = 0;
	std::string text;
	if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n) && n) {
		text.assign(n, '\0');
		if (CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &text[0], &n)) text.resize(n);
		else text.clear();
	}
	LocalFree(out.pbData);
	return text.empty() ? plain : kProtectedPrefix + text;
}

std::string RevealSecret(const std::string &stored)
{
	if (stored.compare(0, sizeof(kProtectedPrefix) - 1, kProtectedPrefix) != 0) return stored;
	const std::string text = stored.substr(sizeof(kProtectedPrefix) - 1);
	DWORD n = 0;
	if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr) || !n) return std::string();
	std::string bytes(n, '\0');
	if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE *>(&bytes[0]), &n, nullptr, nullptr)) {
		return std::string();
	}
	DATA_BLOB in = {n, reinterpret_cast<BYTE *>(&bytes[0])};
	DATA_BLOB out = {};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::string();
	std::string plain(reinterpret_cast<const char *>(out.pbData), out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

}

const Config &GetConfig()
{
	return g_config;
}

void LoadConfig()
{
	LegacyMigrate();
	const Ini ini;

	g_config.renderEnabled = ini.Flag("Render", "Enabled", true);
	g_config.vsync = ini.Flag("Render", "VSync", true);
	g_config.movieVolume = ini.Number("Sound", "MovieVolume", 50);
	if (g_config.movieVolume < 0 || g_config.movieVolume > 100) g_config.movieVolume = 50;
	g_config.movieMute = ini.Flag("Sound", "MovieMute", false);
	g_config.largeAddress = true;
	g_config.brightness = ini.Number("Render", "Brightness", 100);
	if (g_config.brightness < 50 || g_config.brightness > 200) {
		g_config.brightness = 100;
	}
	g_config.fakeModeChange = ini.Flag("Render", "FakeModeChange", true);

	g_config.windowed = _stricmp(ini.Text("Display", "Mode", "fullscreen", kShortText).c_str(), "windowed") == 0;
	g_config.trapMouse = ini.Flag("Display", "TrapMouse", true);
	g_config.monitor = ini.Clamped("Display", "Monitor", 0, 0, 15);
	int resW = 0, resH = 0;
	if (sscanf_s(ini.Text("Display", "Resolution", "", kShortText).c_str(), "%dx%d", &resW, &resH) == 2 && resW >= 640 && resH >= 480 && resW <= 8192 &&
		resH <= 8192) {
		g_config.resolutionW = resW;
		g_config.resolutionH = resH;
	}
	if (sscanf_s(ini.Text("Display", "ResolutionScreen", "", kShortText).c_str(), "%dx%d", &resW, &resH) == 2 && resW > 0 && resH > 0 && resW <= 16384 &&
		resH <= 16384) {
		g_config.resolutionScreenW = resW;
		g_config.resolutionScreenH = resH;
	}
	const std::string windowSize = ini.Text("Display", "WindowSize", "", kShortText);
	if (sscanf_s(windowSize.c_str(), "%dx%d", &resW, &resH) == 2 && resW >= 640 && resH >= 480 && resW <= 8192 && resH <= 8192) {
		g_config.windowW = resW;
		g_config.windowH = resH;
	}
	if (sscanf_s(ini.Text("Display", "WindowScreen", "", kShortText).c_str(), "%dx%d", &resW, &resH) == 2 && resW > 0 && resH > 0 && resW <= 16384 &&
		resH <= 16384) {
		g_config.windowScreenW = resW;
		g_config.windowScreenH = resH;
	}
	// Before each mode kept its own size, Resolution was the size of the mode in use: a window's goes to its own key and full screen
	// starts at the screen's own size.
	if (windowSize.empty() && g_config.windowed && g_config.resolutionW > 0) {
		ConfigSetSavedSize(g_config, true, ConfigSavedSize(g_config, false));
		ConfigSetSavedSize(g_config, false, SavedSize());
	}
	g_config.extendedView = ini.Flag("Display", "ExtendedView", false);
	g_config.fpsLimit = ini.Clamped("Render", "FpsLimit", 0, 0, 1000);
	const std::string scaling = ini.Text("Render", "ImageScaling", "sharp", kShortText);
	g_config.imageScaling = _stricmp(scaling.c_str(), "smooth") == 0 ? 1 : _stricmp(scaling.c_str(), "exact") == 0 ? 2 : 0;
	g_config.showPerf = ini.Flag("Display", "ShowPerformance", false);
	g_config.showNet = ini.Flag("Display", "ShowNetwork", false);
	g_config.panelX = ini.Clamped("Display", "PanelX", 1000, 0, 1000);
	g_config.panelY = ini.Clamped("Display", "PanelY", 0, 0, 1000);
	g_config.panelCompact = ini.Flag("Display", "PanelCompact", true);
	g_config.perfGraphFrame = ini.Flag("Perf", "GraphFrameTime", true);
	g_config.perfGraphCpu = ini.Flag("Perf", "GraphCpu", false);
	g_config.perfGraphMemory = ini.Flag("Perf", "GraphMemory", false);
	g_config.perfGraphPing = ini.Flag("Perf", "GraphPing", true);
	g_config.perfShowLows = ini.Flag("Perf", "ShowLows", true);
	g_config.perfShowCpu = ini.Flag("Perf", "ShowCpu", true);
	g_config.perfShowMemory = ini.Flag("Perf", "ShowMemory", true);
	g_config.perfShowSim = ini.Flag("Perf", "ShowSimulation", true);
	g_config.perfWindow = ini.Clamped("Perf", "Window", 30, 10, 60);
	g_config.perfScale = ini.Clamped("Perf", "Scale", 100, 75, 200);
	g_config.perfRefresh = ini.Clamped("Perf", "Refresh", 500, 100, 2000);
	g_config.perfCapture = ini.Clamped("Perf", "CaptureSeconds", 30, 5, 600);
	g_config.chatX = ini.Clamped("Display", "ChatX", -1, -1, 1000);
	g_config.chatY = ini.Clamped("Display", "ChatY", -1, -1, 1000);
	if (g_config.chatX < 0 || g_config.chatY < 0) g_config.chatX = g_config.chatY = -1;
	g_config.panelOpacity = ini.Clamped("Display", "PanelOpacity", 90, 30, 100);
	g_config.dpiAware = ini.Flag("Display", "DpiAware", true);
	g_config.muteInBackground = ini.Flag("Sound", "MuteInBackground", true);
	g_config.modernAudio = ini.Flag("Sound", "ModernAudio", true);
	g_config.fixVoices = ini.Flag("Sound", "FixVoices", true);
	g_config.onlineAlerts = ini.Flag("Sound", "OnlineAlerts", true);
	g_config.alertVolume = ini.Clamped("Sound", "AlertVolume", 100, 1, 100);
	g_config.masterVolume = ini.Clamped("Sound", "MasterVolume", 100, 0, 100);
	g_config.natureVolume = ini.Number("Sound", "NatureVolume", -1);
	if (g_config.natureVolume < 1 || g_config.natureVolume > 100) g_config.natureVolume = -1;
	g_config.conversationVolume = ini.Number("Sound", "ConversationVolume", 100);
	if (g_config.conversationVolume < 1 || g_config.conversationVolume > 100) g_config.conversationVolume = 100;
	g_config.outputDevice = ini.Text("Sound", "OutputDevice", "", kDeviceText);

	g_config.scrollSpeed = static_cast<float>(atof(ini.Text("Game", "ScrollSpeed", "1.0", kShortText).c_str()));
	if (g_config.scrollSpeed < 0.25f || g_config.scrollSpeed > 8.0f) {
		g_config.scrollSpeed = 1.0f;
	}
	g_config.autosave = ini.Flag("Game", "Autosave", true);
	g_config.autosaveMinutes = ini.Number("Game", "AutosaveMinutes", 10);
	if (g_config.autosaveMinutes < 1 || g_config.autosaveMinutes > 240) {
		g_config.autosaveMinutes = 10;
	}
	g_config.autosaveOnExit = ini.Flag("Game", "AutosaveOnExit", false);
	g_config.autosaveSlots = ini.Number("Game", "AutosaveSlots", 3);
	if (g_config.autosaveSlots < 1 || g_config.autosaveSlots > 20) {
		g_config.autosaveSlots = 3;
	}
	g_config.crashFix = ini.Flag("Game", "CrashFix", true);
	g_config.smoothScroll = ini.Flag("Game", "SmoothScroll", true);
	g_config.healthBarOwners = ini.Clamped("Game", "HealthBarOwners", 0, 0, 3);
	g_config.healthBarHeroesOnly = ini.Flag("Game", "HealthBarHeroesOnly", false);
	g_config.allyColors = ini.Flag("Game", "AllyColors", false);
	g_config.allyPalette = ini.Clamped("Game", "AllyPalette", 0, 0, 2);
	{
		static const char *const kKeys[3] = {"AllySelf", "AllyAllies", "AllyEnemies"};
		const uint32_t *preset = kAllyPresets[g_config.allyPalette == 1 ? 1 : 0];
		for (int i = 0; i < 3; i++) g_config.allyRgb[i] = static_cast<uint32_t>(ini.Clamped("Game", kKeys[i], static_cast<int>(preset[i]), 0, 0xffffff));
	}
	g_config.pauseInBackground = ini.Flag("Game", "PauseInBackground", false);
	g_config.respawnTimers = ini.Flag("Game", "RespawnTimers", true);
	g_config.zoomExtras = ini.Flag("Map", "ZoomExtras", true);
	g_config.zoomFog = ini.Flag("Map", "FogVisible", true);
	g_config.zoomRefreshMs = ini.Clamped("Map", "RefreshMs", 16, 0, 1000);

	g_config.modOverrides = ini.Flag("Mods", "Override", true);
	if (!ParseLogLevel(ini.Text("Debug", "LogLevel", "info", kShortText), g_config.logLevel)) g_config.logLevel = kLogInfo;
	g_config.logFiles = ini.Flag("Debug", "LogFiles", false);
	g_config.rejoinTrace = ini.Flag("Debug", "RejoinTrace", IsBetaVersion(ModVersionText()));
	g_config.rejoinReload = _stricmp(ini.Text("Online", "RejoinMode", "reload", kShortText).c_str(), "hot") != 0;
	g_config.dumpFiles = ini.Text("Debug", "Dump", "", kDumpText);
	g_config.dumpAtStartup = ini.Text("Debug", "DumpAtStartup", "", kLongText);
	g_config.leagueUrl = ini.Text("League", "Url", "", kLongText);
	const std::string storedToken = ini.Text("League", "Token", "", kLongText);
	g_config.leagueToken = RevealSecret(storedToken);
	g_config.leagueSteamId = ini.Text("League", "SteamId", "", kLongText);
	g_config.leagueName = ini.Text("League", "Name", "", kLongText);
	g_config.leagueOfferLink = ini.Flag("League", "OfferLink", true);
	g_config.chatBadges = ini.Flag("League", "ChatBadges", true);
	g_config.matchFoundSound = ini.Flag("League", "MatchFoundSound", true);
	g_config.alertFront = ini.Flag("League", "AlertFront", true);
	if (g_config.leagueToken.empty() && !storedToken.empty()) {
		LogW("boot", "config: the league token can't be read on this Windows user; the account has to be linked again");
	}

	LogI("boot", "config: Render(Enabled=%d VSync=%d Brightness=%d FakeModeChange=%d) Display(Mode=%s "
		"Resolution=%dx%d TrapMouse=%d ExtendedView=%d) Game(ScrollSpeed=%.2f Autosave=%d every %d min, %d slots) Online(RejoinMode=%s)",
		g_config.renderEnabled, g_config.vsync, g_config.brightness, g_config.fakeModeChange,
		g_config.windowed ? "windowed" : "fullscreen", g_config.resolutionW, g_config.resolutionH, g_config.trapMouse,
		g_config.extendedView, g_config.scrollSpeed, g_config.autosave, g_config.autosaveMinutes,
		g_config.autosaveSlots, g_config.rejoinReload ? "reload" : "hot");
}

SavedSize ConfigSavedSize(const Config &c, bool windowed)
{
	SavedSize s;
	s.w = windowed ? c.windowW : c.resolutionW;
	s.h = windowed ? c.windowH : c.resolutionH;
	s.screenW = windowed ? c.windowScreenW : c.resolutionScreenW;
	s.screenH = windowed ? c.windowScreenH : c.resolutionScreenH;
	return s;
}

void ConfigSetSavedSize(Config &c, bool windowed, const SavedSize &s)
{
	(windowed ? c.windowW : c.resolutionW) = s.w;
	(windowed ? c.windowH : c.resolutionH) = s.h;
	(windowed ? c.windowScreenW : c.resolutionScreenW) = s.screenW;
	(windowed ? c.windowScreenH : c.resolutionScreenH) = s.screenH;
}

Config &MutableConfig()
{
	return g_config;
}

void SaveConfig()
{
	const Ini ini;
	ini.Write("Display", "Mode", g_config.windowed ? "windowed" : "fullscreen");
	// In a window the resolution is the window's size; the old keys are left out.
	ini.Remove("Display", "WindowWidth");
	ini.Remove("Display", "WindowHeight");
	ini.WriteFlag("Display", "TrapMouse", g_config.trapMouse);
	ini.WriteFlag("Display", "ExtendedView", g_config.extendedView);
	ini.WriteNumber("Display", "Monitor", g_config.monitor);
	auto writeSize = [&](const char *key, int w, int h) {
		if (w > 0) ini.Write("Display", key, SizeText(w, h));
		else ini.Remove("Display", key);
	};
	writeSize("Resolution", g_config.resolutionW, g_config.resolutionH);
	writeSize("ResolutionScreen", g_config.resolutionScreenW, g_config.resolutionScreenH);
	writeSize("WindowSize", g_config.windowW, g_config.windowH);
	writeSize("WindowScreen", g_config.windowScreenW, g_config.windowScreenH);
	ini.WriteFlag("Render", "VSync", g_config.vsync);
	ini.WriteNumber("Render", "FpsLimit", g_config.fpsLimit);
	static const char *const kScalingNames[] = {"sharp", "smooth", "exact"};
	ini.Write("Render", "ImageScaling", kScalingNames[g_config.imageScaling >= 0 && g_config.imageScaling < 3 ? g_config.imageScaling : 0]);
	ini.Remove("Render", "Scaling");
	ini.Remove("Render", "Filter");
	ini.WriteNumber("Render", "Brightness", g_config.brightness);
	ini.WriteNumber("Sound", "MovieVolume", g_config.movieVolume);
	ini.WriteFlag("Sound", "MovieMute", g_config.movieMute);
	ini.WriteFlag("Game", "Use4GB", g_config.largeAddress);
	ini.WriteFlag("Game", "Autosave", g_config.autosave);
	ini.WriteNumber("Game", "AutosaveMinutes", g_config.autosaveMinutes);
	ini.WriteNumber("Game", "AutosaveSlots", g_config.autosaveSlots);
	ini.WriteFlag("Game", "AutosaveOnExit", g_config.autosaveOnExit);
	ini.WriteFlag("Game", "SmoothScroll", g_config.smoothScroll);
	ini.WriteNumber("Game", "HealthBarOwners", g_config.healthBarOwners);
	ini.WriteFlag("Game", "HealthBarHeroesOnly", g_config.healthBarHeroesOnly);
	ini.WriteFlag("Game", "AllyColors", g_config.allyColors);
	ini.WriteNumber("Game", "AllyPalette", g_config.allyPalette);
	ini.WriteNumber("Game", "AllySelf", static_cast<int>(g_config.allyRgb[0]));
	ini.WriteNumber("Game", "AllyAllies", static_cast<int>(g_config.allyRgb[1]));
	ini.WriteNumber("Game", "AllyEnemies", static_cast<int>(g_config.allyRgb[2]));
	ini.WriteFlag("Game", "PauseInBackground", g_config.pauseInBackground);
	ini.WriteFlag("Game", "RespawnTimers", g_config.respawnTimers);
	ini.WriteFlag("Game", "CrashFix", g_config.crashFix);
	ini.WriteFlag("Mods", "Override", g_config.modOverrides);
	ini.WriteFlag("Display", "ShowPerformance", g_config.showPerf);
	ini.WriteFlag("Display", "ShowNetwork", g_config.showNet);
	ini.WriteNumber("Display", "PanelX", g_config.panelX);
	ini.WriteNumber("Display", "PanelY", g_config.panelY);
	ini.WriteFlag("Display", "PanelCompact", g_config.panelCompact);
	ini.WriteFlag("Perf", "GraphFrameTime", g_config.perfGraphFrame);
	ini.WriteFlag("Perf", "GraphCpu", g_config.perfGraphCpu);
	ini.WriteFlag("Perf", "GraphMemory", g_config.perfGraphMemory);
	ini.WriteFlag("Perf", "GraphPing", g_config.perfGraphPing);
	ini.WriteFlag("Perf", "ShowLows", g_config.perfShowLows);
	ini.WriteFlag("Perf", "ShowCpu", g_config.perfShowCpu);
	ini.WriteFlag("Perf", "ShowMemory", g_config.perfShowMemory);
	ini.WriteFlag("Perf", "ShowSimulation", g_config.perfShowSim);
	ini.WriteNumber("Perf", "Window", g_config.perfWindow);
	ini.WriteNumber("Perf", "Scale", g_config.perfScale);
	ini.WriteNumber("Perf", "Refresh", g_config.perfRefresh);
	ini.WriteNumber("Perf", "CaptureSeconds", g_config.perfCapture);
	ini.WriteNumberOrRemove("Display", "ChatX", g_config.chatX, g_config.chatX >= 0);
	ini.WriteNumberOrRemove("Display", "ChatY", g_config.chatY, g_config.chatY >= 0);
	ini.WriteNumber("Display", "PanelOpacity", g_config.panelOpacity);
	ini.WriteFlag("Sound", "MuteInBackground", g_config.muteInBackground);
	ini.WriteFlag("Sound", "ModernAudio", g_config.modernAudio);
	ini.WriteFlag("Sound", "OnlineAlerts", g_config.onlineAlerts);
	ini.WriteNumber("Sound", "AlertVolume", g_config.alertVolume);
	ini.WriteNumber("Sound", "MasterVolume", g_config.masterVolume);
	// Without the key, nature sounds follow the effects volume.
	ini.WriteNumberOrRemove("Sound", "NatureVolume", g_config.natureVolume, g_config.natureVolume > 0);
	ini.WriteNumber("Sound", "ConversationVolume", g_config.conversationVolume);
	ini.Write("Sound", "OutputDevice", g_config.outputDevice);
	ini.WriteFlag("League", "ChatBadges", g_config.chatBadges);
	ini.WriteFlag("League", "MatchFoundSound", g_config.matchFoundSound);
	ini.WriteFlag("League", "AlertFront", g_config.alertFront);
	ini.Write("Debug", "LogLevel", LogLevelName(g_config.logLevel));
}

void SaveLeagueAccount()
{
	const Ini ini;
	const bool linked = !g_config.leagueToken.empty();
	ini.Write("League", "Token", linked ? ProtectSecret(g_config.leagueToken).c_str() : nullptr);
	ini.Write("League", "SteamId", linked ? g_config.leagueSteamId.c_str() : nullptr);
	ini.Write("League", "Name", linked ? g_config.leagueName.c_str() : nullptr);
}

void SaveHealthBarOwners()
{
	Ini().WriteNumber("Game", "HealthBarOwners", g_config.healthBarOwners);
}

void SaveLeagueOffer()
{
	Ini().Write("League", "OfferLink", g_config.leagueOfferLink ? nullptr : "0");
}
