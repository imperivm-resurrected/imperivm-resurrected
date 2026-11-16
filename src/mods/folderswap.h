// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

enum class FolderSwap {
	kMoved,
	kTargetBusy,
	kFailed,
	kStranded,
};

// Moves the folder `incoming` to `target`. What is already at `target` is first moved to `backup`, and is put back when `incoming`
// cannot take its place. kTargetBusy: the old folder could not be moved aside. kFailed: `incoming` could not be moved and `target`
// is as it was. kStranded: the same, but the old folder could not be put back and is still at `backup`. `error` holds the Windows
// error of the move that failed.
FolderSwap SwapInFolder(const std::string &incoming, const std::string &target, const std::string &backup, unsigned long &error);
