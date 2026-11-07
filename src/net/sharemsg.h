// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

struct MessageRange {
	uint8_t first;
	uint8_t last;
	constexpr bool Has(uint8_t type) const { return type >= first && type <= last; }
	constexpr bool Before(const MessageRange &other) const { return last < other.first; }
};

inline constexpr MessageRange kContentMessages{1, 19};
inline constexpr MessageRange kRejoinMessages{20, 39};
inline constexpr MessageRange kSpectatorMessages{40, 59};
inline constexpr MessageRange kReloadMessages{60, 79};
inline constexpr MessageRange kRoomMessages{80, 99};

static_assert(kContentMessages.Before(kRejoinMessages) && kRejoinMessages.Before(kSpectatorMessages) &&
	kSpectatorMessages.Before(kReloadMessages) && kReloadMessages.Before(kRoomMessages), "the message ranges of the share channel overlap");
