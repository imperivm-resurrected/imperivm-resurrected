// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/uiport.h"

#include <string>

#include "core/log.h"
#include "game/chat.h"
#include "i18n/tr.h"
#include "ui/controlsui.h"
#include "ui/modui.h"
#include "ui/mpui.h"

namespace {

// In a match the notices (a player dropped, is rejoining, diplomacy is locked…) go to the chat with the game's own messages, so
// everything that happens in the match is read in one place; elsewhere they stay notices of the mod.
void Toast(const std::string &text, bool error)
{
	if (ChatAddGameNotice(Tr(text), error)) {
		LogI("ui", "notice in the chat: %s", TrSource(text).c_str());
		return;
	}
	ModUiToast(text, error);
}

const bool g_registered = [] {
	UiPort port;
	port.toast = &Toast;
	port.notice = &ModUiNotice;
	port.askRestart = &ModUiAskRestart;
	port.modManagerOpen = &ModUiIsOpen;
	port.multiplayerOpen = &MpUiIsOpen;
	port.askDropPlayers = &MpUiAskDropPlayers;
	port.hostSave = &MpUiHostSave;
	port.rejoinStarting = &MpUiRejoinStarting;
	port.rejoinInGame = &MpUiRejoinInGame;
	port.refreshMainMenuButton = &MpUiRefreshMainMenuButton;
	port.joinFromDiscord = &MpUiJoinFromDiscord;
	port.openControls = &ControlsUiOpen;
	port.wakeGameThread = &MpUiWakeGameThread;
	port.askTimed = &ModUiChooseTimed;
	port.cancelTimed = &ModUiCancelTimed;
	UiPortSet(port);
	return true;
}();

}
