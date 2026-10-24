// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/log.h"
#include "ui/modui.h"
#include "mods/savelib.h"
#include "addr/state.h"
#include "addr/ui.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

bool MpUiMainMenuVisible()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_mainMenu || g_s.open || *reinterpret_cast<void **>(addr::kGame) != nullptr) return false;
	char *menu = static_cast<char *>(g_mainMenu);
	char *desktop = *reinterpret_cast<char **>(menu + 0x08);
	if (desktop) {
		void *modal = *reinterpret_cast<void **>(desktop + 0x370);
		if (modal && modal != g_mainMenu) return false;
	}
	return true;
}

bool MpUiIsOpen()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.open;
}

void __fastcall HookLobbyUpdate(void *self)
{
	g_origLobbyUpdate(self);
	char *d = static_cast<char *>(self);
	g_flow.lastLobbyUpdate = GetTickCount();
	if (g_flow.backToMain) {
		g_flow.backToMain = false;
		void **vtb = *reinterpret_cast<void ***>(self);
		char *b = static_cast<char *>(reinterpret_cast<GetChild_t>(vtb[0x7c / 4])(self, nullptr, 0x1005,
			*reinterpret_cast<const char **>(addr::kClassNameCUIButton)));
		if (b) {
			typedef void(__fastcall * SendNotify_t)(void *, void *, int, unsigned, int, int, int);
			void **bvt = *reinterpret_cast<void ***>(b);
			reinterpret_cast<SendNotify_t>(bvt[0x6c / 4])(b, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgButtonClick,
				*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
		}
		return;
	}
	if (g_flow.hostPending) {
		g_flow.hostPending = false;
		void **locator = reinterpret_cast<void **>(d + 0x6c);
		if (*locator) {
			typedef void(__fastcall * Delete_t)(void *, void *, int);
			reinterpret_cast<Delete_t>((*reinterpret_cast<void ***>(*locator))[0])(*locator, nullptr, 1);
			*locator = nullptr;
		}
		LogI("lobby", "creating the game in-engine (HostGame)");
		reinterpret_cast<HostGame_t>(addr::kFnHostGame)(0, 0, 0);
		return;
	}
	if (!g_autoJoin.active) return;
	const DWORD now = GetTickCount();
	if (now - g_autoJoin.start > 60000) {
		g_autoJoin.active = false;
		ModUiToast("The host's game did not show up. Click \"Refresh\" or rejoin from Multiplayer.");
		LogW("lobby", "the host's game did not appear within 60 s");
		return;
	}
	if (g_autoJoin.clickedAt) {
		if (g_flow.joinCount != g_autoJoin.countAtClick) {
			g_autoJoin.active = false;
			g_autoJoin.clickedAt = 0;
			return;
		}
		if (now - g_autoJoin.clickedAt < 2500) return;
		LogW("lobby", "Join did not start the entry (attempt %d)", g_autoJoin.clicks);
		g_autoJoin.clickedAt = 0;
		g_autoJoin.selected = 0;
		g_autoJoin.items = nullptr;
		if (g_autoJoin.clicks >= 4) {
			LogW("lobby", "the entry does not start after %d attempts; cancelling and leaving the room", g_autoJoin.clicks);
			{
				std::lock_guard<std::mutex> lock(g_lock);
				AbortJoin();
			}
			ModUiToast("Couldn't join the host's game. Try again from the list.");
			SteamLeaveLobby();
			return;
		}
	}
	if (!g_autoJoin.lan) {
		g_autoJoin.lan = true;
		if (*reinterpret_cast<int *>(d + 0x1180) == 1) {
			reinterpret_cast<ThisOnly_t>(addr::kStartLanBrowser)(self);
		}
	}
	void **vt = *reinterpret_cast<void ***>(self);
	void *list = reinterpret_cast<GetChild_t>(vt[0x7c / 4])(self, nullptr, *reinterpret_cast<int *>(d + 0x117c),
		*reinterpret_cast<const char **>(addr::kClassNameCUISList));
	if (list) {
		char *l = static_cast<char *>(list);
		const unsigned count = *reinterpret_cast<unsigned *>(l + 0x58);
		char *items = *reinterpret_cast<char **>(l + 0x94);
		bool found = false;
		for (unsigned i = 0; items && i < count; i++) {
			if (*reinterpret_cast<int *>(items + i * 0x14 + 0xc) == 0) continue;
			found = true;
			if (!g_autoJoin.selected || items != g_autoJoin.items || count != g_autoJoin.itemCount) {
				g_autoJoin.selected = GetTickCount();
				g_autoJoin.items = items;
				g_autoJoin.itemCount = count;
				LogI("lobby", "room game found in the list; selecting it");
				reinterpret_cast<SetSelectedItem_t>(addr::kSetSelectedItem)(list, nullptr, i, 1, 1);
				typedef void(__fastcall * SelectExclusive_t)(void *, void *, unsigned, int, int);
				reinterpret_cast<SelectExclusive_t>(addr::kListSelectExclusive)(list, nullptr, i, 1, 1);
				return;
			}
			if (GetTickCount() - g_autoJoin.selected < 400) return;
			g_autoJoin.clickedAt = GetTickCount();
			g_autoJoin.clicks++;
			g_autoJoin.countAtClick = g_flow.joinCount;
			char *b = static_cast<char *>(reinterpret_cast<GetChild_t>(vt[0x7c / 4])(self, nullptr, 0x1002,
				*reinterpret_cast<const char **>(addr::kClassNameCUIButton)));
			LogI("lobby", "clicking Join");
			if (b) {
				typedef void(__fastcall * SendNotify_t)(void *, void *, int, unsigned, int, int, int);
				void **bvt = *reinterpret_cast<void ***>(b);
				reinterpret_cast<SendNotify_t>(bvt[0x6c / 4])(b, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgButtonClick,
					*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
			}
			else {
				reinterpret_cast<ThisOnly_t>(addr::kJoinSelectedGame)(self);
			}
			return;
		}
		if (!found) {
			g_autoJoin.selected = 0;
			g_autoJoin.items = nullptr;
		}
	}
	if (!g_autoJoin.selected && now - g_autoJoin.refresh > 3000) {
		g_autoJoin.refresh = now;
		reinterpret_cast<ThisOnly_t>(addr::kOnRefreshClicked)(self);
	}
}

HostAfterGameState g_hostAfter;

void MpUiHostSaveAfterGame(const std::string &path)
{
	g_hostAfter.savePath = path;
	g_hostAfter.time = GetTickCount();
}

bool MpUiHostSave(const std::string &path)
{
	if (!SteamOnlineReady()) {
		ModUiToast("This game is online, but Steam is not available: it is loaded as a normal game");
		return false;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.saves = ListSaves();
	std::string want = path;
	for (char &c : want) {
		if (c == '\\') c = '/';
	}
	int idx = -1;
	const size_t slash = want.find_last_of('/');
	const std::string moved = slash == std::string::npos ? "" : want.substr(0, slash + 1) + kSaveOnlineFolder + "/" + want.substr(slash + 1);
	for (size_t i = 0; i < g_s.saves.size(); i++) {
		if (_stricmp(g_s.saves[i].vfsPath.c_str(), want.c_str()) == 0) idx = static_cast<int>(i);
	}
	for (size_t i = 0; idx < 0 && !moved.empty() && i < g_s.saves.size(); i++) {
		if (_stricmp(g_s.saves[i].vfsPath.c_str(), moved.c_str()) == 0) idx = static_cast<int>(i);
	}
	if (idx < 0) {
		LogW("saves", "\"%s\" is not in the saved games list", path.c_str());
		return false;
	}
	OpenMpMenu(false);
	g_s.ready = true;
	g_s.createSave = idx;
	g_s.createVisibility = 0;
	g_s.passInput.clear();
	g_s.createProfile = ModsMountedProfile().name;
	g_s.createCivSets.clear();
	LogI("saves", "online game: the room is created to load it");
	DoAction(kCreateOk, 0);
	return true;
}

void MpUiJoinFromDiscord(uint64_t lobbyId)
{
	if (*reinterpret_cast<void **>(addr::kGame) != nullptr) {
		ModUiToast("You were invited from Discord: finish the game and click Join again");
		return;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.room.id) {
		ModUiToast("You were invited from Discord: leave your current room first");
		return;
	}
	g_s.discordJoin = lobbyId;
	g_s.discordJoinAt = GetTickCount();
	g_s.discordJoinRefresh = 0;
	if (!g_s.open) OpenMpMenu(false);
	SteamRefreshLobbies();
	WakeGameThread();
}

void MpUiRejoinStarting()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.open = false;
	g_s.recoverAsk = false;
}

void MpUiRejoinInGame()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.inOnlineGame = true;
}

void MpUiRefreshMainMenuButton()
{
	ModUiResetUpdates();
	g_menuRefresh = true;
	if (g_wakeWnd) PostMessageW(g_wakeWnd, kMsgMpUiWake, 0, 0);
}

std::string MapKey(std::string p)
{
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

void StepSaveMap(const GameRoomSnapshot &snap)
{
	if (g_s.saveMapStep >= 2) return;
	const DWORD now = GetTickCount();
	int mapSel = -1;
	for (const GameRoomOption &o : snap.options) {
		if (o.id == 0x1000) mapSel = o.sel;
	}
	auto done = [&](const std::string &error) {
		g_s.saveMapStep = 2;
		g_s.saveMapError = error;
		if (error.empty()) LogD("saves", "saved game: room map ready (%s)", g_s.saveMapRandom ? "random" : snap.scenario.c_str());
		else LogW("saves", "saved game: could not choose the map \"%s\": %s", g_s.saveMap.c_str(), error.c_str());
	};
	if (g_s.saveMap.empty() && !g_s.saveMapRandom) {
		done("it is not known which map it was saved with; choose it yourself");
		return;
	}
	if (g_s.saveMapTick && now - g_s.saveMapTick < 700) return;
	if (g_s.saveMapTries >= 6) {
		if (snap.picker) GameRoomPickerCancel();
		done("could not pick the game's map");
		return;
	}
	if (g_s.saveMapRandom) {
		if (snap.scenario.empty() && mapSel != 0) {
			done("");
			return;
		}
		if (snap.picker) GameRoomPickerCancel();
		else GameRoomSetOption(0x1000, 1);
		g_s.saveMapTries++;
		g_s.saveMapTick = now;
		return;
	}
	const std::string want = MapKey(g_s.saveMap);
	if (!snap.picker && !snap.scenario.empty() && MapKey(snap.scenario) == want) {
		done("");
		return;
	}
	if (snap.picker) {
		if (g_s.saveMapStep == 1 && g_s.saveMapPick >= 0) {
			GameRoomPickerAccept();
			g_s.saveMapStep = 0;
			g_s.saveMapTries++;
			g_s.saveMapTick = now;
			return;
		}
		int idx = -1;
		for (size_t i = 0; i < snap.pickerPaths.size() && idx < 0; i++) {
			if (MapKey(snap.pickerPaths[i]) == want) idx = static_cast<int>(i);
		}
		const std::string wantFile = want.substr(want.find_last_of('/') + 1);
		for (size_t i = 0; i < snap.pickerPaths.size() && idx < 0; i++) {
			const std::string k = MapKey(snap.pickerPaths[i]);
			if (k.substr(k.find_last_of('/') + 1) == wantFile) idx = static_cast<int>(i);
		}
		if (idx < 0 || idx >= static_cast<int>(snap.pickerItems.size())) {
			LogD("saves", "saved game: the scenario \"%s\" is not among the %u in the selector%s%s", g_s.saveMap.c_str(),
				static_cast<unsigned>(snap.pickerPaths.size()), snap.pickerPaths.empty() ? "" : " (e.g.",
				snap.pickerPaths.empty() ? "" : (snap.pickerPaths[0] + ")").c_str());
			GameRoomPickerCancel();
			done(Tr("you don't have the scenario \"") + wantFile + "\"");
			return;
		}
		LogD("saves", "saved game: choosing scenario %d \"%s\"", idx + 1, snap.pickerPaths[idx].c_str());
		GameRoomPickerSelect(idx);
		g_s.saveMapPick = idx;
		g_s.saveMapStep = 1;
		g_s.saveMapTick = now;
		return;
	}
	GameRoomSetOption(0x1000, mapSel == 0 ? 1 : 0);
	g_s.saveMapStep = 0;
	g_s.saveMapTries++;
	g_s.saveMapTick = now;
}

bool MpUiAskDropPlayers(const std::vector<std::string> &names, int positions)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.dropAsk = true;
		g_s.dropNames = names;
		g_s.dropPositions = positions;
		g_s.dropAnswer = -1;
	}
	const DWORD start = GetTickCount();
	int answer = -1;
	while (answer < 0) {
		MSG msg;
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				PostQuitMessage(static_cast<int>(msg.wParam));
				answer = 0;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessageA(&msg);
		}
		Sleep(10);
		std::lock_guard<std::mutex> lock(g_lock);
		if (answer < 0) answer = g_s.dropAnswer;
		if (answer < 0 && GetTickCount() - start > 120000) answer = 0;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.dropAsk = false;
	if (answer == 1) {
		for (const std::string &n : names) {
			const uint64_t id = MemberIdByName(g_s, n);
			if (id) SteamSendSwap(id, "drop " + std::to_string(positions));
		}
	}
	return answer == 1;
}
