// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// The game keeps a volume of 1-100 for each kind of sound and turns it into -60..0 dB.
inline constexpr int kSoundFollowEffects = -1;

int SoundTypeVolume(int value, int fallback);
// Nature sounds follow the effects volume until the player gives them their own.
int SoundNatureVolume(int setting, int effects);
int SoundMasterPercent(int value);
float SoundMasterGain(int percent);
// The cutscene volume for IBasicAudio, in hundredths of dB.
long SoundMovieVolume(int movie, bool mute, int master);
// The index of the device called `wanted`, or -1 for the default one (empty name or not found).
int SoundPickDevice(const std::vector<std::string> &names, const std::string &wanted);
