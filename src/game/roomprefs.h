// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "mods/modoptions.h"

std::string RoomPrefEncode(const std::string &text);
std::string RoomPrefDecode(const std::string &text);
int RoomPrefItemIndex(const std::vector<std::string> &items, const std::string &text);
unsigned RoomRulesFromSaved(const std::vector<ModOption> &options, bool haveSaved, unsigned saved, unsigned baseMask, const std::map<std::string, int> &values);
