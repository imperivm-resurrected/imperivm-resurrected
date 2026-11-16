// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>
void MpUiRejoinStarting();
void MpUiRejoinInGame();
#include <windows.h>

#include <cstdint>
#include <string>

void InstallMpUi(bool knownExe);
// The section the multiplayer screen opens on next, from the main menu's Multiplayer submenu.
enum class MpSection { Rooms, Ranked, Tournaments };
void MpUiOpenSection(MpSection section);
bool MpUiIsOpen();
bool MpUiMainMenuVisible();
// The game's main menu dialog while no game runs, also under other screens; null otherwise.
void *MpUiMainMenu();
bool MpUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
bool MpUiNeedsDraw();
void MpUiDraw(uint32_t *buf, int w, int h);
void MpUiObserveFrame(const uint32_t *buf, int w, int h);
void MpUiRefreshMainMenuButton();
bool MpUiHostSave(const std::string &path);
void MpUiJoinFromDiscord(uint64_t lobbyId);
void MpUiGameThreadTick();
const UINT kMsgMpUiWake = WM_APP + 0x4d;
void MpUiSetGameWindow(HWND hwnd);
void MpUiWakeHandled();
void MpUiWakeGameThread();

bool MpUiAskDropPlayers(const std::vector<std::string> &names, int positions);
void MpUiHostSaveAfterGame(const std::string &path);
