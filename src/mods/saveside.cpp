// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/saveside.h"

#include <windows.h>

#include "mods/modpaths.h"

void MoveSaveSideFiles(const std::string &from, const std::string &to)
{
	for (const char *suffix : kSaveSideSuffixes) {
		if (modpaths::IsFile(from + suffix)) MoveFileExA((from + suffix).c_str(), (to + suffix).c_str(), MOVEFILE_REPLACE_EXISTING);
		else DeleteFileA((to + suffix).c_str());
	}
}
