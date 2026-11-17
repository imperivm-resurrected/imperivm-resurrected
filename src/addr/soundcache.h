// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The table of sound entities of the game's sound manager (see game/soundreload.h).
namespace addr {

// GetPtrPool(): the pool the game allocates small objects from (a virtual object: allocate in slot 1, free in slot 3).
inline constexpr uintptr_t kGetPtrPool = 0x006468f0;
// CSoundManager::LoadSoundEntities(this): reads data/sound entities/*.xml into the table of the manager (count 432 bytes in, table 436 bytes in).
inline constexpr uintptr_t kLoadSoundEntities = 0x006af9c0;
// The two critical sections the sound code takes: every call that plays or stops a sound holds the first, the manager's table the second (always in this order).
inline constexpr uintptr_t kDirectMusicLock = 0x009f23d8;
inline constexpr uintptr_t kSoundManagerLock = 0x00a78428;

}
