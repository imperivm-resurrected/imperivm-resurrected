// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "game/autosave.h"
#include "game/gamelayout.h"
#include "core/config.h"
#include "game/gameroom.h"
#include "core/log.h"
#include "mods/savelib.h"
#include "game/netlink.h"
#include "game/gamehook.h"
#include "hooks/tweaks.h"
#include "i18n/tr.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/world.h"
#include "core/uiport.h"
#include "core/winpath.h"

namespace {
typedef void *(__cdecl *ProcessRequests_t)();
typedef void *(__cdecl *BuildPath_t)(void *out);
typedef void *(__fastcall *Assign_t)(void *self, void *edx, const char *text, unsigned len);
typedef void(__cdecl *Free_t)(void *);
typedef int(__cdecl *IsDemo_t)();

ProcessRequests_t g_origProcess = nullptr;
BuildPath_t g_origBuildPath = nullptr;
int g_inSession = 0;
DWORD g_lastTick = 0;
DWORD g_playedMs = 0;
// The file of the last autosave asked of the game, lowercase with forward slashes: the save written there is an autosave whatever
// its name, which the player's language decides.
std::mutex g_autosavePathLock;
std::string g_autosavePath;

std::string SavePathKey(const std::string &path)
{
	std::string key = path;
	for (char &c : key) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return key;
}

void *__cdecl HookProcessRequests()
{
	g_inSession++;
	g_playedMs = 0;
	SaveSessionBegin();
	GameRulesOnGameStart();
	LogI("autosave", "game running (every %d min, %d slots%s)", GetConfig().autosaveMinutes,
		GetConfig().autosaveSlots, GetConfig().autosave ? "" : ", DISABLED");
	void *r = g_origProcess();
	g_inSession--;
	GameRulesOnGameEnd();
	return r;
}

std::string ReadGameString(const char *s)
{
	return layout::StringValue(s, 4096);
}

using winpath::GameDir;

void *__cdecl HookBuildQuickSavePath(void *out)
{
	void *r = g_origBuildPath(out);
	int *game = *reinterpret_cast<int **>(addr::kGame);
	if (!out || !game || *reinterpret_cast<uintptr_t *>(game) != addr::kMultiplayerVtable) return r;
	const std::string path = ReadGameString(static_cast<const char *>(out));
	const size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos || path.size() < 4 || _stricmp(path.c_str() + path.size() - 4, ".snr") != 0) return r;
	std::string dir = GameDir() + "\\" + path.substr(0, slash) + "\\" + kSaveOnlineFolder;
	for (char &c : dir) {
		if (c == '/') c = '\\';
	}
	CreateDirectoryA(dir.c_str(), nullptr);
	const std::string online = path.substr(0, slash + 1) + kSaveOnlineFolder + "/" + path.substr(slash + 1);
	reinterpret_cast<Assign_t>(addr::kStringAssign)(out, nullptr, online.c_str(), static_cast<unsigned>(online.size()));
	return r;
}

std::string ProfileOfDir(const std::string &dir)
{
	std::string profile = dir;
	while (!profile.empty() && (profile.back() == '/' || profile.back() == '\\')) profile.pop_back();
	size_t pslash = profile.find_last_of("/\\");
	if (pslash != std::string::npos && _stricmp(profile.c_str() + pslash + 1, kSaveOnlineFolder) == 0) {
		profile.resize(pslash);
		pslash = profile.find_last_of("/\\");
	}
	return pslash != std::string::npos ? profile.substr(pslash + 1) : profile;
}

bool QuickSaveDir(std::string &dir, std::string &suffix)
{
	alignas(4) char s[0x1c] = {};
	reinterpret_cast<BuildPath_t>(addr::kBuildQuickSavePath)(s);
	const std::string quick = ReadGameString(s);
	if (*reinterpret_cast<unsigned *>(s + 0x18) >= 0x10) {
		reinterpret_cast<Free_t>(addr::kGameFree)(*reinterpret_cast<void **>(s + 4));
	}
	const size_t slash = quick.find_last_of("/\\");
	if (quick.empty() || slash == std::string::npos) return false;
	dir = quick.substr(0, slash + 1);
	const std::string file = quick.substr(slash + 1);
	suffix.clear();
	const size_t dash = file.find('-');
	if (file.size() > 4 && file.compare(file.size() - 4, 4, ".snr") == 0) suffix = ".snr";
	else if (dash != std::string::npos) suffix = file.substr(dash);
	return true;
}

void RequestSave(const std::string &path)
{
	reinterpret_cast<Assign_t>(addr::kStringAssign)(reinterpret_cast<void *>(addr::kSaveName), nullptr, path.c_str(),
		static_cast<unsigned>(path.size()));
	*reinterpret_cast<int *>(addr::kSaveRequest) = 1;
}

void RequestAutosave()
{
	std::string dir, suffix;
	if (!QuickSaveDir(dir, suffix)) {
		LogW("autosave", "could not tell where to save");
		return;
	}

	const std::string profile = ProfileOfDir(dir);
	SaveLibPruneAutosaves(profile, SaveSessionCurrent(), std::max(1, GetConfig().autosaveSlots) - 1);
	SYSTEMTIME now;
	GetLocalTime(&now);
	char stamp[64];
	sprintf_s(stamp, Tr("Autosave %02dh%02d").c_str(), now.wHour, now.wMinute);
	std::string name = stamp;
	{
		std::string disk = GameDir() + "\\" + dir + name + suffix;
		for (char &c : disk) if (c == '/') c = '\\';
		if (GetFileAttributesA(disk.c_str()) != INVALID_FILE_ATTRIBUTES) {
			sprintf_s(stamp, Tr("Autosave %02dh%02dm%02d").c_str(), now.wHour, now.wMinute, now.wSecond);
			name = stamp;
		}
	}
	const std::string path = dir + name + suffix;
	{
		std::lock_guard<std::mutex> lock(g_autosavePathLock);
		g_autosavePath = SavePathKey(path);
	}
	RequestSave(path);
	LogI("autosave", "saving to %s", path.c_str());
}

}

std::string AutosaveRequestNamed(const std::string &name)
{
	std::string dir, suffix;
	if (!QuickSaveDir(dir, suffix)) return "";
	const std::string path = dir + name + suffix;
	RequestSave(path);
	LogI("saves", "quick save requested: %s", path.c_str());
	return path;
}

bool AutosaveIsAutosaveFile(const std::string &gamePath)
{
	const std::string key = SavePathKey(gamePath);
	std::lock_guard<std::mutex> lock(g_autosavePathLock);
	const std::string &own = g_autosavePath;
	if (key.empty() || own.empty()) return false;
	// One of the two may carry a folder prefix the other lacks: the shorter must be the tail of the longer, at a folder boundary.
	const std::string &longer = key.size() >= own.size() ? key : own, &shorter = key.size() >= own.size() ? own : key;
	const size_t at = longer.size() - shorter.size();
	return longer.compare(at, std::string::npos, shorter) == 0 && (at == 0 || longer[at - 1] == '/' || shorter[0] == '/');
}

bool AutosavePending()
{
	return *reinterpret_cast<int *>(addr::kSaveRequest) != 0;
}

bool CanAutosaveNow()
{
	if (!g_origProcess || g_inSession <= 0) return false;
	int *game = *reinterpret_cast<int **>(addr::kGame);
	if (!game) return false;
	if (reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) return false;
	if (*reinterpret_cast<uintptr_t *>(game) == addr::kMultiplayerVtable && !NetIsLockstepHost()) return false;
	return !*reinterpret_cast<int *>(addr::kEditorMode) && !*reinterpret_cast<int *>(addr::kLoadingSavedGame) &&
		!*reinterpret_cast<int *>(addr::kLoadRequest) && !*reinterpret_cast<int *>(addr::kSaveRequest);
}

typedef void(__cdecl *RequestQuit_t)();
RequestQuit_t g_origRequestQuit = nullptr;
bool g_quitPending = false;
DWORD g_quitSince = 0;

void __cdecl HookRequestQuit()
{
	if (g_quitPending) return;
	if (AutosaveOnExit()) {
		g_quitPending = true;
		g_quitSince = GetTickCount();
		LogI("autosave", "exit: saving the game, then exiting");
		return;
	}
	g_origRequestQuit();
}

bool AutosaveOnExit()
{
	if (!GetConfig().autosave || !GetConfig().autosaveOnExit || !CanAutosaveNow()) return false;
	g_playedMs = 0;
	RequestAutosave();
	LogI("autosave", "on exit");
	return true;
}

void AutosaveQuitIfReady()
{
	if (!g_quitPending || !g_origRequestQuit) return;
	const bool timeout = GetTickCount() - g_quitSince > 15000;
	if (AutosavePending() && !timeout) return;
	g_quitPending = false;
	// RequestQuitToMenu (006cebf0) calls through g_pGame without checking it: the game may have ended during the wait.
	if (g_inSession <= 0 || !*reinterpret_cast<void **>(addr::kGame)) {
		LogI("autosave", "exit: the game ended while it was saving; nothing to quit");
		return;
	}
	if (timeout) LogW("autosave", "exit: the save did not finish in 15 s; exiting anyway");
	g_origRequestQuit();
}

void AutosaveTick()
{
	if (g_quitPending && (!AutosavePending() || GetTickCount() - g_quitSince > 15000)) UiWakeGameThread();
	const DWORD now = GetTickCount();
	DWORD dt = g_lastTick ? now - g_lastTick : 0;
	g_lastTick = now;
	if (dt > 2000) dt = 0;
	if (!g_origProcess || g_inSession <= 0 || !GetConfig().autosave) return;
	int *game = *reinterpret_cast<int **>(addr::kGame);
	if (!game) return;
	if (reinterpret_cast<IsDemo_t>(addr::kIsDemoPlayback)()) return;
	if (*reinterpret_cast<uintptr_t *>(game) == addr::kMultiplayerVtable && !NetIsLockstepHost()) return;
	if (*reinterpret_cast<int *>(addr::kEditorMode) || *reinterpret_cast<int *>(addr::kLoadingSavedGame) ||
		*reinterpret_cast<int *>(addr::kLoadRequest)) {
		return;
	}
	if (*reinterpret_cast<int *>(addr::kSaveRequest)) {
		g_playedMs = 0;
		return;
	}
	if (TweaksGamePaused(game)) return;
	g_playedMs += dt;
	if (g_playedMs >= static_cast<DWORD>(GetConfig().autosaveMinutes) * 60000u) {
		g_playedMs = 0;
		RequestAutosave();
	}
}

void InstallAutosave(bool knownExe)
{
	if (!knownExe) return;
	if (HookFunction(addr::kProcessPendingGameRequests, &HookProcessRequests, &g_origProcess)) {
		LogI("autosave", "ready");
	}
	else {
		g_origProcess = nullptr;
		LogE("autosave", "could not hook the game loop");
	}
	if (HookFunction(addr::kRequestQuitToMenu, &HookRequestQuit, &g_origRequestQuit)) {
		LogI("autosave", "on exit, saves after confirmation");
	}
	else {
		g_origRequestQuit = nullptr;
	}
	if (HookFunction(addr::kBuildQuickSavePath, &HookBuildQuickSavePath, &g_origBuildPath)) {
		LogI("autosave", "online games are saved in the %s folder of the profile", kSaveOnlineFolder);
	}
}
