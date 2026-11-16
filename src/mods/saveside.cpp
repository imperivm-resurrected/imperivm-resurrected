// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/saveside.h"

#include <windows.h>

#include "mods/modpaths.h"

bool MoveSaveSideFiles(const std::string &from, const std::string &to)
{
	bool ok = true;
	for (const char *suffix : kSaveSideSuffixes) {
		const std::string src = from + suffix, dst = to + suffix;
		if (!modpaths::IsFile(src)) {
			DeleteFileA(dst.c_str());
			continue;
		}
		if (MoveFileExA(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) continue;
		// A file held open without delete sharing (a picture viewer, an antivirus) cannot be moved but can be copied. The original
		// left behind belongs to no save any more, so the cleaning of orphans removes it later.
		if (CopyFileA(src.c_str(), dst.c_str(), FALSE)) {
			DeleteFileA(src.c_str());
			continue;
		}
		ok = false;
	}
	return ok;
}
