// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The game copies some CONST.INI values into globals once per process and never looks at the ini again. These are the words
// that tell it "already copied" (read in the decompilation; each one is written only by the function that fills the copy).
namespace addr {

// Values -1 until the loader has run (its first test is "== -1" or "< 0").
constexpr uintptr_t kConstSentinelGuards[] = {0x0081b238, 0x0081b244, 0x0081b248, 0x00821170, 0x0082116c, 0x00821298, 0x0082129c, 0x008212a4, 0x00824be8, 0x00824d14, 0x00824d18, 0x00824d1c, 0x00824d20, 0x00824d24};
// Bit flags (0 until the loader has run), one bit for each value or group of values.
constexpr uintptr_t kConstFlagGuards[] = {0x00996c84, 0x00996e38, 0x00996f64, 0x009a70a0, 0x009a7160, 0x009a7168, 0x009a7170, 0x009a717c, 0x009bdb4c, 0x009bf7c4, 0x009c0834, 0x009c083c, 0x009c0858, 0x009c0904, 0x009c0910, 0x009c0960};
// Read again only while they are 0 (the copy is also made by the constructor, which always reads).
constexpr uintptr_t kConstZeroGuards[] = {0x009a7080, 0x009a7084};
inline constexpr uintptr_t kDamageAmplify = 0x009bf7a0;
// Set to 1 by the loader of DamageAmplify and the settlement constants (also in the flag list above).
inline constexpr uintptr_t kGameplayConstantsLoaded = 0x009bf7c4;
// Bits 0 and 1 of this word guard LoyaltyRadius and LoyaltyRadiusTownhall; bit 2 caches a class pointer and is left alone.
inline constexpr uintptr_t kLoyaltyRadiusFlags = 0x009bf7d4;

}
