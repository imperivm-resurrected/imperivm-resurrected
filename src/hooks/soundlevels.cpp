// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/soundlevels.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

int SoundTypeVolume(int value, int fallback)
{
	return value >= 1 && value <= 100 ? value : fallback;
}

int SoundNatureVolume(int setting, int effects)
{
	if (setting == kSoundFollowEffects) return std::max(1, std::min(100, effects));
	return SoundTypeVolume(setting, std::max(1, std::min(100, effects)));
}

int SoundMasterPercent(int value)
{
	return std::max(0, std::min(100, value));
}

float SoundMasterGain(int percent)
{
	return SoundMasterPercent(percent) / 100.0f;
}

long SoundMovieVolume(int movie, bool mute, int master)
{
	const int pct = mute ? 0 : std::max(0, std::min(100, movie)) * SoundMasterPercent(master);
	if (pct <= 0) return -10000;
	return std::max(-10000L, static_cast<long>(2000.0 * std::log10(pct / 10000.0)));
}

int SoundPickDevice(const std::vector<std::string> &names, const std::string &wanted)
{
	if (wanted.empty()) return -1;
	const auto it = std::find(names.begin(), names.end(), wanted);
	return it == names.end() ? -1 : static_cast<int>(it - names.begin());
}
