// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "mods/modsleague.h"

#include <cstring>
#include <mutex>

#include "mods/downloads.h"
#include "mods/mods.h"

namespace {

std::mutex g_ownLock;
bool g_ownKnown = false;
uint64_t g_ownSerial = 0;
std::string g_ownProfile;
DWORD g_ownAt = 0;
std::vector<LeagueModRef> g_own;

// The source of each installed copy is read from disk: never more than this often while nothing changes.
const DWORD kOwnKeepMs = 5000;

std::vector<LeagueModRef> ComputeOwn(const ModProfile &mounted)
{
	std::vector<LeagueModRef> out;
	const std::vector<ModInfo> installed = ModsSummaries();
	for (const std::string &folder : mounted.mods) {
		for (const ModInfo &m : installed) {
			if (_stricmp(m.folder.c_str(), folder.c_str()) != 0 || !m.gameplay) continue;
			LeagueModRef r;
			r.name = m.name;
			r.version = m.version;
			const uint64_t file = DlInstalledGbFile(m);
			if (m.gbId && file) {
				r.gbId = m.gbId;
				r.fileId = file;
			}
			out.push_back(r);
			break;
		}
	}
	return out;
}

}

std::vector<LeagueModRef> ModsOwnLeagueMods()
{
	const uint64_t serial = ModsListSerial();
	const ModProfile mounted = ModsMountedProfile();
	std::lock_guard<std::mutex> lock(g_ownLock);
	const DWORD now = GetTickCount();
	if (g_ownKnown && serial == g_ownSerial && mounted.name == g_ownProfile && now - g_ownAt < kOwnKeepMs) return g_own;
	g_own = ComputeOwn(mounted);
	g_ownKnown = true;
	g_ownSerial = serial;
	g_ownProfile = mounted.name;
	g_ownAt = now;
	return g_own;
}

std::string ModsFolderOfGbFile(uint64_t gbId, uint64_t fileId)
{
	if (!gbId || !fileId) return std::string();
	for (const ModInfo &m : ModsSummaries()) {
		if (m.gbId == gbId && DlInstalledGbFile(m) == fileId) return m.folder;
	}
	return std::string();
}
