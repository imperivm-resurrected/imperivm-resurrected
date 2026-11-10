// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "hooks/soundlevels.h"

static void TypeVolumes()
{
	CHECK_EQ(SoundTypeVolume(1, 100), 1);
	CHECK_EQ(SoundTypeVolume(100, 50), 100);
	CHECK_EQ(SoundTypeVolume(0, 100), 100);
	CHECK_EQ(SoundTypeVolume(101, 70), 70);
	CHECK_EQ(SoundTypeVolume(-1, 100), 100);
}

static void Nature()
{
	CHECK_EQ(SoundNatureVolume(kSoundFollowEffects, 50), 50);
	CHECK_EQ(SoundNatureVolume(kSoundFollowEffects, 87), 87);
	CHECK_EQ(SoundNatureVolume(kSoundFollowEffects, 0), 1);
	CHECK_EQ(SoundNatureVolume(30, 87), 30);
	CHECK_EQ(SoundNatureVolume(500, 87), 87);
	CHECK_EQ(SoundNatureVolume(0, 40), 40);
}

static void Master()
{
	CHECK_EQ(SoundMasterPercent(-5), 0);
	CHECK_EQ(SoundMasterPercent(140), 100);
	CHECK(SoundMasterGain(100) == 1.0f);
	CHECK(SoundMasterGain(0) == 0.0f);
	CHECK(SoundMasterGain(50) == 0.5f);
}

static void Movie()
{
	CHECK_EQ(SoundMovieVolume(100, false, 100), 0L);
	CHECK_EQ(SoundMovieVolume(50, false, 100), -602L);
	CHECK_EQ(SoundMovieVolume(100, false, 50), -602L);
	CHECK_EQ(SoundMovieVolume(50, false, 50), -1204L);
	CHECK_EQ(SoundMovieVolume(50, true, 100), -10000L);
	CHECK_EQ(SoundMovieVolume(50, false, 0), -10000L);
	CHECK_EQ(SoundMovieVolume(0, false, 100), -10000L);
	CHECK_EQ(SoundMovieVolume(1, false, 1), -8000L);
}

static void Devices()
{
	const std::vector<std::string> names = {"Speakers (Realtek(R) Audio)", "Headphones (USB Audio)", "Speakers (Realtek(R) Audio)"};
	CHECK_EQ(SoundPickDevice(names, ""), -1);
	CHECK_EQ(SoundPickDevice(names, "Headphones (USB Audio)"), 1);
	CHECK_EQ(SoundPickDevice(names, "Speakers (Realtek(R) Audio)"), 0);
	CHECK_EQ(SoundPickDevice(names, "Headphones (Bluetooth)"), -1);
	CHECK_EQ(SoundPickDevice({}, "Headphones (USB Audio)"), -1);
}

int main()
{
	TypeVolumes();
	Nature();
	Master();
	Movie();
	Devices();
	return test::Summary("soundlevels");
}
