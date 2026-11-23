// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/profilehash.h"

#include <string.h>

#include "mods/modrules.h"

std::string ProfileGameplayHash(const ModProfile &p, const std::vector<ModInfo> &mods)
{
	std::vector<mods_impl::RulesPart> parts;
	for (const std::string &folder : p.mods) {
		for (const ModInfo &m : mods) {
			// Mounting finds the folder ignoring case (ModsFind): a profile that says "mymod" for "MyMod" still plays its rules.
			if (m.gameplay && _stricmp(m.folder.c_str(), folder.c_str()) == 0) {
				parts.push_back({m.id, m.gameplayHash});
			}
		}
	}
	return mods_impl::RulesFingerprint(parts);
}
