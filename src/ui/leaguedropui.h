// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>

// Over a ranked 1v1 whose opponent dropped: the paused game's wait for them ("Waiting for Livia: 2:41 to come back") and, once it is
// over, the button that claims the win and leaves the game. Over a ranked 2v2: a band at the top for the players who dropped ("Livia has
// dropped: the AI controls them. They have 4:12 to come back") and the partner's proposal to surrender, with Yes and No for 30 s; the
// game goes on under it, and only the vote's buttons take the mouse.
void LeagueDropUiGameTick();
bool LeagueDropUiNeedsDraw();
void LeagueDropUiDraw(uint32_t *buf, int w, int h);
bool LeagueDropUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
