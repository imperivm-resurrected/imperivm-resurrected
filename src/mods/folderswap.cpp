// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/folderswap.h"

#include <windows.h>

FolderSwap SwapInFolder(const std::string &incoming, const std::string &target, const std::string &backup, unsigned long &error)
{
	error = 0;
	const bool replacing = GetFileAttributesA(target.c_str()) != INVALID_FILE_ATTRIBUTES;
	if (replacing && !MoveFileExA(target.c_str(), backup.c_str(), MOVEFILE_COPY_ALLOWED)) {
		error = GetLastError();
		return FolderSwap::kTargetBusy;
	}
	if (MoveFileExA(incoming.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) return FolderSwap::kMoved;
	error = GetLastError();
	if (!replacing) return FolderSwap::kFailed;
	return MoveFileExA(backup.c_str(), target.c_str(), 0) ? FolderSwap::kFailed : FolderSwap::kStranded;
}
