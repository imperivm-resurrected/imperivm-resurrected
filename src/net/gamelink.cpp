// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/netlink.h"
#include "net/mpsave.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "net/steamnet.h"
#include "net/vnet.h"

namespace {

bool MultiplayerSaveLoading()
{
	return !MpSavePendingLoad().empty();
}

bool RejoinLoading()
{
	return RejoinPendingLoad() || RejoinClientActive() || SpectatorWatching() || SpectatorViewing();
}

unsigned RoomRules()
{
	return SteamCurrentRoom().info.rules;
}

bool IsLockstepHost()
{
	return VnetActive() && VnetMyNumber() == 1;
}

const bool g_registered = [] {
	NetLink link;
	link.multiplayerSaveLoading = &MultiplayerSaveLoading;
	link.rejoinLoading = &RejoinLoading;
	link.roomRules = &RoomRules;
	link.isLockstepHost = &IsLockstepHost;
	link.sendRoomCountdown = &SteamSendRoomCountdown;
	NetLinkSet(link);
	return true;
}();

}
