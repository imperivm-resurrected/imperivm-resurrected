// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The campaigns of Imperivm I and II are played as in the original: only their own content packs and cosmetic mods.
struct CampaignModEntry {
	std::string folder, id, name, version;
	bool gameplay = false;
	// It can go into any profile without changing the game (ModsLooseProfileWith's rule).
	bool cosmetic = false;
};

// The names of the gameplay mods of the mounted profile that are not one of the campaign's packs, in the profile's order.
std::vector<std::string> CampaignForeignMods(const std::vector<std::string> &packIds, const std::vector<std::string> &mounted,
	const std::vector<CampaignModEntry> &installed);

// The folders of the profile the campaign is played with: one installed copy of each pack (the mounted one, else the newest) and the
// cosmetic mods of the mounted profile. False, with `folders` empty, when a pack is not installed.
bool CampaignProfileFolders(const std::vector<std::string> &packIds, const std::vector<std::string> &mounted,
	const std::vector<CampaignModEntry> &installed, std::vector<std::string> &folders);
