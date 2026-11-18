// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/uiport.h"

#include "core/log.h"

namespace {

UiPort g_port;

void Missing(const char *what)
{
	static int logged = 0;
	if (logged++ < 8) LogW("ui", "interface: %s was called before the interface was registered", what);
}

}

void UiPortSet(const UiPort &port)
{
	g_port = port;
}

void UiToast(const std::string &text, bool error)
{
	if (g_port.toast) g_port.toast(text, error);
	else Missing("toast");
}

void UiNotice(const std::string &title, const std::string &text)
{
	if (g_port.notice) g_port.notice(title, text);
	else Missing("notice");
}

void UiAskRestart(const std::string &reason, int largeAddress, const std::vector<std::string> &items, const std::string &itemsTitle)
{
	if (g_port.askRestart) g_port.askRestart(reason, largeAddress, items, itemsTitle);
	else Missing("restart question");
}

bool UiModManagerOpen()
{
	if (!g_port.modManagerOpen) {
		Missing("mod manager query");
		return false;
	}
	return g_port.modManagerOpen();
}

bool UiMultiplayerOpen()
{
	if (!g_port.multiplayerOpen) {
		Missing("multiplayer screen query");
		return false;
	}
	return g_port.multiplayerOpen();
}

bool UiAskDropPlayers(const std::vector<std::string> &names, int positions)
{
	if (!g_port.askDropPlayers) {
		Missing("drop players question");
		return false;
	}
	return g_port.askDropPlayers(names, positions);
}

bool UiHostSave(const std::string &path)
{
	if (!g_port.hostSave) {
		Missing("host saved game");
		return false;
	}
	return g_port.hostSave(path);
}

void UiRejoinStarting()
{
	if (g_port.rejoinStarting) g_port.rejoinStarting();
	else Missing("rejoin start");
}

void UiRejoinInGame()
{
	if (g_port.rejoinInGame) g_port.rejoinInGame();
	else Missing("rejoin in game");
}

void UiRefreshMainMenuButton()
{
	if (g_port.refreshMainMenuButton) g_port.refreshMainMenuButton();
	else Missing("main menu refresh");
}

void UiJoinFromDiscord(uint64_t lobbyId)
{
	if (g_port.joinFromDiscord) g_port.joinFromDiscord(lobbyId);
	else Missing("join from Discord");
}

void UiOpenControls()
{
	if (g_port.openControls) g_port.openControls();
	else Missing("controls screen");
}

void UiAskTimed(const std::string &title, const std::string &text, const std::string &yes, void (*onYes)(), const std::string &alt, void (*onAlt)(),
	unsigned deadline)
{
	if (g_port.askTimed) g_port.askTimed(title, text, yes, onYes, alt, onAlt, deadline);
	else Missing("timed question");
}

void UiCancelTimed()
{
	if (g_port.cancelTimed) g_port.cancelTimed();
	else Missing("timed question");
}

void UiWakeGameThread()
{
	if (g_port.wakeGameThread) g_port.wakeGameThread();
	else Missing("game thread wake");
}
