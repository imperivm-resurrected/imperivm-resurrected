// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/leaguemods.h"

// The gameplay mods of the profile the game mounted, as the league asks for them: each one's GameBanana mod and the file it was
// installed from, zeros for one not installed from GameBanana. Worked out again only when the mods or the mounted profile change, or
// every few seconds; any thread.
std::vector<LeagueModRef> ModsOwnLeagueMods();

// The installed copy of that GameBanana file (installed from it, whatever its version says); empty when there is none.
std::string ModsFolderOfGbFile(uint64_t gbId, uint64_t fileId);
