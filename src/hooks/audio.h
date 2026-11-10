// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// Gives nature sounds and conversations their own volume on top of the game's sound options.
void InstallSoundLevels(bool knownExe);
// Applies the sound settings again after they change. Game thread.
void SoundLevelsApply();
void AudioSetBackgroundMute(bool mute);
// While the game runs under something drawn over it, like a spectator catching up with the live game.
void AudioSetCoverMute(bool mute);
