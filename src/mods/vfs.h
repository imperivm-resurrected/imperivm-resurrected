// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "mods/layerdiag.h"

void InstallVfsHooks(bool knownExe);

void VfsDumpAtStartup();
void VfsLogStats();

void VfsSetHidden(const std::set<std::string> &paths);
bool VfsIsHidden(const std::string &rel);
void InstallHiddenHooks();

bool VfsReadUnpatched(const std::string &rel, std::string &out);

// The loose files of the mounted ruleset layer, to tell which ones the engine really opens (see mods/layerdiag.h). label names the ruleset,
// diskDir is its folder. An empty list switches the account off.
void VfsLayerDiagReset(const std::string &label, const std::string &diskDir, const std::vector<std::string> &files);
// The loose files of every active source (pack roots and the mounted ruleset), in the order they are mounted: the files that more than one
// of them has are reported, with the one that served them, the first time the engine opens them.
void VfsSharedFilesReset(const std::vector<FileSource> &sources, const std::vector<std::vector<std::string>> &files);
// Logs how many of them the engine has opened so far and which it has not (when says where in the game this is).
void VfsLayerDiagSummary(const char *when);

// Rebuilds the patches and forgets every patched copy.
void VfsResetPatchedAll();

// The file as the game reads it now, with the mounted patches applied.
bool VfsReadPatched(const std::string &rel, std::string &out);

// Rebuilds the patches and forgets the patched copy of rel (a path like "data/const.ini") so the next open serves the new bytes.
void VfsResetPatched(const std::string &rel);
