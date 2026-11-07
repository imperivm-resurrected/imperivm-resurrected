// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/netlink.h"

namespace {

NetLink g_link;

}

void NetLinkSet(const NetLink &link)
{
	g_link = link;
}

bool NetMultiplayerSaveLoading()
{
	return g_link.multiplayerSaveLoading && g_link.multiplayerSaveLoading();
}

bool NetRejoinLoading()
{
	return g_link.rejoinLoading && g_link.rejoinLoading();
}

unsigned NetRoomRules()
{
	return g_link.roomRules ? g_link.roomRules() : 0;
}

bool NetIsLockstepHost()
{
	return g_link.isLockstepHost && g_link.isLockstepHost();
}

bool NetSendRoomCountdown(unsigned seq, int ms)
{
	return g_link.sendRoomCountdown && g_link.sendRoomCountdown(seq, ms);
}

std::string NetRoomName()
{
	return g_link.roomName ? g_link.roomName() : std::string();
}

bool NetRenameRoom(const std::string &name)
{
	return g_link.renameRoom && g_link.renameRoom(name);
}
