// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

#include "core/playeralert.h"

// Calls the player to the game (core/playeralert.h): the horn, also through the background mute; the taskbar flashing; and the window brought
// to the front when Windows lets it, which leaves the flash when it does not. Returns at once from any thread, under any lock: the window's
// messages and the audio sessions are touched from a thread of its own.
void AlertPlayer(PlayerAlert kind);
// The start countdown of the room `room` began: called once, and again only after a quiet while for the same room.
void AlertPlayerCountdown(uint64_t room);
