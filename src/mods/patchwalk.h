// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// A patch file found below a patches folder: the game file it patches (lowercase, "/"), the file itself, and whether its changing operations are skipped
// (see PatchPolicy in mods/patchxml.h).
struct PatchFileFound {
	std::string rel;
	std::string path;
	bool addOnly = false;
};

// Every file below dir, however deep. addOnly is the decision for the whole folder (the root of a rules package whose own ruleset is not in use); the patches of
// the interface are exempt from it, so each file gets it here, in one place, instead of the walk handing it down.
std::vector<PatchFileFound> PatchWalk(const std::string &dir, bool addOnly);
