// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

namespace addr {

inline constexpr uintptr_t kChannelSetVolume = 0x00637260;
inline constexpr uintptr_t kCurrentDisplayModeIndex = 0x00676bd0;
inline constexpr uintptr_t kUpdateChannelVolumes = 0x006b0810;
inline constexpr uintptr_t kSaveGameOptionsToIni = 0x006e73e0;
inline constexpr uintptr_t kInitResolutionList = 0x006e8420;
inline constexpr uintptr_t kChangeDisplayModeTo = 0x00747220;
inline constexpr uintptr_t kWindowHandler = 0x00747a80;
inline constexpr uintptr_t kVtblFileStream = 0x007d81f8;
inline constexpr uintptr_t kVtblOggStream = 0x007d8250;
inline constexpr uintptr_t kCurrentResolutionIndex = 0x0082ef8c;
inline constexpr uintptr_t kShowSplash = 0x00830230;
inline constexpr uintptr_t kResolutionList = 0x00a83534;

}
