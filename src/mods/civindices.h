// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "core/json.h"

inline constexpr int kCivIndexFirst = 10, kCivIndexLast = 127;

struct CivIndex {
	std::string id;
	int index = 0;
};

// IDs are normalized; callers validate explicit slots and resolve duplicate IDs/slots.
std::vector<std::string> CivIndicesAssign(std::vector<CivIndex> &civs);
void CivIndicesSet(const std::vector<CivIndex> &civs, bool automatic);
bool CivIndicesAutomatic();
Json CivIndicesToJson();

enum class CivIndicesStatus { Compatible, Missing, Invalid, Mismatch, DifferentFile };

// ponytail: changed civ sets require the original profile; no live save/script remapping.
CivIndicesStatus CivIndicesCheck(const Json *saved);
CivIndicesStatus CivIndicesCheckSaved(const Json *saved, const std::string &savedHash, const std::string &fileHash);
