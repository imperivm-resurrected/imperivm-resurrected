// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <wincrypt.h>

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

namespace {

std::string IniPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	return path;
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

std::string ReadIniString(const char *section, const char *key, const std::string &path)
{
	char buf[2048] = {};
	GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
	return buf;
}

}

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
	g_config.fpsLimit = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Render", "FpsLimit", 0, path))));
	GetPrivateProfileStringA("Render", "ImageScaling", "sharp", buf, sizeof(buf), path);
	g_config.imageScaling = _stricmp(buf, "smooth") == 0 ? 1 : _stricmp(buf, "exact") == 0 ? 2 : 0;
	g_config.showPerf = GetPrivateProfileIntA("Display", "ShowPerformance", 0, path) != 0;
	g_config.showNet = GetPrivateProfileIntA("Display", "ShowNetwork", 0, path) != 0;
	g_config.panelX = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "PanelX", 1000, path))));
	g_config.panelY = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "PanelY", 0, path))));
	g_config.panelCompact = GetPrivateProfileIntA("Display", "PanelCompact", 0, path) != 0;
	g_config.chatX = (std::max)(-1, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "ChatX", -1, path))));
	g_config.chatY = (std::max)(-1, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Display", "ChatY", -1, path))));
	if (g_config.chatX < 0 || g_config.chatY < 0) g_config.chatX = g_config.chatY = -1;
	g_config.panelOpacity = (std::max)(30, (std::min)(100, static_cast<int>(GetPrivateProfileIntA("Display", "PanelOpacity", 90, path))));
	g_config.dpiAware = GetPrivateProfileIntA("Display", "DpiAware", 1, path) != 0;
	g_config.muteInBackground = GetPrivateProfileIntA("Sound", "MuteInBackground", 1, path) != 0;
	g_config.modernAudio = GetPrivateProfileIntA("Sound", "ModernAudio", 1, path) != 0;
	g_config.fixVoices = GetPrivateProfileIntA("Sound", "FixVoices", 1, path) != 0;
	g_config.onlineAlerts = GetPrivateProfileIntA("Sound", "OnlineAlerts", 1, path) != 0;
	g_config.masterVolume = (std::max)(0, (std::min)(100, static_cast<int>(GetPrivateProfileIntA("Sound", "MasterVolume", 100, path))));
	g_config.natureVolume = static_cast<int>(GetPrivateProfileIntA("Sound", "NatureVolume", -1, path));
	if (g_config.natureVolume < 1 || g_config.natureVolume > 100) g_config.natureVolume = -1;
	g_config.conversationVolume = static_cast<int>(GetPrivateProfileIntA("Sound", "ConversationVolume", 100, path));
	if (g_config.conversationVolume < 1 || g_config.conversationVolume > 100) g_config.conversationVolume = 100;
	char device[512] = {};
	GetPrivateProfileStringA("Sound", "OutputDevice", "", device, sizeof(device), path);
	g_config.outputDevice = device;

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
	g_config.smoothScroll = GetPrivateProfileIntA("Game", "SmoothScroll", 1, path) != 0;
	g_config.healthBarOwners = (std::max)(0, (std::min)(3, static_cast<int>(GetPrivateProfileIntA("Game", "HealthBarOwners", 0, path))));
	g_config.healthBarHeroesOnly = GetPrivateProfileIntA("Game", "HealthBarHeroesOnly", 0, path) != 0;
	g_config.allyColors = GetPrivateProfileIntA("Game", "AllyColors", 0, path) != 0;
	g_config.pauseInBackground = GetPrivateProfileIntA("Game", "PauseInBackground", 0, path) != 0;
	g_config.zoomExtras = GetPrivateProfileIntA("Map", "ZoomExtras", 1, path) != 0;
	g_config.zoomFog = GetPrivateProfileIntA("Map", "FogVisible", 1, path) != 0;
	g_config.zoomRefreshMs = (std::max)(0, (std::min)(1000, static_cast<int>(GetPrivateProfileIntA("Map", "RefreshMs", 16, path))));

	g_config.modOverrides = GetPrivateProfileIntA("Mods", "Override", 1, path) != 0;
	GetPrivateProfileStringA("Debug", "LogLevel", "info", buf, sizeof(buf), path);
	if (!ParseLogLevel(buf, g_config.logLevel)) g_config.logLevel = kLogInfo;
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
	g_config.leagueUrl = ReadIniString("League", "Url", path);
	g_config.leagueToken = RevealSecret(ReadIniString("League", "Token", path));
	g_config.leagueSteamId = ReadIniString("League", "SteamId", path);
	g_config.leagueName = ReadIniString("League", "Name", path);
	g_config.leagueOfferLink = GetPrivateProfileIntA("League", "OfferLink", 1, path) != 0;
	g_config.chatBadges = GetPrivateProfileIntA("League", "ChatBadges", 1, path) != 0;
	g_config.matchFoundSound = GetPrivateProfileIntA("League", "MatchFoundSound", 1, path) != 0;
	if (g_config.leagueToken.empty() && !ReadIniString("League", "Token", path).empty()) {
		LogW("boot", "config: the league token can't be read on this Windows user; the account has to be linked again");
	}

	LogI("boot", "config: Render(Enabled=%d VSync=%d Brightness=%d FakeModeChange=%d) Display(Mode=%s "
		"Resolution=%dx%d TrapMouse=%d ExtendedView=%d) Game(ScrollSpeed=%.2f Autosave=%d every %d min, %d slots) Online(RejoinMode=%s)",
		g_config.renderEnabled, g_config.vsync, g_config.brightness, g_config.fakeModeChange,
		g_config.windowed ? "windowed" : "fullscreen", g_config.resolutionW, g_config.resolutionH, g_config.trapMouse,
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
	// In a window the resolution is the window's size; the old keys are left out.
	WritePrivateProfileStringA("Display", "WindowWidth", nullptr, path);
	WritePrivateProfileStringA("Display", "WindowHeight", nullptr, path);
	WritePrivateProfileStringA("Display", "TrapMouse", g_config.trapMouse ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ExtendedView", g_config.extendedView ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "Monitor", std::to_string(g_config.monitor).c_str(), path);
	if (g_config.resolutionW > 0) {
		WritePrivateProfileStringA("Display", "Resolution", (std::to_string(g_config.resolutionW) + "x" + std::to_string(g_config.resolutionH)).c_str(), path);
	}
	if (g_config.resolutionScreenW > 0) {
		WritePrivateProfileStringA("Display", "ResolutionScreen",
			(std::to_string(g_config.resolutionScreenW) + "x" + std::to_string(g_config.resolutionScreenH)).c_str(), path);
	}
	WritePrivateProfileStringA("Render", "VSync", g_config.vsync ? "1" : "0", path);
	WritePrivateProfileStringA("Render", "FpsLimit", std::to_string(g_config.fpsLimit).c_str(), path);
	static const char *const kScalingNames[] = {"sharp", "smooth", "exact"};
	WritePrivateProfileStringA("Render", "ImageScaling", kScalingNames[g_config.imageScaling >= 0 && g_config.imageScaling < 3 ? g_config.imageScaling : 0], path);
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
	WritePrivateProfileStringA("Game", "HealthBarOwners", std::to_string(g_config.healthBarOwners).c_str(), path);
	WritePrivateProfileStringA("Game", "HealthBarHeroesOnly", g_config.healthBarHeroesOnly ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "AllyColors", g_config.allyColors ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "PauseInBackground", g_config.pauseInBackground ? "1" : "0", path);
	WritePrivateProfileStringA("Game", "CrashFix", g_config.crashFix ? "1" : "0", path);
	WritePrivateProfileStringA("Mods", "Override", g_config.modOverrides ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ShowPerformance", g_config.showPerf ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ShowNetwork", g_config.showNet ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "PanelX", std::to_string(g_config.panelX).c_str(), path);
	WritePrivateProfileStringA("Display", "PanelY", std::to_string(g_config.panelY).c_str(), path);
	WritePrivateProfileStringA("Display", "PanelCompact", g_config.panelCompact ? "1" : "0", path);
	WritePrivateProfileStringA("Display", "ChatX", g_config.chatX >= 0 ? std::to_string(g_config.chatX).c_str() : nullptr, path);
	WritePrivateProfileStringA("Display", "ChatY", g_config.chatY >= 0 ? std::to_string(g_config.chatY).c_str() : nullptr, path);
	WritePrivateProfileStringA("Display", "PanelOpacity", std::to_string(g_config.panelOpacity).c_str(), path);
	WritePrivateProfileStringA("Sound", "MuteInBackground", g_config.muteInBackground ? "1" : "0", path);
	WritePrivateProfileStringA("Sound", "ModernAudio", g_config.modernAudio ? "1" : "0", path);
	WritePrivateProfileStringA("Sound", "OnlineAlerts", g_config.onlineAlerts ? "1" : "0", path);
	WritePrivateProfileStringA("Sound", "MasterVolume", std::to_string(g_config.masterVolume).c_str(), path);
	// Without the key, nature sounds follow the effects volume.
	WritePrivateProfileStringA("Sound", "NatureVolume", g_config.natureVolume > 0 ? std::to_string(g_config.natureVolume).c_str() : nullptr, path);
	WritePrivateProfileStringA("Sound", "ConversationVolume", std::to_string(g_config.conversationVolume).c_str(), path);
	WritePrivateProfileStringA("Sound", "OutputDevice", g_config.outputDevice.c_str(), path);
	WritePrivateProfileStringA("League", "ChatBadges", g_config.chatBadges ? "1" : "0", path);
	WritePrivateProfileStringA("League", "MatchFoundSound", g_config.matchFoundSound ? "1" : "0", path);
	WritePrivateProfileStringA("Debug", "LogLevel", LogLevelName(g_config.logLevel), path);
}

void SaveLeagueAccount()
{
	const std::string path = IniPath();
	const bool linked = !g_config.leagueToken.empty();
	WritePrivateProfileStringA("League", "Token", linked ? ProtectSecret(g_config.leagueToken).c_str() : nullptr, path.c_str());
	WritePrivateProfileStringA("League", "SteamId", linked ? g_config.leagueSteamId.c_str() : nullptr, path.c_str());
	WritePrivateProfileStringA("League", "Name", linked ? g_config.leagueName.c_str() : nullptr, path.c_str());
}

void SaveLeagueOffer()
{
	WritePrivateProfileStringA("League", "OfferLink", g_config.leagueOfferLink ? nullptr : "0", IniPath().c_str());
}
