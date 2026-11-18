// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/modoptions.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <atomic>

#include "core/json.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/mods.h"
#include "mods/modtext.h"

namespace {

std::mutex g_lock;
std::atomic<unsigned> g_activeRules{0};
std::vector<ModOption> g_options;
bool g_loaded = false;

using modtext::Lower;

bool ValidId(const std::string &id)
{
	if (id.empty() || id.size() > 32) return false;
	for (char c : id) {
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
	}
	return true;
}

void Load()
{
	std::vector<ModOption> found;
	for (const std::string &dir : ModsMountedDirs()) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((dir + "\\options\\*.json").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			const std::string path = dir + "\\options\\" + fd.cFileName;
			Json j;
			std::string err;
			if (!ReadJsonFile(path, j, &err) || j.type != Json::Object) {
				LogW("options", "game options: %s is not valid JSON (%s)", path.c_str(), err.c_str());
				continue;
			}
			std::string stem = fd.cFileName;
			stem.resize(stem.size() - 5);
			ModOption o;
			o.id = Lower(j.GetString("id", stem));
			if (!ValidId(o.id)) {
				LogW("options", "game options: %s: the id \"%s\" is not valid (letters, numbers, _ and -; up to 32)", path.c_str(), o.id.c_str());
				continue;
			}
			o.name = ModsLocalizedText(j, "name", o.id);
			o.description = ModsLocalizedText(j, "description", "");
			o.mod = dir;
			const Json *vals = j.Get("values");
			std::vector<std::string> ids;
			if (vals && vals->type == Json::Array) {
				for (const Json &v : vals->arr) {
					if (v.type == Json::String) {
						o.values.push_back(v.str);
						ids.push_back(Lower(v.str));
					}
					else if (v.type == Json::Object) {
						const std::string vid = v.GetString("id");
						o.values.push_back(ModsLocalizedText(v, "name", vid));
						ids.push_back(Lower(vid));
					}
				}
				if (o.values.size() < 2 || o.values.size() > 16) {
					LogW("options", "game options: %s: a list takes 2 to 16 values (it has %d); ignored", path.c_str(), static_cast<int>(o.values.size()));
					continue;
				}
				o.width = ModOptionWidth(o.values.size());
			}
			const Json *d = j.Get("default");
			if (d) {
				if (d->type == Json::Number) o.def = static_cast<int>(d->num);
				else if (d->type == Json::String) {
					for (size_t i = 0; i < ids.size(); i++) {
						if (ids[i] == Lower(d->str)) o.def = static_cast<int>(i);
					}
				}
				else o.def = j.GetBool("default", j.GetBool("default", false)) ? 1 : 0;
			}
			const int maxValue = o.values.empty() ? 1 : static_cast<int>(o.values.size()) - 1;
			o.def = (std::max)(0, (std::min)(o.def, maxValue));
			if (o.id == kSeasonOptionId || o.id == kDynamicSeasonsOptionId) continue;
			if (std::find_if(found.begin(), found.end(), [&](const ModOption &x) { return x.id == o.id; }) != found.end()) continue;
			found.push_back(o);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::sort(found.begin(), found.end(), [](const ModOption &a, const ModOption &b) { return a.id < b.id; });
	int used = 0;
	std::vector<ModOption> placed;
	for (ModOption &o : found) {
		if (!ModOptionPlace(o, used)) {
			LogW("options", "game options: \"%s\" does not fit (%d bits are reserved among all the options and %d are already used); ignored", o.id.c_str(), kModOptionBits, used);
			continue;
		}
		LogI("options", "game options: \"%s\" (%s)%s, bits 0x%x, default %d", o.id.c_str(), o.name.c_str(),
			o.values.empty() ? "" : (", list of " + std::to_string(o.values.size())).c_str(), o.bit, o.def);
		placed.push_back(o);
	}
	ModOption season;
	season.id = kSeasonOptionId;
	season.name = Tr("Season");
	season.description = Tr("Time of year the game is played in. It changes the look of the terrain and of some buildings.");
	season.values = {Tr("Spring"), Tr("Autumn"), Tr("Winter")};
	season.width = 2;
	season.pos = 9;
	season.bit = kSeasonBits;
	ModOption dynamic;
	dynamic.id = kDynamicSeasonsOptionId;
	dynamic.name = Tr("Dynamic seasons");
	dynamic.description = Tr("Spring, autumn and winter take turns during the game: the terrain, the trees and the buildings change, with a fade.");
	dynamic.values = {Tr("Off"), Tr("Every 10 seconds"), Tr("Every 10 minutes"), Tr("Every 20 minutes")};
	dynamic.width = 2;
	dynamic.pos = 11;
	dynamic.bit = kDynamicSeasonsBits;
	placed.insert(placed.begin(), dynamic);
	placed.insert(placed.begin(), season);
	g_options = placed;
}

}

const std::vector<ModOption> &ModOptions()
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_loaded) {
		g_loaded = true;
		Load();
	}
	return g_options;
}

unsigned ModOptionsMask()
{
	unsigned m = 0;
	for (const ModOption &o : ModOptions()) m |= o.bit;
	return m;
}

unsigned ModOptionsDefaults()
{
	unsigned m = 0;
	for (const ModOption &o : ModOptions()) m = ModOptionSet(o, m, o.def);
	return m;
}

unsigned ModRulesActive()
{
	return g_activeRules;
}

void ModRulesSetActive(unsigned rules)
{
	g_activeRules = rules;
}

int ModOptionValue(const std::string &id)
{
	const std::string want = Lower(id);
	for (const ModOption &o : ModOptions()) {
		if (o.id == want) return ModOptionGet(o, ModRulesActive());
	}
	return 0;
}
