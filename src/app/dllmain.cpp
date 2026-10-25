// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <cstdint>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "game/controls.h"
#include "core/crashlog.h"
#include "hooks/displaymode.h"
#include "mods/lang.h"
#include "game/scriptapi.h"
#include "game/scripterrors.h"
#include "game/civs.h"
#include "hooks/diag.h"
#include "game/scriptevents.h"
#include "mods/modvars.h"
#include "game/specials.h"
#include "core/log.h"
#include "core/version.h"
#include "hooks/media.h"
#include "net/mpsave.h"
#include "app/compat.h"
#include "app/overlay.h"
#include "app/render.h"
#include "hooks/tweaks.h"
#include "mods/updater.h"
#include "mods/vfs.h"
#include "game/options.h"
#include "hooks/dmshim.h"
#include "mods/mods.h"
#include "mods/netmods.h"
#include "mods/downloads.h"
#include "game/autosave.h"
#include "game/seasondecors.h"
#include "net/steamnet.h"
#include "ui/mpui.h"
#include "net/vnet.h"
#include "game/gameroom.h"
#include "net/discord.h"
#include "net/netdrop.h"
#include "net/lobbygates.h"
#include "hooks/selectionguard.h"
#include "hooks/curseguard.h"
#include "hooks/handleguard.h"
#include "game/packrules.h"
#include "hooks/builtinfixes.h"
#include "net/nettune.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "game/mapfog.h"
#include "game/quickicons.h"
#include "game/noteicons.h"
#include "game/zoomview.h"
#include "game/viewgrid.h"
#include "game/cheats.h"
#include "net/onlinesaves.h"
#include "ui/savesui.h"

bool LoadRealWinmm();

namespace {

const uint64_t kOriginalSteamExe = 0x8063a6de24cf3ccaull;

const uint64_t kOriginalSteamExeNormalized = 0x333183d57588d212ull;

void HashFile(const wchar_t *path, DWORD *sizeOut, uint64_t *exact, uint64_t *normalized)
{
	*exact = *normalized = 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) {
		return;
	}
	auto image = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
	const DWORD pe = reinterpret_cast<const IMAGE_DOS_HEADER *>(image)->e_lfanew;
	uint64_t h = 1469598103934665603ull, n = h;
	uint8_t buf[65536];
	DWORD read = 0, total = 0;
	while (ReadFile(f, buf, sizeof(buf), &read, nullptr) && read) {
		for (DWORD i = 0; i < read; i++) {
			const DWORD pos = total + i;
			const bool laaField = (pos >= pe + 0x16 && pos < pe + 0x18) || (pos >= pe + 0x58 && pos < pe + 0x5c);
			h = (h ^ buf[i]) * 1099511628211ull;
			n = (n ^ (laaField ? 0 : buf[i])) * 1099511628211ull;
		}
		total += read;
	}
	CloseHandle(f);
	*sizeOut = total;
	*exact = h;
	*normalized = n;
}

bool CheckExecutable()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	DWORD size = 0;
	uint64_t hash = 0, normalized = 0;
	HashFile(path, &size, &hash, &normalized);

	auto base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(nullptr));
	auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
	bool laa = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;

	const bool known = hash == kOriginalSteamExe || normalized == kOriginalSteamExeNormalized;
	LogI("boot", "executable: %lu bytes, fingerprint %016llx (normalized %016llx), timestamp %08lx, 4GB(LAA)=%d -> %s", size,
		hash, normalized, nt->FileHeader.TimeDateStamp, laa,
		known ? (hash == kOriginalSteamExe ? "original Steam" : "original Steam with the 4 GB patch")
			  : "UNKNOWN (exe-dependent fixes disabled)");
	return known;
}

void MakeDpiAware()
{
	if (!GetConfig().dpiAware) {
		return;
	}
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	typedef BOOL(WINAPI * SetCtx_t)(HANDLE);
	typedef UINT(WINAPI * GetDpi_t)();
	auto setCtx = reinterpret_cast<SetCtx_t>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
	auto getDpi = reinterpret_cast<GetDpi_t>(GetProcAddress(user32, "GetDpiForSystem"));
	const char *how = "failed";
	if (setCtx && setCtx(reinterpret_cast<HANDLE>(-2))) {
		how = "enabled";
	}
	else if (GetLastError() == ERROR_ACCESS_DENIED) {
		how = "already enabled (exe compatibility options)";
	}
	else if (SetProcessDPIAware()) {
		how = "enabled (old method)";
	}
	UINT dpi = getDpi ? getDpi() : 96;
	LogI("display", "scaling: (DPI) %s; Windows scale %u %%", how, dpi * 100 / 96);
}

bool IsGameProcess()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	const wchar_t *name = wcsrchr(path, L'\\');
	return name && _wcsicmp(name + 1, L"gbr.exe") == 0;
}

}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		bool game = IsGameProcess();
		if (game) {
			LogInit();
		}
		if (!LoadRealWinmm()) {
			return FALSE;
		}
		if (!game) {
			return TRUE;
		}
		const DWORD bootStart = GetTickCount();
		LogI("boot", "winmm proxy loaded, mod %s", ModVersionText());
		InstallCrashLog();
		const bool knownExe = CheckExecutable();
		char steamApp[32] = {};
		GetEnvironmentVariableA("SteamAppId", steamApp, sizeof(steamApp));
		LogI("boot", "launched from Steam: %s (SteamAppId=%s)", steamApp[0] ? "yes" : "no", steamApp[0] ? steamApp : "-");
		LogOverlayStatus("at startup");
		LoadConfig();
		MakeDpiAware();
		if (MH_Initialize() != MH_OK) {
			LogE("boot", "minhook: could not initialize");
			return TRUE;
		}
		HookBatchBegin();
		InstallRenderHooks();
		InstallDisplayModeHooks();
		InstallTweaks(knownExe);
		InstallUpdater();
		OverlayInstall(knownExe);
		InstallLanguage(knownExe);
		ScriptErrorsInit(knownExe);
		InstallScriptApi(knownExe);
		InstallScriptEvents(knownExe);
			InstallModVars(knownExe);
			InstallSpecials(knownExe);
		InstallCivs(knownExe);
		InstallCivUi(knownExe);
		InstallCivAi(knownExe);
		InstallDiag(knownExe);
		BuiltinFixesInstall(knownExe);
		InstallVfsHooks(knownExe);
		InstallModsHooks(knownExe);
		InstallNetModsHooks(knownExe);
		DlStartUpdateCheck();
		InstallAutosave(knownExe);
		InstallSeasonDecors(knownExe);
		MediaInstall(knownExe);
		InstallMpSave(knownExe);
		InstallOnlineSaves(knownExe);
		InstallNetDrop(knownExe);
		InstallLobbyGates(knownExe);
		InstallSelectionGuard(knownExe);
		InstallCurseGuard(knownExe);
		InstallHandleGuard(knownExe);
		InstallPackRules(knownExe);
		InstallRejoin(knownExe);
		InstallNetTune(knownExe);
		InstallSpectator(knownExe);
		InstallCheats(knownExe);
		InstallZoomFog(knownExe);
		InstallQuickIcons(knownExe);
		InstallNoteIcons(knownExe);
		InstallZoomView(knownExe);
		InstallViewGrid(knownExe);
		InstallSavesUi(knownExe);
		CheckCompatibilityLayers();
		if (knownExe && GetConfig().largeAddress && !ModsExeIsLargeAddressAware()) ModsPatchExeAtExit(1);
		InstallMpUi(knownExe);
		VnetInstallHooks(knownExe);
		GameRoomInstall(knownExe);
		StartSteamOnline();
		StartDiscordPresence();
		InstallOptionsHooks(knownExe);
		InstallControls(knownExe);
		InstallModernAudio(knownExe);
		HookBatchEnd();
		LogHookSummary();
		LogI("boot", "mod ready in %lu ms", static_cast<unsigned long>(GetTickCount() - bootStart));
	}
	else if (reason == DLL_PROCESS_DETACH) {
		DisplayRestoreMonitor();
		ShutdownRender();
		LogShutdown();
	}
	return TRUE;
}
