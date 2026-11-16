// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

// The files kept beside a save, named after it: "<save>.bmp" and so on.
inline constexpr const char *kSaveSideSuffixes[] = {".bmp", ".cameras", ".mods.json", ".stats", ".objectives"};

// Moves the side files of the save `from` to the save `to`, copying the ones that cannot be moved. A side file that `from` lacks is
// deleted at `to`. False when one could be neither moved nor copied: it is still beside `from`.
bool MoveSaveSideFiles(const std::string &from, const std::string &to);
