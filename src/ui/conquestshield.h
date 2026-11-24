// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

// The empty steps of a conquest's path: a heater shield with a metal rim and a dark field, smoothed at its edges. kShieldNext is the
// step the player takes next, with the accent rim; kShieldAhead one further on, quieter.
enum ConquestShieldStyle { kShieldAhead, kShieldNext };

// The pixels of a shield `w` x `h`, 0xAARRGGBB with straight alpha.
std::vector<uint32_t> ConquestShieldArt(int w, int h, int style);
