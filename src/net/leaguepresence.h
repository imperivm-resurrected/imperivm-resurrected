// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// What friends see on Steam and Discord while this player searches or plays a ranked game: "Searching ranked 1 vs 1 · Centurion II"
// or "Ranked 1 vs 1 · Centurion II", in the game's language. Empty otherwise: then each says what it says of any other moment. Any
// thread.
std::string LeagueRankedPresence();
