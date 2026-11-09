// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>

#include "game/gamemodes.h"
#include "game/civs.h"
#include "core/json.h"
#include "core/log.h"
#include "i18n/tr.h"
#include "mods/features.h"
#include "mods/mods.h"

namespace {
const unsigned kOptsNoBonuses = 0x04;
const unsigned kOptsNoFog = 0x08;
const unsigned kOptsExplored = 0x0c;
const unsigned kOptsGold = 0x40;
const unsigned kOptsPopulation = 0x44;
const unsigned kOptsMode = 0x60;
const unsigned kOptsModeSize = 0x20;
const unsigned kSettingsMode = 0xd34;
const unsigned kSettingsRaces = 0x5c;
const unsigned kPlayerEntries = 0xa4;
const unsigned kPlayerEntrySize = 0xc0;
const unsigned kEntryType = 0x08;
const unsigned kEntryBonus = 0x38;
const unsigned kEntrySlot = 0xa0;
const int kRandomRace = 8;

std::mutex g_lock;
std::vector<std::unique_ptr<GameMode>> g_modes, g_retired;
std::map<std::string, std::vector<std::string>> g_errors;

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

void AddError(const std::string &folder, const std::string &message)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		std::vector<std::string> &list = g_errors[folder];
		if (std::find(list.begin(), list.end(), message) == list.end()) list.push_back(message);
	}
	LogW("room", "modes (%s): %s", folder.empty() ? "imperivm-resurrected\\modes" : folder.c_str(), message.c_str());
}

int ResolveCiv(const std::string &name)
{
	const int base = GameCivByName(name);
	if (base >= 0) return base;
	const Civ *c = CivByName(name);
	return c ? c->index : -1;
}

void LoadMode(const std::string &folder, const std::string &path, const std::string &fileName, std::vector<std::unique_ptr<GameMode>> &out)
{
	Json j;
	std::string err;
	if (!ReadJsonFile(path, j, &err) || j.type != Json::Object) {
		AddError(folder, TrFmt("modes\\%s: not valid JSON (%s)", {fileName, err.empty() ? Tr("not an object") : err}));
		return;
	}
	auto m = std::make_unique<GameMode>();
	m->modFolder = folder;
	m->key = j.GetString("mode", fileName.substr(0, fileName.rfind('.')));
	if (m->key.empty() || m->key.size() >= kOptsModeSize) {
		AddError(folder, TrFmt("modes\\%s: the mode \"%s\" must have between 1 and %s characters, like the name of its script in data\\GameScripts without .vs",
			{fileName, m->key, std::to_string(kOptsModeSize - 1)}));
		return;
	}
	static const char *const kKnown[] = {"mode", "gold", "population", "noFog", "explored", "bonus", "civilizations"};
	for (const auto &kv : j.obj) {
		if (std::none_of(std::begin(kKnown), std::end(kKnown), [&](const char *k) { return kv.first == k; })) {
			AddError(folder, TrFmt("modes\\%s: \"%s\" is not a setting of a mode", {fileName, kv.first}));
		}
	}
	auto number = [&](const char *key, int low, int high, int &to) {
		const Json *v = j.Get(key);
		if (!v) return;
		if (v->type != Json::Number || v->num < low || v->num > high || v->num != static_cast<int>(v->num)) {
			AddError(folder, TrFmt("modes\\%s: \"%s\" is a whole number between %s and %s", {fileName, key, std::to_string(low), std::to_string(high)}));
			return;
		}
		to = static_cast<int>(v->num);
	};
	auto flag = [&](const char *key, int &to) {
		const Json *v = j.Get(key);
		if (!v) return;
		if (v->type != Json::Bool) {
			AddError(folder, TrFmt("modes\\%s: \"%s\" is true or false", {fileName, key}));
			return;
		}
		to = v->b ? 1 : 0;
	};
	number("gold", 0, 10000000, m->gold);
	number("population", 1, 100000, m->population);
	number("bonus", 0, 99, m->bonus);
	flag("noFog", m->noFog);
	flag("explored", m->explored);
	if (const Json *list = j.Get("civilizations")) {
		if (list->type != Json::Array) {
			AddError(folder, TrFmt("modes\\%s: \"civilizations\" is a list of civilization names", {fileName}));
		}
		else {
			for (const Json &c : list->arr) {
				const int race = c.type == Json::String ? ResolveCiv(c.str) : -1;
				if (race < 0) {
					AddError(folder, TrFmt("modes\\%s: \"%s\" is not a civilization of the game or of an active content pack", {fileName, c.type == Json::String ? c.str : c.Dump()}));
					continue;
				}
				if (std::find(m->civs.begin(), m->civs.end(), race) == m->civs.end()) {
					m->civs.push_back(race);
					m->civNames.push_back(c.str);
				}
			}
		}
	}
	out.push_back(std::move(m));
}

void GameModesRebuild()
{
	std::vector<std::unique_ptr<GameMode>> found;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_errors.clear();
	}
	std::vector<std::pair<std::string, std::string>> roots;
	for (const std::string &dir : ModsMountedDirs()) roots.push_back({ModsPackFolder(dir), dir + "\\modes"});
	roots.push_back({"", GameDir() + "\\imperivm-resurrected\\modes"});
	for (const auto &root : roots) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((root.second + "\\*.json").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) LoadMode(root.first, root.second + "\\" + fd.cFileName, fd.cFileName, found);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::vector<std::unique_ptr<GameMode>> unique;
	for (auto &m : found) {
		const auto same = std::find_if(unique.begin(), unique.end(), [&](const std::unique_ptr<GameMode> &u) { return _stricmp(u->key.c_str(), m->key.c_str()) == 0; });
		if (same != unique.end()) {
			AddError(m->modFolder, TrFmt("the mode \"%s\" is already set by content pack %s: ignored", {m->key, (*same)->modFolder}));
			continue;
		}
		std::string what;
		if (m->gold >= -1) what += " gold=" + std::to_string(m->gold);
		if (m->population >= 0) what += " population=" + std::to_string(m->population);
		if (m->noFog >= 0) what += std::string(" noFog=") + (m->noFog ? "yes" : "no");
		if (m->explored >= 0) what += std::string(" explored=") + (m->explored ? "yes" : "no");
		if (m->bonus >= 0) what += " bonus=" + std::to_string(m->bonus);
		if (!m->civs.empty()) what += " civilizations=" + std::to_string(m->civs.size());
		LogI("room", "mode \"%s\" (%s):%s", m->key.c_str(), m->modFolder.empty() ? "imperivm-resurrected\\modes" : m->modFolder.c_str(), what.empty() ? " nothing" : what.c_str());
		unique.push_back(std::move(m));
	}
	std::lock_guard<std::mutex> lock(g_lock);
	for (auto &old : g_modes) g_retired.push_back(std::move(old));
	g_modes = std::move(unique);
}

std::vector<std::string> GameModeProblemsFor(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_errors.find(folder);
	return it == g_errors.end() ? std::vector<std::string>() : it->second;
}

const bool g_featureRegistered = (ModsRegisterFeature({45, &GameModesRebuild, &GameModeProblemsFor}), true);

std::string GameStringText(const char *s)
{
	const unsigned size = *reinterpret_cast<const unsigned *>(s + 0x14);
	const unsigned capacity = *reinterpret_cast<const unsigned *>(s + 0x18);
	const char *text = capacity < 0x10 ? s + 4 : *reinterpret_cast<const char *const *>(s + 4);
	return text && size < 256 ? std::string(text, size) : std::string();
}

int FirstAllowedRace(const GameMode &mode, bool ai)
{
	for (int race : mode.civs) {
		if (race >= 10) {
			const Civ *c = CivByIndex(race);
			if (!c) continue;
			return ai && !c->ai ? c->base : race;
		}
		for (const Civ &c : CivsList()) {
			if (c.replacesBase && c.base == race && (!ai || c.ai)) return c.index;
		}
		return race;
	}
	return -1;
}
}

const GameMode *GameModeFor(const char *key)
{
	if (!key || !key[0]) return nullptr;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const auto &m : g_modes) {
		if (_stricmp(m->key.c_str(), key) == 0) return m.get();
	}
	return nullptr;
}

const GameMode *GameModeOfSettings(const char *settings)
{
	if (!settings) return nullptr;
	return GameModeFor(GameStringText(settings + kSettingsMode).c_str());
}

bool GameModeAllows(const GameMode &mode, int race)
{
	if (mode.civs.empty() || race == kRandomRace) return true;
	if (std::find(mode.civs.begin(), mode.civs.end(), race) != mode.civs.end()) return true;
	if (race >= 10) {
		const Civ *c = CivByIndex(race);
		if (c && c->variant && std::find(mode.civs.begin(), mode.civs.end(), c->base) != mode.civs.end()) return true;
	}
	return false;
}

std::vector<int> GameModeRandomPool(const GameMode &mode, bool ai)
{
	std::vector<int> pool;
	const std::vector<Civ> civs = CivsList();
	for (int race : mode.civs) {
		if (race >= 10) {
			const Civ *c = CivByIndex(race);
			if (c && (!ai || c->ai)) pool.push_back(race);
			continue;
		}
		bool replaced = false;
		for (const Civ &c : civs) {
			if (!c.replacesBase || c.base != race) continue;
			replaced = true;
			if (c.random && (!ai || c.ai)) pool.push_back(c.index);
		}
		if (!replaced) pool.push_back(race);
	}
	std::sort(pool.begin(), pool.end());
	pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
	return pool;
}

int GameModeFirstAllowed(const GameMode &mode, bool ai)
{
	return FirstAllowedRace(mode, ai);
}

bool GameModeLocksOption(const GameMode *mode, int optionId)
{
	if (!mode) return false;
	switch (optionId) {
	case 0x1004: return mode->population >= 0;
	case 0x1005: return mode->gold >= -1;
	case 0x1007: return mode->noFog >= 0;
	case 0x1008: return mode->explored >= 0;
	case 0x10a7: return mode->bonus >= 0;
	default: return false;
	}
}

void GameModeApplyOptions(char *opts)
{
	if (!opts) return;
	char key[kOptsModeSize + 1] = {};
	memcpy(key, opts + kOptsMode, kOptsModeSize);
	const GameMode *m = GameModeFor(key);
	if (!m) return;
	int &gold = *reinterpret_cast<int *>(opts + kOptsGold);
	int &population = *reinterpret_cast<int *>(opts + kOptsPopulation);
	int &noFog = *reinterpret_cast<int *>(opts + kOptsNoFog);
	int &explored = *reinterpret_cast<int *>(opts + kOptsExplored);
	int &noBonuses = *reinterpret_cast<int *>(opts + kOptsNoBonuses);
	bool changed = false;
	auto set = [&changed](int &field, int value) {
		if (field != value) changed = true;
		field = value;
	};
	if (m->gold >= -1) set(gold, m->gold);
	if (m->population >= 0) set(population, m->population);
	if (m->noFog >= 0) set(noFog, m->noFog);
	if (m->explored >= 0) set(explored, m->explored);
	if (m->bonus >= 0) set(noBonuses, 0);
	if (changed) LogD("room", "mode \"%s\": the room settings it fixes are set again", m->key.c_str());
}

void GameModeApplySettings(char *settings)
{
	char *gs = settings ? *reinterpret_cast<char **>(settings) : nullptr;
	const GameMode *m = GameModeOfSettings(gs);
	if (!m) return;
	for (int i = 0; i < 8; i++) {
		char *entry = gs + kPlayerEntries + i * kPlayerEntrySize;
		if (*reinterpret_cast<int *>(entry + kEntrySlot) != i) continue;
		const int type = *reinterpret_cast<int *>(entry + kEntryType);
		if (type != 0 && type != 1) continue;
		if (m->bonus >= 0) *reinterpret_cast<int *>(entry + kEntryBonus) = m->bonus;
		int &race = *reinterpret_cast<int *>(entry);
		if (GameModeAllows(*m, race)) continue;
		const int want = FirstAllowedRace(*m, type == 1);
		if (want < 0) continue;
		LogI("room", "mode \"%s\": player %d cannot play civilization %d; plays %d", m->key.c_str(), i, race, want);
		race = want;
		*reinterpret_cast<signed char *>(gs + kSettingsRaces + i) = static_cast<signed char>(want);
	}
}
