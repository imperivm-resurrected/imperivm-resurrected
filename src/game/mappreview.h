// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "game/mapinfo.h"
#include "mods/mods.h"

struct MapPreviewData {
	bool failed = false;
	MapInfo info;
	MapImage image;
	MapOrigin origin;
	int humans = 0;
};

std::shared_ptr<const MapPreviewData> MapPreviewGet(const std::string &scenarioPath);
// The preview if it is read already, without asking for it: for what is not on screen.
std::shared_ptr<const MapPreviewData> MapPreviewPeek(const std::string &scenarioPath);
void MapPreviewWanted(const std::vector<std::string> &scenarioPaths, int first);
void MapPreviewReset();
