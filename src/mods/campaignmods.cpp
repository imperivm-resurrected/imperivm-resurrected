// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/campaignmods.h"

#include <cstring>
#include <string>
#include <vector>

#include "core/versioncmp.h"

namespace {

bool Same(const std::string &a, const std::string &b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

bool IsPack(const std::vector<std::string> &packIds, const std::string &id)
{
	for (const std::string &p : packIds) {
		if (!id.empty() && Same(p, id)) return true;
	}
	return false;
}

bool Has(const std::vector<std::string> &list, const std::string &folder)
{
	for (const std::string &f : list) {
		if (Same(f, folder)) return true;
	}
	return false;
}

const CampaignModEntry *Find(const std::vector<CampaignModEntry> &installed, const std::string &folder)
{
	for (const CampaignModEntry &m : installed) {
		if (Same(m.folder, folder)) return &m;
	}
	return nullptr;
}

}

std::vector<std::string> CampaignForeignMods(const std::vector<std::string> &packIds, const std::vector<std::string> &mounted,
	const std::vector<CampaignModEntry> &installed)
{
	std::vector<std::string> out;
	for (const std::string &folder : mounted) {
		const CampaignModEntry *m = Find(installed, folder);
		if (!m || !m->gameplay || IsPack(packIds, m->id)) continue;
		out.push_back(m->name.empty() ? m->folder : m->name);
	}
	return out;
}

bool CampaignProfileFolders(const std::vector<std::string> &packIds, const std::vector<std::string> &mounted,
	const std::vector<CampaignModEntry> &installed, std::vector<std::string> &folders)
{
	folders.clear();
	for (const std::string &id : packIds) {
		const CampaignModEntry *pick = nullptr;
		for (const CampaignModEntry &m : installed) {
			if (!Same(m.id, id)) continue;
			const bool inUse = Has(mounted, m.folder), pickInUse = pick && Has(mounted, pick->folder);
			if (!pick || (inUse && !pickInUse) || (inUse == pickInUse && CompareVersions(m.version, pick->version) > 0)) pick = &m;
		}
		if (!pick) {
			folders.clear();
			return false;
		}
		if (!Has(folders, pick->folder)) folders.push_back(pick->folder);
	}
	for (const std::string &folder : mounted) {
		const CampaignModEntry *m = Find(installed, folder);
		if (m && m->cosmetic && !m->gameplay && !IsPack(packIds, m->id) && !Has(folders, m->folder)) folders.push_back(m->folder);
	}
	return true;
}
