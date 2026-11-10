// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// The game's own translation of a text of its interface: "text@context" first, then the text alone, else the text unchanged.
std::string GameText(const std::string &text, const std::string &context = std::string());
// The name the game gives a base civilization (0 Gaul to 7 Germany) in its interface; empty for any other number.
std::string GameRaceText(int race);
