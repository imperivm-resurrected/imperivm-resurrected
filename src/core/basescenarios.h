// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// The scenarios the game installs in its Scenarios folder (Steam, depots 752581 and 752582, build 7227256): every player has them
// as they are, so a community cup plays them without sending them. They are loose files like the player's own, so only their name and
// contents tell them apart.

// The SHA-256 of the game's own file of that name (case is ignored), in lower-case hex; null when the game has no such scenario.
const char *BaseScenarioSha256(const std::string &fileName);
// The game's own scenario, untouched: its name and its contents are the game's.
bool BaseScenarioIs(const std::string &fileName, const std::string &sha256);
