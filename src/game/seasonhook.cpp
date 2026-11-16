// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <string>
#include <vector>

#include "addr/state.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/seasondecors.h"
#include "game/gamehook.h"
#include "hooks/gamefiles.h"

namespace {
const unsigned kReadMode = 1;
const char kDecorsPath[] = "MapObjects/decors/decors.ini";
const int kSeasonOffset = 0x174;

typedef void *(__cdecl *LoadDecorsIni_t)(const char *path);
LoadDecorsIni_t g_origLoadDecorsIni = nullptr;

void *__cdecl HookLoadDecorsIni(const char *path)
{
	void *error = g_origLoadDecorsIni(path);
	if (error) return error;
	const char *game = *reinterpret_cast<const char **>(addr::kGame);
	const int season = game ? *reinterpret_cast<const int *>(game + kSeasonOffset) : 0;
	if (season != 2 && season != 3) return error;
	std::vector<char> bytes;
	if (!ReadGameFile(kDecorsPath, kReadMode, bytes) || bytes.empty()) return error;
	void **entities = reinterpret_cast<void **>(addr::kDecorEntities);
	unsigned replaced = 0;
	for (const SeasonDecorPair &pair : SeasonDecorPairs(std::string(bytes.begin(), bytes.end()), season)) {
		if (!entities[pair.twin]) continue;
		entities[pair.base] = entities[pair.twin];
		replaced++;
	}
	LogI("room", "decorations for the %s: %u types use their %s art", season == 2 ? "autumn" : "winter", replaced, season == 2 ? "autumn" : "winter");
	return error;
}
}

void InstallSeasonDecors(bool knownExe)
{
	if (!knownExe) return;
	if (!HookFunction(addr::kLoadDecorsIni, &HookLoadDecorsIni, &g_origLoadDecorsIni)) {
		g_origLoadDecorsIni = nullptr;
		LogE("room", "could not hook the loading of the decorations");
	}
}
