// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "addr/classcache.h"
#include "addr/world.h"
#include "core/crashlog.h"
#include "core/log.h"
#include "game/entitycache.h"
#include "game/entitywalk.h"

namespace {
typedef void(__cdecl *UnloadEntity_t)(const char *name);
const unsigned kCapacity = 8192;

std::string Folder(const char *name)
{
	std::string s(name);
	for (char &c : s) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s.substr(0, s.find_last_of('/') == std::string::npos ? 0 : s.find_last_of('/'));
}

// Only plain data in here, so the __try can protect it. -1: a fault.
int Walk(EntityRawEntry *out)
{
	int n;
	g_crashLogQuiet++;
	__try {
		n = static_cast<int>(EntityWalkCache(*reinterpret_cast<const uint32_t *const *>(addr::kEntityNameHead), out, kCapacity));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		n = -1;
	}
	g_crashLogQuiet--;
	return n;
}

bool Unload(const char *name)
{
	bool ok = true;
	g_crashLogQuiet++;
	__try {
		reinterpret_cast<UnloadEntity_t>(addr::kUnloadEntity)(name);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ok = false;
	}
	g_crashLogQuiet--;
	return ok;
}
}

EntityInvalidation InvalidateEntityFolders(const std::vector<std::string> &folders, const std::vector<unsigned> &decorEntities)
{
	EntityInvalidation result;
	std::vector<EntityRawEntry> entries(kCapacity);
	const int n = Walk(entries.data());
	// A full buffer may have left entities out: not knowing, so not done.
	if (n < 0 || static_cast<unsigned>(n) >= kCapacity) return result;
	result.cached = static_cast<unsigned>(n);
	// The names first: UnloadEntity changes the map the walk read.
	std::vector<std::string> names;
	for (int i = 0; i < n; i++) {
		if (std::find(folders.begin(), folders.end(), Folder(entries[i].name)) == folders.end()) continue;
		names.emplace_back(entries[i].name);
		if (std::find(decorEntities.begin(), decorEntities.end(), entries[i].entity) != decorEntities.end()) result.decorHit = true;
	}
	if (result.decorHit) {
		result.ok = true;
		return result;
	}
	for (const std::string &name : names) {
		if (!Unload(name.c_str())) return result;
		result.unloaded.push_back(name);
	}
	result.ok = true;
	return result;
}
