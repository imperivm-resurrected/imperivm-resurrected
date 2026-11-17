// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>
#include <vector>

#include "mods/mods.h"
#include "net/steamnet.h"

// A line "@package<TAB>id" (older versions skip it: it has too few fields) says that the room's ruleset is the package with that id itself, a package
// with civilizations that has no ruleset of its own: the global files of its root (mods/rootrules.h) are mounted. rulesetPackage gets the id.
std::vector<OnlineMod> DecodeModList(const std::string &text, std::string *rulesetPackage = nullptr);

// ruleset is the one the room mounts; the package that holds it is listed with the layer's id, name and key.
std::string EncodeModList(const std::vector<ModInfo> &mounted, const std::string &scenModId, int &optional, const ModRuleset &ruleset = ModRuleset());
