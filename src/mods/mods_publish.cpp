// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include "MinHook.h"
#include "hooks/audio.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "core/sha256.h"
#include "mods/gbref.h"
#include "mods/modoptions.h"
#include "mods/modvars.h"
#include "mods/civindices.h"
#include "mods/civsets.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

bool ModsExeIsLargeAddressAware()
{
	auto base = reinterpret_cast<const unsigned char *>(GetModuleHandleA(nullptr));
	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
	return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

void ModsPatchExeAtExit(int largeAddress)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	std::string p;
	for (char c : std::string(exe)) {
		p += c;
		if (c == '\'') p += '\'';
	}
	const std::string cmd = "powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Wait-Process -Id " +
		std::to_string(GetCurrentProcessId()) + " -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; $p='" + p +
		"'; if (-not (Test-Path ($p+'.no4gb'))) { Copy-Item $p ($p+'.no4gb') }; $b=[IO.File]::ReadAllBytes($p); "
		"$o=[BitConverter]::ToInt32($b,60)+22; " +
		(largeAddress ? std::string("$b[$o]=[byte]($b[$o] -bor 32); ") : std::string("$b[$o]=[byte]($b[$o] -band 223); ")) +
		"[IO.File]::WriteAllBytes($p,$b)\"";
	STARTUPINFOA si = {sizeof(si)};
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	std::vector<char> line(cmd.begin(), cmd.end());
	line.push_back(0);
	if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		LogI("boot", "memory: when the game closes, the 4 GB patch is %s", largeAddress ? "applied" : "removed");
	}
}

void ModsRestartGame(int largeAddress, const std::string &args)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	char steamApp[32] = {};
	GetEnvironmentVariableA("SteamAppId", steamApp, sizeof(steamApp));
	auto quote = [](std::string s) {
		std::string out;
		for (char c : s) {
			out += c;
			if (c == '\'') out += '\'';
		}
		return "'" + out + "'";
	};
	std::string steamArgs;
	for (char c : args) steamArgs += c == ' ' ? std::string("%20") : std::string(1, c);
	std::string start = steamApp[0] ? "Start-Process " + quote(std::string("steam://rungameid/") + steamApp +
														   (args.empty() ? "" : "//" + steamArgs))
									 : "Start-Process -FilePath " + quote(exe) + " -WorkingDirectory " + quote(GameDir()) +
										   (args.empty() ? "" : " -ArgumentList " + quote(args));
	std::string patch;
	if (largeAddress >= 0) {
		patch = "$p=" + quote(exe) + "; if (-not (Test-Path ($p+'.no4gb'))) { Copy-Item $p ($p+'.no4gb') }; "
			"$b=[IO.File]::ReadAllBytes($p); $o=[BitConverter]::ToInt32($b,60)+22; " +
			(largeAddress ? std::string("$b[$o]=[byte]($b[$o] -bor 32); ") : std::string("$b[$o]=[byte]($b[$o] -band 223); ")) +
			"[IO.File]::WriteAllBytes($p,$b); ";
		LogI("content", "on restart, the 4 GB patch is %s in gbr.exe", largeAddress ? "applied" : "removed");
	}
	std::string cmd = "powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Wait-Process -Id " +
		std::to_string(GetCurrentProcessId()) + " -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; " +
		patch + start + "\"";
	STARTUPINFOA si = {sizeof(si)};
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	std::vector<char> line(cmd.begin(), cmd.end());
	line.push_back(0);
	if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		LogI("content", "restarting the game (%s)", steamApp[0] ? "via Steam" : "directly");
	}
	else {
		LogW("content", "could not prepare the restart (error %lu); the game must be opened by hand", GetLastError());
	}
	AudioSetBackgroundMute(false);
	LogShutdown();
	TerminateProcess(GetCurrentProcess(), 0);
}

void ModsOpenFolder(const std::string &folder)
{
	const std::string path = folder.empty() ? ModsDir() : ModsDir() + "\\" + folder;
	CreateDirectoryA(ModsDir().c_str(), nullptr);
	HWND game = GetActiveWindow();
	if (game && !(GetWindowLongA(game, GWL_STYLE) & WS_CAPTION)) {
		LogI("content", "minimizing the game to open the folder");
		ShowWindow(game, SW_MINIMIZE);
	}
	if (folder.empty()) {
		ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}
	const std::string args = "/select,\"" + path + "\"";
	ShellExecuteA(nullptr, "open", "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool ModsOnSaveWritten(const std::string &gamePath, bool online)
{
	const bool absolute = gamePath.size() > 1 && (gamePath[1] == ':' || gamePath[0] == '\\' || gamePath[0] == '/');
	const std::string file = absolute ? gamePath : GameDir() + "\\" + gamePath;
	const std::string side = file + ".mods.json";
	const ModProfile p = ModsMountedProfile();
	Json j = Json::MakeObject();
	j.Set("profile", Json::MakeString(p.name));
	j.Set("gameplayFingerprint", Json::MakeString(ModsProfileGameplayHash(p)));
	j.Set("schema", Json::MakeNumber(kRulesSchema));
	Json mods = Json::MakeArray();
	for (const std::string &folder : p.mods) {
		ModInfo m;
		Json e = Json::MakeObject();
		e.Set("folder", Json::MakeString(folder));
		if (ModsFind(folder, m)) {
			e.Set("id", Json::MakeString(m.id));
			e.Set("name", Json::MakeString(m.name));
			e.Set("version", Json::MakeString(m.version));
			if (!m.gameplayHash.empty()) e.Set("fingerprint", Json::MakeString(m.gameplayHash));
			e.Set("gameplay", Json::MakeBool(m.gameplay));
			e.Set("kind", Json::MakeString(ModsKindId(m.kind)));
			GbKeyToJson(e, m.gbId);
		}
		mods.arr.push_back(e);
	}
	j.Set("mods", mods);
	if (CivIndicesAutomatic()) {
		const std::string hash = HashFile(file);
		if (hash.empty()) return false;
		j.Set("civIndices", CivIndicesToJson());
		j.Set("civSaveHash", Json::MakeString(hash));
	}
	if (const int secs = SaveLibGameSeconds(); secs >= 0) j.Set("seconds", Json::MakeNumber(secs));
	{
		int states[8];
		if (SaveLibPlayerStates(states)) {
			Json arr = Json::MakeArray();
			for (int s : states) arr.arr.push_back(Json::MakeNumber(s));
			j.Set("states", arr);
			LogI("saves", "player states when saving: %d %d %d %d %d %d %d %d", states[0], states[1], states[2], states[3], states[4], states[5], states[6], states[7]);
		}
	}
	if (online) j.Set("online", Json::MakeBool(true));
	if (const unsigned rules = ModRulesActive()) j.Set("rules", Json::MakeNumber(static_cast<double>(rules)));
	if (const std::vector<std::string> sets = CivRoomSets(); !sets.empty()) j.Set("civSets", Json::MakeString(CivSetsJoin(sets)));
	j.Set("session", Json::MakeString(SaveSessionCurrent()));
		if (ModVarsCount() > 0) j.Set("variables", ModVarsToJson());
	{
		const size_t slash = gamePath.find_last_of("/\\");
		if (SaveLibIsAutosaveName(gamePath.substr(slash == std::string::npos ? 0 : slash + 1))) j.Set("autosave", Json::MakeBool(true));
	}
	return WriteJsonFile(side, j);
}
