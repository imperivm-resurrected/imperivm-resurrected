// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/unitpreload.h"

#include <windows.h>

#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"
#include "core/log.h"
#include "game/civs.h"
#include "game/cmdcheck.h"
#include "game/gamelayout.h"

// CVXScriptClass: its id at +4 and its entity file at +0x74 ("Units/RVillager/RVillager.ent.xml"). GetEntity resolves it once
// (+0x224) through LoadEntity, whose cache by file name keeps it for the session: the same call the game makes when a unit first
// shows up, made here before play. Only graphics: what the simulation reads of an entity is the same whenever it is loaded.

namespace {

const int kClassId = 4, kClassEntity = 0x74, kClassResolved = 0x224;
const size_t kMaxClasses = 4096;

typedef void *(__fastcall *GetEntity_t)(void *cls);
typedef const char *(__cdecl *GetRaceLetter_t)(int race, int lower);

void *g_preloadedFor = nullptr;

void Collect(char *node, char *head, std::vector<char *> &out, int depth)
{
	if (!node || node == head || depth > 64 || out.size() >= kMaxClasses) return;
	Collect(*reinterpret_cast<char **>(node), head, out, depth + 1);
	char *cls = *reinterpret_cast<char **>(node + 0x28);
	if (cls) out.push_back(cls);
	Collect(*reinterpret_cast<char **>(node + 8), head, out, depth + 1);
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

}

void UnitArtPreload()
{
	void *game = *reinterpret_cast<void **>(addr::kGame);
	if (!game || game == g_preloadedFor) return;
	g_preloadedFor = game;
	char *registry = *reinterpret_cast<char **>(addr::kRegistry);
	if (!registry || !*reinterpret_cast<int *>(registry + 0xc)) return;
	char *head = *reinterpret_cast<char **>(registry + 4);
	if (!head) return;

	std::set<std::string> letters;
	const auto letterOf = reinterpret_cast<GetRaceLetter_t>(addr::kGetRaceLetter);
	for (int player = 1; player <= layout::kSettingsEntryCount; player++) {
		const int race = CivsPlayerCiv(player);
		if (race < 0) continue;
		const char *letter = letterOf(race, 1);
		if (letter && *letter) letters.insert(Lower(letter));
	}
	if (letters.empty()) return;

	std::vector<char *> classes;
	Collect(*reinterpret_cast<char **>(head + 4), head, classes, 0);
	LARGE_INTEGER from, to, freq;
	QueryPerformanceCounter(&from);
	const auto getEntity = reinterpret_cast<GetEntity_t>(addr::kScriptClassGetEntity);
	int loaded = 0;
	for (char *cls : classes) {
		if (*reinterpret_cast<int *>(cls + kClassResolved)) continue;
		const std::string entity = Lower(GameStringAt(cls + kClassEntity));
		if (entity.compare(0, 6, "units/") != 0) continue;
		const std::string id = Lower(GameStringAt(cls + kClassId));
		bool ours = false;
		for (const std::string &l : letters) ours = ours || id.compare(0, l.size(), l) == 0;
		if (!ours) continue;
		getEntity(cls);
		loaded++;
	}
	QueryPerformanceCounter(&to);
	QueryPerformanceFrequency(&freq);
	std::string list;
	for (const std::string &l : letters) list += l;
	LogI("game", "unit graphics preloaded: %d classes of the civilizations '%s' in %.0f ms", loaded, list.c_str(),
		(to.QuadPart - from.QuadPart) * 1000.0 / freq.QuadPart);
}
