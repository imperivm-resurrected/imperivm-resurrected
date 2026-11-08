// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <mutex>
#include <map>
#include <algorithm>
#include <memory>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/json.h"
#include "core/log.h"
#include "mods/mods.h"
#include "game/specials.h"
#include "i18n/tr.h"
#include "mods/features.h"
#include "addr/engine.h"
#include "addr/script.h"
#include "addr/world.h"

namespace {
const int kClassMaskLow = 0x2bc, kClassMaskHigh = 0x2c0;
const int kUnitMaskLow = 0x198, kUnitMaskHigh = 0x19c;

typedef const char *(__fastcall *RegisterIntConstant_t)(void *engine, void *edx, const char *name, int value);

const int kGameSpecialCount = 36;

std::mutex g_lock;
std::vector<const ModSpecial *> g_specials;
std::map<std::string, std::vector<std::string>> g_errors;

std::string GameDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *slash = 0;
	return path;
}

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

void AddError(const std::string &folder, const std::string &message)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		std::vector<std::string> &list = g_errors[folder];
		if (std::find(list.begin(), list.end(), message) == list.end()) list.push_back(message);
	}
	LogW("specials", "(%s) %s", folder.empty() ? "imperivm-resurrected\\specials" : folder.c_str(), message.c_str());
}

bool ValidId(const std::string &id)
{
	if (id.empty() || id.size() > 24 || !isalpha(static_cast<unsigned char>(id[0]))) return false;
	for (char c : id) {
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
	}
	return true;
}

void ReadTexts(const Json &j, const char *key, std::vector<std::pair<std::string, std::string>> &out)
{
	const Json *n = j.Get(key);
	if (!n) return;
	if (n->type == Json::String) {
		out.push_back({"", n->str});
		return;
	}
	for (const auto &kv : n->obj) {
		if (kv.second.type == Json::String) out.push_back({Lower(kv.first), kv.second.str});
	}
}

std::string Pick(const std::vector<std::pair<std::string, std::string>> &texts)
{
	const std::string lang = ModsLanguage();
	for (const char *want : {lang.c_str(), "english", ""}) {
		for (const auto &n : texts) {
			if (n.first == want) return n.second;
		}
	}
	return texts.empty() ? "" : texts[0].second;
}

void LoadFile(const std::string &folder, const std::string &path, const std::string &fileName, std::vector<const ModSpecial *> &out)
{
	Json j;
	std::string error;
	if (!ReadJsonFile(path, j, &error)) {
		AddError(folder, TrFmt("%s: not valid JSON (%s)", {fileName, error}));
		return;
	}
	std::unique_ptr<ModSpecial> s(new ModSpecial());
	s->modFolder = folder;
	s->id = j.GetString("id");
	if (!ValidId(s->id)) {
		AddError(folder, TrFmt("%s: the id \"%s\" is not valid (letters, numbers and _; starts with a letter; up to 24)", {fileName, s->id}));
		return;
	}
	for (int i = 0; i < kGameSpecialCount; i++) {
		const char *g = reinterpret_cast<const char *const *>(addr::kGameSpecialNames)[i];
		if (g && _stricmp(g, s->id.c_str()) == 0) {
			AddError(folder, TrFmt("%s: \"%s\" is a game special", {fileName, s->id}));
			return;
		}
	}
	s->bit = static_cast<int>(j.GetNumber("bit", 0));
	if (s->bit < 36 || s->bit > 63) {
		AddError(folder, TrFmt("%s: \"bit\" is missing or not between 36 and 63 (bits 0 to 35 belong to the game's specials)", {fileName}));
		return;
	}
	ReadTexts(j, "name", s->names);
	ReadTexts(j, "description", s->descriptions);
	s->icon = j.GetString("icon");
	out.push_back(s.release());
}

const ModSpecial *FindByName(const std::string &name)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModSpecial *s : g_specials) {
		if (_stricmp(s->id.c_str(), name.c_str()) == 0) return s;
	}
	return nullptr;
}

unsigned *MaskOf(void *obj, int low, int high, int bit, unsigned *mask)
{
	char *p = static_cast<char *>(obj);
	*mask = 1u << (bit & 31);
	return reinterpret_cast<unsigned *>(p + (bit < 32 ? low : high));
}

}

void SpecialsRebuild()
{
	std::vector<const ModSpecial *> found;
	std::vector<std::pair<std::string, std::string>> roots;
	for (const std::string &dir : ModsMountedDirs()) roots.push_back({ModsPackFolder(dir), dir + "\\specials"});
	roots.push_back({"", GameDir() + "\\imperivm-resurrected\\specials"});
	for (const auto &root : roots) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((root.second + "\\*.json").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) LoadFile(root.first, root.second + "\\" + fd.cFileName, fd.cFileName, found);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::vector<const ModSpecial *> unique;
	for (const ModSpecial *s : found) {
		bool dup = false;
		for (const ModSpecial *u : unique) {
			if (_stricmp(u->id.c_str(), s->id.c_str()) == 0 || u->bit == s->bit) {
				AddError(s->modFolder, TrFmt("\"%s\" (bit %s) clashes with \"%s\" from content pack %s: ignored", {s->id, std::to_string(s->bit), u->id, u->modFolder}));
				dup = true;
				break;
			}
		}
		if (!dup) unique.push_back(s);
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_specials = unique;
	}
	for (const ModSpecial *s : unique) {
		LogI("specials", "registered %s (bit %d, mod %s)", s->id.c_str(), s->bit, s->modFolder.empty() ? "imperivm-resurrected\\specials" : s->modFolder.c_str());
	}
}

std::vector<ModSpecial> SpecialsList()
{
	std::vector<ModSpecial> out;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModSpecial *s : g_specials) out.push_back(*s);
	return out;
}

const ModSpecial *SpecialByBit(int bit)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const ModSpecial *s : g_specials) {
		if (s->bit == bit) return s;
	}
	return nullptr;
}

std::string SpecialDisplayName(const ModSpecial &s)
{
	const std::string t = Pick(s.names);
	return t.empty() ? s.id : t;
}

std::string SpecialDescription(const ModSpecial &s)
{
	return Pick(s.descriptions);
}

std::vector<std::string> SpecialsErrorsFor(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_errors.find(folder);
	return it == g_errors.end() ? std::vector<std::string>() : it->second;
}

void SpecialsRegisterConstants(void *engine)
{
	for (const ModSpecial &s : SpecialsList()) {
		const char *err = reinterpret_cast<RegisterIntConstant_t>(addr::kRegisterIntConstant)(engine, nullptr, s.id.c_str(), s.bit);
		if (err) LogE("specials", "could not register the constant %s for the scripts (%s)", s.id.c_str(), err);
		else LogD("specials", "script constant %s = %d", s.id.c_str(), s.bit);
	}
}

void SpecialsOnClassElement(void *cls, const unsigned *element)
{
	if (!cls || !element || !element[0] || _stricmp(reinterpret_cast<const char *>(element[0]), "properties") != 0) return;
	const unsigned count = element[8] < 64 ? element[8] : 64;
	for (unsigned i = 0; i < count; i++) {
		if (_stricmp(reinterpret_cast<const char *>(element[9 + i * 2]), "unit_specials") != 0) continue;
		const std::string list = reinterpret_cast<const char *>(element[10 + i * 2]);
		unsigned custom = 0;
		std::vector<std::string> own;
		size_t start = 0;
		while (start <= list.size()) {
			size_t end = list.find(',', start);
			if (end == std::string::npos) end = list.size();
			std::string name = list.substr(start, end - start);
			name.erase(0, name.find_first_not_of(" \t"));
			name.erase(name.find_last_not_of(" \t") + 1);
			if (const ModSpecial *s = FindByName(name)) {
				custom |= 1u << (s->bit - 32);
				own.push_back(s->id);
			}
			start = end + 1;
		}
		unsigned *high = reinterpret_cast<unsigned *>(static_cast<char *>(cls) + kClassMaskHigh);
		const unsigned before = *high;
		*high |= custom;
		if (custom) {
			static int logged = 0;
			if (logged++ < 30) {
				char *c = static_cast<char *>(cls);
				const char *className = *reinterpret_cast<unsigned *>(c + 0x1c) < 0x10 ? c + 8 : *reinterpret_cast<const char **>(c + 8);
				std::string names;
				for (const std::string &n : own) names += (names.empty() ? "" : ", ") + n;
				LogD("specials", "class %s carries [%s]; high part of the mask %08x -> %08x", className, names.c_str(), before, *high);
			}
		}
	}
}

void *SpecialsUnitFromObj(unsigned handle, int offset)
{
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle & 0xffff];
	if (!base) return nullptr;
	return base + offset;
}

bool SpecialsHas(void *unit, int bit)
{
	if (!unit || bit < 0 || bit > 63) return false;
	unsigned mask;
	const unsigned word = *MaskOf(unit, kUnitMaskLow, kUnitMaskHigh, bit, &mask);
	return (word & mask) != 0;
}

void SpecialsSet(void *unit, int bit, bool on)
{
	if (!unit || bit < 0 || bit > 63) return;
	unsigned mask;
	unsigned *word = MaskOf(unit, kUnitMaskLow, kUnitMaskHigh, bit, &mask);
	if (on) *word |= mask;
	else *word &= ~mask;
}

namespace {
typedef void *(__cdecl *BuildStats_t)(void *out, void *cls);
typedef void *(__fastcall *Append_t)(void *self, void *edx, const char *text, unsigned n);
BuildStats_t g_origBuildStats = nullptr;

void *__cdecl HookBuildStats(void *out, void *cls)
{
	void *r = g_origBuildStats(out, cls);
	if (!out || !cls) return r;
	const unsigned high = *reinterpret_cast<unsigned *>(static_cast<char *>(cls) + kClassMaskHigh);
	if (!(high & ~0xFu)) return r;
	for (const ModSpecial &s : SpecialsList()) {
		if (!((high >> (s.bit - 32)) & 1)) continue;
		const std::string line = "\n" + SpecialDisplayName(s);
		reinterpret_cast<Append_t>(addr::kStringAppend)(out, nullptr, line.c_str(), static_cast<unsigned>(line.size()));
	}
	return r;
}

}

void InstallSpecials(bool knownExe)
{
	if (!knownExe) return;
	if (HookCreate(reinterpret_cast<void *>(addr::kBuildStatsText), reinterpret_cast<void *>(&HookBuildStats), reinterpret_cast<void **>(&g_origBuildStats)) != MH_OK ||
		HookEnable(reinterpret_cast<void *>(addr::kBuildStatsText)) != MH_OK) {
		LogE("specials", "could not hook BuildObjTypeStatsText (they will not appear in the class sheet)");
		return;
	}
	LogI("specials", "hooked");
}

namespace {

std::vector<std::string> SpecialtyProblemsFor(const std::string &folder)
{
	std::vector<std::string> out;
	for (const std::string &e : SpecialsErrorsFor(folder)) out.push_back(Tr("Specialty ") + e);
	return out;
}

const bool g_featureRegistered = (ModsRegisterFeature({30, &SpecialsRebuild, &SpecialtyProblemsFor}), true);

}
