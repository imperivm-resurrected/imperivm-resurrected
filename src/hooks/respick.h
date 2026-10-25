// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <vector>

struct ResSize {
	int w = 0, h = 0;
};

enum class ResChoice { kSaved, kScreen, kFirst, kNone };

struct ResPick {
	int index = -1;
	ResChoice why = ResChoice::kNone;
};

ResPick PickStartResolution(const std::vector<ResSize> &list, ResSize saved, ResSize savedOn, ResSize screen, bool savedIsMonitorMode = false);

std::vector<ResSize> BuildResolutionList(const std::vector<ResSize> &windows, ResSize screen, const std::vector<ResSize> &extended, bool monitorModesBeyondScreen = false);

int KeepResolution(const std::vector<ResSize> &list, ResSize size, ResSize screen);
