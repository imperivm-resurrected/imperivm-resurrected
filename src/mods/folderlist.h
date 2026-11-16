// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

// Folder names stored in one ini value, separated by '|'. Windows folder names ignore case, and so do the lookups.
std::vector<std::string> ParseFolderList(const std::string &text);
std::string JoinFolderList(const std::vector<std::string> &list);
bool HasFolder(const std::vector<std::string> &list, const std::string &folder);
// Removes every entry naming `folder`. False when there was none.
bool RemoveFolderFrom(std::vector<std::string> &list, const std::string &folder);
