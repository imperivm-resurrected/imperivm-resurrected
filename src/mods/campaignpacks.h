// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The official campaigns of Imperivm I and II: one archive each on GameBanana with several content packs (the adventures, and the
// data with their classes), distributed with the rights holder's permission. This DLL asks for the exact file its classes match.
struct CampaignPack {
	int game = 0;
	std::string name;
	std::string version;
	uint64_t gbId = 0;
	uint64_t gbFileId = 0;
	std::string md5;
	uint64_t bytes = 0;
	// The ids of the content packs the archive installs.
	std::vector<std::string> ids;
};

enum class CampaignPackState { kMissing, kOutdated, kInstalled };

struct CampaignPackView {
	bool known = false;
	CampaignPackState state = CampaignPackState::kMissing;
	uint64_t bytes = 0;
	bool published = false;
	bool downloading = false;
	bool unpacking = false;
	// Waiting behind another download.
	bool queued = false;
	uint64_t done = 0, total = 0;
};

// game: 1 Imperivm I, 2 Imperivm II; null for any other.
const CampaignPack *CampaignPackFor(int game);
// installed: the id and version of every content pack in the mods folder.
CampaignPackState CampaignPackStateOf(const CampaignPack &pack, const std::vector<std::pair<std::string, std::string>> &installed);
// Reads the mods folder: call it when the list of content packs changes.
CampaignPackView CampaignPackViewOf(int game);
// Only the download's progress, cheap enough for every frame.
void CampaignPackProgress(int game, CampaignPackView &v);
// Queues the download; false when the campaign is not published yet.
bool CampaignPackDownload(int game);
