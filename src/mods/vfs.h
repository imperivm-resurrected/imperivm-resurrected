// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <set>
#include <string>
#include <vector>

void InstallVfsHooks(bool knownExe);

void VfsDumpAtStartup();
void VfsLogStats();

void VfsSetHidden(const std::set<std::string> &paths);
bool VfsIsHidden(const std::string &rel);
void InstallHiddenHooks();

bool VfsReadUnpatched(const std::string &rel, std::string &out);

// Rebuilds the patches and forgets the patched copy of rel (a path like "data/const.ini") so the next open serves the new bytes.
void VfsResetPatched(const std::string &rel);
