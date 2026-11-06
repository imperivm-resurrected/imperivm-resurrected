// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <vector>

// How much of each pixel a laurel branch covers, 0 to 255, row by row. The branch grows from the lower right to the upper left.
std::vector<uint8_t> LaurelBranchMask(int width, int height);
