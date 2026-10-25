// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>
#include <atomic>

#include "ui/textedit.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/log.h"
#include "ui/modui.h"
#include "mods/vfs.h"
#include "ui/savesui.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "game/loadcurtain.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

namespace mpui_impl {
namespace {
typedef bool(__cdecl *BoolFn_t)();
BoolFn_t g_origUpdateXmlExists = nullptr;
BoolFn_t g_origIsNewUpdate = nullptr;

bool __cdecl HookUpdateXmlExists() { return true; }

bool __cdecl HookIsNewUpdateAvailable()
{
	static int last = -1;
	const int n = ModUiUpdatesAvailable();
	if (n != last) {
		last = n;
		LogI("ui", "main menu update indicator: %d content packs have an update", n);
		VfsLogStats();
	}
	return n > 0;
}
}
}

namespace mpui_impl {
std::atomic<bool> g_menuRefresh{false};
std::atomic<HWND> g_wakeWnd{nullptr};
}

namespace mpui_impl {
namespace {
unsigned __fastcall HookMainMenuOnMessage(void *dlg, void *edx, int *msg);
}
}

namespace mpui_impl {
void RunMenuRefresh(void *dlg)
{
	int fake[16] = {};
	fake[0x10 / 4] = 0x1934f937;
	HookMainMenuOnMessage(dlg, nullptr, fake);
}
}

namespace mpui_impl {
namespace {
unsigned __fastcall HookMainMenuOnMessage(void *dlg, void *edx, int *msg)
{
	g_mainMenu = dlg;
	if (g_menuRefresh.exchange(false) && static_cast<unsigned>(msg[0x10 / 4]) != 0x1934f937) RunMenuRefresh(dlg);
	if (static_cast<unsigned>(msg[0x10 / 4]) == kMsgButtonClick) {
		void *sender = reinterpret_cast<void *>(msg[0xc / 4]);
		const int id = sender ? reinterpret_cast<GetId_t>((*reinterpret_cast<void ***>(sender))[0x1c / 4])(sender) : 0;
		if (id == kIdMainMenuMods) {
			ModUiOpen();
			return 0;
		}
		if (id == kIdLoadGame) {
			SavesUiOpen();
			return 0;
		}
		if (id == kIdMultiplayer) {
			std::lock_guard<std::mutex> lock(g_lock);
			if (!g_s.classicPending) {
				g_mainMenu = dlg;
				OpenMpMenu(true);
				return 0;
			}
			g_s.classicPending = false;
		}
	}
	return g_origMainMenu(dlg, edx, msg);
}
}
}

typedef int(__cdecl *ShowModalMenu_t)(void *, const char *, const char *, int, int, void *, int);
ShowModalMenu_t g_origShowModal = nullptr;

std::string g_joinModalText;

int __cdecl HookShowModalMenu(void *winos, const char *ini, const char *text, int a, int b, void *c, int d)
{
	if (g_flow.inJoin) {
		LogD("lobby", "game message during the join: (not shown) %s", text ? text : "");
		if (!g_flow.joinAbort) g_joinModalText = text ? text : "";
		return 0;
	}
	LogD("lobby", "game message: (%s) %s", ini ? ini : "?", text ? text : "");
	LoadCurtainLift("a message of the game");
	bool wasOpen = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		wasOpen = g_s.open;
		if (wasOpen) {
			const bool joining = g_s.connecting || g_autoJoin.active;
			g_s.connecting = false;
			g_autoJoin.active = false;
			g_s.open = false;
			if (joining) ModUiToast(std::string("The game says: ") + (text ? text : ""));
		}
	}
	g_gameModal++;
	const int r = g_origShowModal(winos, ini, text, a, b, c, d);
	g_gameModal--;
	if (wasOpen && *reinterpret_cast<void **>(addr::kGame) == nullptr) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.open = true;
	}
	return r;
}

typedef int(__cdecl *JoinGameByIP_t)(unsigned ip);
JoinGameByIP_t g_origJoinByIP = nullptr;

int __cdecl HookJoinGameByIP(unsigned ip)
{
	LogI("lobby", "connecting to the host", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
	const DWORD t0 = GetTickCount();
	g_flow.joinAbort = false;
	g_flow.joinCount++;
	g_flow.inJoin = true;
	const int r = g_origJoinByIP(ip);
	g_flow.inJoin = false;
	if (g_flow.joinAbort.exchange(false)) {
		LogW("lobby", "join: cancelled (%lu ms)", GetTickCount() - t0);
		return r ? r : 1;
	}
	LogAt(r ? kLogWarn : kLogInfo, "lobby", "join: %s (%lu ms)", r ? "could not connect" : "connected, waiting for the game room", GetTickCount() - t0);
	if (r) {
		const std::string why = g_joinModalText;
		g_joinModalText.clear();
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.connecting = false;
		g_autoJoin.active = false;
		g_autoJoin.selected = 0;
		g_s.classicPending = false;
		g_flow.backToMain = true;
		ModUiToast(why.find("versi") != std::string::npos ? why : "Couldn't join the host's game. Try again from the list.");
		SteamLeaveLobby();
		if (!g_s.open) OpenMpMenu(false);
	}
	return r;
}

typedef int(__fastcall *PumpMessages_t)(void *winos);
PumpMessages_t g_origPumpMessages = nullptr;

int __fastcall HookPumpMessages(void *winos)
{
	if (g_flow.inJoin && g_flow.joinAbort) return 0;
	return g_origPumpMessages(winos);
}

const int kIdReleaseLabel = 0x1022;
const char kGameReleaseText[] = "Release 1.0";
const unsigned kFlagVisible = 0x2;

typedef int(__fastcall *MainMenuOnShow_t)(void *dlg);
typedef void(__fastcall *GetTextA_t)(void *text, void *edx, char *buf, int size);
typedef void(__fastcall *ModifyFlags_t)(void *wnd, void *edx, unsigned set, unsigned clear, int invalidate);
MainMenuOnShow_t g_origMainMenuOnShow = nullptr;

void HideGameReleaseLabel(void *dlg)
{
	void **vt = *reinterpret_cast<void ***>(dlg);
	void *label = reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, kIdReleaseLabel, *reinterpret_cast<const char **>(addr::kClassNameCUITextW));
	if (!label) return;
	char text[64] = {};
	reinterpret_cast<GetTextA_t>(addr::kTextGetTextA)(label, nullptr, text, sizeof(text) - 1);
	if (strcmp(text, kGameReleaseText) != 0) return;
	reinterpret_cast<ModifyFlags_t>(addr::kModifyFlags)(label, nullptr, 0, kFlagVisible, 1);
}

int __fastcall HookMainMenuOnShow(void *dlg)
{
	const int r = g_origMainMenuOnShow(dlg);
	HideGameReleaseLabel(dlg);
	return r;
}

typedef void(__fastcall *MainMenuDtor_t)(void *dlg);
MainMenuDtor_t g_origMainMenuDtor = nullptr;

void __fastcall HookMainMenuDtor(void *dlg)
{
	if (dlg == g_mainMenu) g_mainMenu = nullptr;
	g_origMainMenuDtor(dlg);
}

void InstallMpUi(bool knownExe)
{
	if (!knownExe) return;
	void *target = reinterpret_cast<void *>(addr::kMainMenuOnMessage);
	if (HookCreate(target, reinterpret_cast<void *>(&HookMainMenuOnMessage), reinterpret_cast<void **>(&g_origMainMenu)) ==
			MH_OK &&
		HookEnable(target) == MH_OK) {
		LogI("lobby", "new menu ready");
	}
	void *update = reinterpret_cast<void *>(addr::kLobbyUpdate);
	if (HookCreate(update, reinterpret_cast<void *>(&HookLobbyUpdate), reinterpret_cast<void **>(&g_origLobbyUpdate)) != MH_OK ||
		HookEnable(update) != MH_OK) {
		LogE("lobby", "could not hook the classic list (automatic join disabled)");
	}
	void *modal = reinterpret_cast<void *>(addr::kShowModalMenu);
	if (HookCreate(modal, reinterpret_cast<void *>(&HookShowModalMenu), reinterpret_cast<void **>(&g_origShowModal)) == MH_OK) {
		HookEnable(modal);
	}
	void *pump = reinterpret_cast<void *>(addr::kPumpMessages);
	if (HookCreate(pump, reinterpret_cast<void *>(&HookPumpMessages), reinterpret_cast<void **>(&g_origPumpMessages)) == MH_OK) {
		HookEnable(pump);
	}
	void *join = reinterpret_cast<void *>(addr::kJoinGameByIP);
	if (HookCreate(join, reinterpret_cast<void *>(&HookJoinGameByIP), reinterpret_cast<void **>(&g_origJoinByIP)) == MH_OK) {
		HookEnable(join);
	}
	void *mmShow = reinterpret_cast<void *>(addr::kMainMenuOnShow);
	if (HookCreate(mmShow, reinterpret_cast<void *>(&HookMainMenuOnShow), reinterpret_cast<void **>(&g_origMainMenuOnShow)) == MH_OK) {
		HookEnable(mmShow);
	}
	void *mmDtor = reinterpret_cast<void *>(addr::kMainMenuDtor);
	if (HookCreate(mmDtor, reinterpret_cast<void *>(&HookMainMenuDtor), reinterpret_cast<void **>(&g_origMainMenuDtor)) == MH_OK) {
		HookEnable(mmDtor);
	}
	void *xmlExists = reinterpret_cast<void *>(addr::kUpdateXmlExists);
	if (HookCreate(xmlExists, reinterpret_cast<void *>(&HookUpdateXmlExists), reinterpret_cast<void **>(&g_origUpdateXmlExists)) == MH_OK) {
		HookEnable(xmlExists);
	}
	void *newUpdate = reinterpret_cast<void *>(addr::kIsNewUpdateAvailable);
	if (HookCreate(newUpdate, reinterpret_cast<void *>(&HookIsNewUpdateAvailable), reinterpret_cast<void **>(&g_origIsNewUpdate)) == MH_OK) {
		HookEnable(newUpdate);
	}
	void *gameMenu = reinterpret_cast<void *>(addr::kGameMenuOnMessage);
	if (HookCreate(gameMenu, reinterpret_cast<void *>(&HookGameMenuMsg), reinterpret_cast<void **>(&g_origGameMenuMsg)) == MH_OK) {
		HookEnable(gameMenu);
	}
}
