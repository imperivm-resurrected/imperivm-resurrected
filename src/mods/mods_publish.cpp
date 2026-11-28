// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include "MinHook.h"
#include "hooks/audio.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "mods/gbref.h"
#include "mods/modoptions.h"
#include "mods/modvars.h"

#include "mods/mods_internal.h"

using namespace mods_impl;

bool ModsExeIsLargeAddressAware()
{
	auto base = reinterpret_cast<const unsigned char *>(GetModuleHandleA(nullptr));
	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
	return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

namespace {
std::string PsQuote(const std::string &s)
{
	std::string out;
	for (char c : s) {
		out += c;
		if (c == '\'') out += '\'';
	}
	return "'" + out + "'";
}

// Byte 22 past the PE header's offset (kept at 60) is the low byte of the characteristics, and 32 is IMAGE_FILE_LARGE_ADDRESS_AWARE.
// The first change keeps the untouched exe as .no4gb.
std::string LargeAddressScript(const std::string &exe, int largeAddress)
{
	return "$p=" + PsQuote(exe) + "; if (-not (Test-Path ($p+'.no4gb'))) { Copy-Item $p ($p+'.no4gb') }; $b=[IO.File]::ReadAllBytes($p); "
		"$o=[BitConverter]::ToInt32($b,60)+22; " +
		(largeAddress ? std::string("$b[$o]=[byte]($b[$o] -bor 32); ") : std::string("$b[$o]=[byte]($b[$o] -band 223); ")) +
		"[IO.File]::WriteAllBytes($p,$b)";
}

// Returns 0, or the error of CreateProcess.
DWORD RunHiddenAfterExit(const std::string &script)
{
	const std::string cmd = "powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"Wait-Process -Id " +
		std::to_string(GetCurrentProcessId()) + " -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; " + script + "\"";
	STARTUPINFOA si = {sizeof(si)};
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	std::vector<char> line(cmd.begin(), cmd.end());
	line.push_back(0);
	if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return GetLastError();
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
}

void ModsPatchExeAtExit(int largeAddress)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	if (RunHiddenAfterExit(LargeAddressScript(exe, largeAddress)) == 0) {
		LogI("boot", "memory: when the game closes, the 4 GB patch is %s", largeAddress ? "applied" : "removed");
	}
}

void ModsRestartGame(int largeAddress, const std::string &args)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	char steamApp[32] = {};
	GetEnvironmentVariableA("SteamAppId", steamApp, sizeof(steamApp));
	std::string steamArgs;
	for (char c : args) steamArgs += c == ' ' ? std::string("%20") : std::string(1, c);
	std::string start = steamApp[0] ? "Start-Process " + PsQuote(std::string("steam://rungameid/") + steamApp +
														   (args.empty() ? "" : "//" + steamArgs))
									 : "Start-Process -FilePath " + PsQuote(exe) + " -WorkingDirectory " + PsQuote(GameDir()) +
										   (args.empty() ? "" : " -ArgumentList " + PsQuote(args));
	std::string patch;
	if (largeAddress >= 0) {
		patch = LargeAddressScript(exe, largeAddress) + "; ";
		LogI("content", "on restart, the 4 GB patch is %s in gbr.exe", largeAddress ? "applied" : "removed");
	}
	const DWORD error = RunHiddenAfterExit(patch + start);
	if (error == 0) {
		LogI("content", "restarting the game (%s)", steamApp[0] ? "via Steam" : "directly");
	}
	else {
		LogW("content", "could not prepare the restart (error %lu); the game must be opened by hand", error);
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

void ModsOnSaveWritten(const std::string &gamePath, bool online)
{
	const std::string side = GameDir() + "\\" + gamePath + ".mods.json";
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
	if (const int secs = SaveLibGameSeconds(); secs >= 0) j.Set("seconds", Json::MakeNumber(secs));
	// Who played, for a replay's card: the game cannot read its players back from it as it does from a save.
	{
		std::vector<SavePlayer> players;
		if (SaveLibPlayers(players)) {
			Json arr = Json::MakeArray();
			for (const SavePlayer &p : players) {
				Json e = Json::MakeObject();
				e.Set("name", Json::MakeString(p.name));
				e.Set("color", Json::MakeNumber(p.color555));
				if (p.ai) e.Set("ai", Json::MakeBool(true));
				arr.arr.push_back(e);
			}
			j.Set("players", arr);
		}
	}
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
	j.Set("session", Json::MakeString(SaveSessionCurrent()));
		if (ModVarsCount() > 0) j.Set("variables", ModVarsToJson());
	{
		const size_t slash = gamePath.find_last_of("/\\");
		if (SaveLibIsAutosaveName(gamePath.substr(slash == std::string::npos ? 0 : slash + 1))) j.Set("autosave", Json::MakeBool(true));
	}
	std::string dir = side.substr(0, side.find_last_of('\\'));
	if (IsDir(dir)) {
		WriteJsonFile(side, j);
	}
}
