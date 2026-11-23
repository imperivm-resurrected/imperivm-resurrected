// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>

// The player's statistics (the game's "Change player" screen), drawn over the game's own dialog, which stays open under it and does the work.
void ProfileUiGameTick();
bool ProfileUiNeedsDraw();
void ProfileUiDraw(uint32_t *buf, int w, int h);
bool ProfileUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp);
// Any player's profile on the league, the same screen with their league and cup games, over the multiplayer screen: its way back
// leaves that screen as it was. `name` shows while the league answers.
void ProfileUiOpenPublic(uint64_t steamId, const std::string &name);
// Its requests and its closing, on the game thread: ProfileUiGameTick runs it too, for the frames the game draws.
void ProfileUiPublicTick();
