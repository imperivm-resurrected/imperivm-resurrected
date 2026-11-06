// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

enum Ornament {
	kOrnamentLaurel,  // grows from the lower right to the upper left
	kOrnamentRule,  // a line that swells towards a central lozenge and fades out at both ends
	kOrnamentScreen,
	kOrnamentSound,
	kOrnamentSwords,
	kOrnamentGear,
	kOrnamentKeys,
	kOrnamentCount
};

// How much of each pixel the ornament covers, 0 to 255, row by row.
std::vector<uint8_t> OrnamentMask(Ornament ornament, int width, int height);
