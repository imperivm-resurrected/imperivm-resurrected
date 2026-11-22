// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/campaignpacks.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "core/versioncmp.h"
#include "i18n/tr.h"
#include "mods/downloads.h"
#include "mods/mods.h"

namespace {
// The GameBanana pages (gamebanana.com/mods/743343 and 743344) and their files. A file id of 0 means not published yet: the card says so.
// The sizes and MD5 are those of tools/build-campaign-packs.js for the converters of this version.
const std::vector<CampaignPack> &Packs()
{
	static const std::vector<CampaignPack> packs = {
		{1, "Imperivm I: La guerra de las Galias", "1.0", 743343, 1897476, "dba06e81111354cecccb2b49ef7ee8be", 189941222, {"imperivm1-campaign", "imperivm1-campaign-data"}},
		{2, "Imperivm II: the campaigns", "0.2.0", 743344, 1897477, "378025317f36bd1f9d6bd163d1fdf72c", 167179270,
			{"imperivm2-data", "imperivm2-hannibals-campaigns", "imperivm2-the-punic-wars", "imperivm2-numantia"}},
	};
	return packs;
}

bool Published(const CampaignPack &pack) { return pack.gbFileId != 0; }
}

const CampaignPack *CampaignPackFor(int game)
{
	for (const CampaignPack &pack : Packs()) {
		if (pack.game == game) return &pack;
	}
	return nullptr;
}

CampaignPackState CampaignPackStateOf(const CampaignPack &pack, const std::vector<std::pair<std::string, std::string>> &installed)
{
	bool any = false, all = true, older = false;
	for (const std::string &id : pack.ids) {
		const std::pair<std::string, std::string> *found = nullptr;
		for (const auto &m : installed) {
			if (_stricmp(m.first.c_str(), id.c_str()) == 0 && (!found || CompareVersions(m.second, found->second) > 0)) found = &m;
		}
		any = any || found;
		all = all && found;
		older = older || (found && CompareVersions(found->second, pack.version) < 0);
	}
	if (!any) return CampaignPackState::kMissing;
	return all && !older ? CampaignPackState::kInstalled : CampaignPackState::kOutdated;
}

CampaignPackView CampaignPackViewOf(int game)
{
	CampaignPackView v;
	const CampaignPack *pack = CampaignPackFor(game);
	if (!pack) return v;
	v.known = true;
	v.bytes = pack->bytes;
	v.published = Published(*pack);
	std::vector<std::pair<std::string, std::string>> installed;
	bool waiting = false;
	for (const ModInfo &m : ModsList()) {
		installed.push_back({m.id, m.version});
		for (const std::string &id : pack->ids) waiting = waiting || (_stricmp(m.id.c_str(), id.c_str()) == 0 && DlPendingFor(m.folder));
	}
	v.state = CampaignPackStateOf(*pack, installed);
	// The update is downloaded and goes in when the game restarts: asking for it again would download it again.
	if (v.state == CampaignPackState::kOutdated && waiting) v.state = CampaignPackState::kInstalled;
	CampaignPackProgress(game, v);
	return v;
}

void CampaignPackProgress(int game, CampaignPackView &v)
{
	const CampaignPack *pack = CampaignPackFor(game);
	v.downloading = v.unpacking = v.queued = false;
	v.done = v.total = 0;
	if (!pack) return;
	const DownloadStatus s = DlStatus();
	const std::string name = Tr(pack->name);
	if (!s.active || s.what.compare(0, name.size(), name) != 0) {
		v.queued = DlBundleQueued(pack->gbFileId);
		return;
	}
	v.downloading = true;
	v.unpacking = s.unpacking;
	v.done = s.unpacking ? s.unpacked : s.done;
	v.total = s.unpacking ? s.unpackTotal : s.total ? s.total : pack->bytes;
}

bool CampaignPackDownload(int game)
{
	const CampaignPack *pack = CampaignPackFor(game);
	if (!pack) return false;
	if (!Published(*pack)) {
		LogI("download", "campaign %s: not published on GameBanana yet", pack->name.c_str());
		return false;
	}
	CatalogEntry e;
	e.source = "gamebanana";
	e.name = Tr(pack->name);
	e.version = pack->version;
	e.url = "https://gamebanana.com/dl/" + std::to_string(pack->gbFileId);
	e.md5 = pack->md5;
	e.bytes = pack->bytes;
	e.gbId = pack->gbId;
	e.fileId = pack->gbFileId;
	e.authors = {"Imperivm Resurrected"};
	LogI("download", "campaign %s %s: downloading file %llu", pack->name.c_str(), pack->version.c_str(), static_cast<unsigned long long>(pack->gbFileId));
	DlInstallBundle(e);
	return true;
}
