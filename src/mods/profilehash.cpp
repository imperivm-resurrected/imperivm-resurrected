// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/profilehash.h"

#include "mods/modrules.h"

std::string ProfileGameplayHash(const ModProfile &p, const std::vector<ModInfo> &mods)
{
	std::vector<mods_impl::RulesPart> parts;
	for (const std::string &folder : p.mods) {
		for (const ModInfo &m : mods) {
			if (m.folder == folder && m.gameplay) {
				parts.push_back({m.id, m.gameplayHash});
			}
		}
	}
	return mods_impl::RulesFingerprint(parts);
}
