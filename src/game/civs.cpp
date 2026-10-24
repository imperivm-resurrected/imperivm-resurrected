// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "game/civs.h"
#include "game/civroom.h"
#include "mods/civsets.h"
#include "game/gamemodes.h"
#include "game/specials.h"
#include "core/json.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/civindices.h"
#include "i18n/tr.h"
#include "mods/features.h"
#include "hooks/diag.h"

#include <algorithm>
#include <mutex>
#include <memory>
#include <cstdint>
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/script.h"
#include "addr/ui.h"
#include "addr/world.h"

namespace {
const int kClassRaceOffset = 0xb48;

typedef const char *(__cdecl *GetRaceName_t)(int race);
typedef const char *(__cdecl *GetRaceLetter_t)(int race, int lower);
typedef void *(__cdecl *GetRaceFolder_t)(void *out, int race);
typedef const char *(__fastcall *ClassStartElement_t)(void *self, void *edx, unsigned p1, unsigned *element);
typedef void(__fastcall *StringAssign_t)(void *str, void *edx, const char *text, size_t n);
typedef int(__fastcall *GetSlotInfo_t)(void *dlg, void *edx, int slot, const char **name, int *type, int *color, int *ready, int *race,
	int *team, int *extra);
typedef int(__fastcall *TemplateForRace_t)(void *lib, void *edx, int race, int extra);
typedef void *(__cdecl *MakeRandomName_t)(void *out, int race);
typedef int(__cdecl *CatapultByRace_t)(int race);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *ListInsert_t)(void *list, void *edx, const char *text, void *image, int d1, int d2, unsigned pos, int redraw);
typedef const char *(__fastcall *RegisterIntConstant_t)(void *engine, void *edx, const char *name, int value);

GetRaceName_t g_origName = nullptr;
GetRaceLetter_t g_origLetter = nullptr;
GetRaceFolder_t g_origFolder = nullptr;
ClassStartElement_t g_origClassStart = nullptr;
GetSlotInfo_t g_origSlotInfo = nullptr;
TemplateForRace_t g_origTown = nullptr, g_origVillage = nullptr;
MakeRandomName_t g_origRandomName = nullptr;
CatapultByRace_t g_origCatapult = nullptr;
typedef void *(__cdecl *GenerateRandomMap_t)(unsigned params, int *out);
GenerateRandomMap_t g_origGenerateMap = nullptr;

const char *const kGameNames[8] = {"Gaul", "RepublicanRome", "Carthage", "Iberia", "ImperialRome", "Britain", "Egypt", "Germany"};

std::mutex g_lock;
std::vector<const Civ *> g_civs;
std::map<std::string, std::vector<std::string>> g_errors;
bool g_ready = false;

struct BaseOverride {
	std::string letterUp, letterLow;
	char prefix[48] = {};
	char neutralPrefix[48] = {};
};
BaseOverride g_baseOverride[8];
uint32_t g_neutralPrefixOriginal[8] = {};
bool g_neutralPrefixRead = false;
bool g_hooked = false;

struct GameString {
	int allocator;
	const char *text;
	char unused[12];
	unsigned size;
	unsigned capacity;
};

std::string Lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

bool ValidId(const std::string &s)
{
	if (s.empty() || s.size() > 24 || s[0] < 'a' || s[0] > 'z') return false;
	for (char c : s) {
		if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_') return false;
	}
	return true;
}

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
	LogW("civilizations", "(%s) %s", folder.empty() ? "imperivm-resurrected\\civs" : folder.c_str(), message.c_str());
}

const Civ *FindByIndex(int index)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const Civ *c : g_civs) {
		if (c->index == index) return c;
	}
	return nullptr;
}

const Civ *FindByName(const std::string &name)
{
	const std::string n = Lower(name);
	std::lock_guard<std::mutex> lock(g_lock);
	for (const Civ *c : g_civs) {
		if (c->id == n || (Lower(c->internalName) == n && n != Lower(kGameNames[c->base]))) return c;
	}
	return nullptr;
}

const char *__cdecl HookGetRaceName(int race)
{
	if (race >= 10) {
		if (const Civ *c = FindByIndex(race)) return c->internalName.c_str();
	}
	return g_origName(race);
}

const char *__cdecl HookGetRaceLetter(int race, int lower)
{
	if (race >= 10) {
		if (const Civ *c = FindByIndex(race)) return lower ? c->letterLow.c_str() : c->letterUp.c_str();
	}
	if (race >= 0 && race < 8 && !g_baseOverride[race].letterUp.empty()) return lower ? g_baseOverride[race].letterLow.c_str() : g_baseOverride[race].letterUp.c_str();
	return g_origLetter(race, lower);
}

void *__cdecl HookGetRaceFolder(void *out, int race)
{
	if (race >= 10) {
		if (const Civ *c = FindByIndex(race)) {
			void *r = g_origFolder(out, c->base);
			if (!c->folder.empty()) reinterpret_cast<StringAssign_t>(addr::kStringAssign)(out, nullptr, c->folder.c_str(), c->folder.size());
			return r;
		}
	}
	return g_origFolder(out, race);
}

typedef int(__cdecl *Native_t)(int *pSP);
Native_t g_origGetStrPref = nullptr, g_origGetStrPrefLow = nullptr, g_origObjStrPref = nullptr, g_origObjStrPrefLow = nullptr;

void PushScriptString(int *pSP, const char *text)
{
	const int block = reinterpret_cast<int(__cdecl *)(const char *)>(addr::kAllocScriptString)(text);
	*reinterpret_cast<int *>(*pSP) = block;
	char *p = reinterpret_cast<char *>(*pSP + 4);
	*pSP = reinterpret_cast<int>(p);
	char *interp = *reinterpret_cast<char **>(addr::kCurInterpreter);
	const int last = *reinterpret_cast<int *>(interp + 0x1c);
	*reinterpret_cast<short *>(p) = last == 0 ? 0 : static_cast<short>(reinterpret_cast<int>(p) - last);
	*reinterpret_cast<short *>(p + 2) = 0xb;
	*reinterpret_cast<int *>(interp + 0x1c) = *pSP;
	*pSP += 4;
}

int RaceFromRaceArg(int *pSP) { return *reinterpret_cast<int *>(*pSP - 4); }
int RaceFromObjArg(int *pSP)
{
	const unsigned handle = *reinterpret_cast<unsigned *>(*pSP - 8) & 0xffff;
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle];
	if (!base) return -1;
	char *obj = base + *reinterpret_cast<int *>(*pSP - 4);
	if (obj == nullptr || *reinterpret_cast<char **>(obj + 0x3c) == nullptr) return -1;
	return *reinterpret_cast<int *>(*reinterpret_cast<char **>(obj + 0x3c) + kClassRaceOffset);
}

template <bool obj, bool lower>
int __cdecl HookRaceLetterNative(int *pSP)
{
	const int race = obj ? RaceFromObjArg(pSP) : RaceFromRaceArg(pSP);
	if (race >= 10) {
		if (const Civ *c = FindByIndex(race)) {
			*pSP -= obj ? 8 : 4;
			static int logged = 0;
			if (logged++ < 5) LogD("civilizations", "%s of civilization %d -> '%s'", obj ? "Obj::raceStrPref" : "GetRaceStrPref", race, lower ? c->letterLow.c_str() : c->letterUp.c_str());
			PushScriptString(pSP, lower ? c->letterLow.c_str() : c->letterUp.c_str());
			return 0;
		}
	}
	if (race >= 0 && race < 8 && !g_baseOverride[race].letterUp.empty()) {
		*pSP -= obj ? 8 : 4;
		PushScriptString(pSP, lower ? g_baseOverride[race].letterLow.c_str() : g_baseOverride[race].letterUp.c_str());
		return 0;
	}
	Native_t orig = obj ? (lower ? g_origObjStrPrefLow : g_origObjStrPref) : (lower ? g_origGetStrPrefLow : g_origGetStrPref);
	return orig(pSP);
}

Native_t g_origPlayerRace = nullptr;

int __cdecl HookGetPlayerRace(int *pSP)
{
	const int r = g_origPlayerRace(pSP);
	int *result = reinterpret_cast<int *>(*pSP - 4);
	if (*result >= 10) {
		const Civ *c = FindByIndex(*result);
		if (c && c->variant) {
			static int logged = 0;
			if (logged++ < 5) LogD("civilizations", "GetPlayerRace of %s -> the base (%d): it is a variant", c->id.c_str(), c->base);
			*result = c->base;
		}
	}
	return r;
}

std::vector<const Civ *> SortedCivs();

bool GameInRoom()
{
	return CivSetsAllow(CivRoomSets(), kCivSetGame);
}

// A civilization that comes from no content pack (the civs folder of the mod itself) is not in any set: it is always allowed.
bool CivInRoom(const Civ &c)
{
	return c.modId.empty() || CivSetsAllow(CivRoomSets(), c.modId);
}

std::vector<RandomCiv> RandomCivs(const std::vector<const Civ *> &civs, bool filter)
{
	std::vector<RandomCiv> out;
	for (const Civ *c : civs) out.push_back({c->index, c->base, c->replacesBase, c->random, c->ai, !filter || CivInRoom(*c)});
	return out;
}

const unsigned char kRandomDrawOriginal[21] = {0x8b, 0x93, 0xa0, 0x12, 0x00, 0x00, 0x8d, 0x8b, 0xa0, 0x12, 0x00, 0x00, 0x6a,
	0x07, 0x6a, 0x00, 0xff, 0x52, 0x14, 0x8b, 0xc8};

int __cdecl PickRandomRace(char *game, char *frame)
{
	const char *gs = *reinterpret_cast<char **>(frame + 8);
	const int slot = *reinterpret_cast<int *>(frame - 0x18);
	const bool isAi = gs && slot >= 0 && slot < 8 && *reinterpret_cast<const int *>(gs + 0xa4 + slot * 0xc0 + 8) == 1;
	const std::vector<const Civ *> civs = SortedCivs();
	void *rng = game + 0x12a0;
	void **vt = *reinterpret_cast<void ***>(rng);
	auto rand = reinterpret_cast<int(__fastcall *)(void *, void *, int, int)>(vt[0x14 / 4]);
	if (const GameMode *mode = GameModeOfSettings(gs)) {
		const std::vector<int> allowed = GameModeRandomPool(*mode, isAi);
		if (!allowed.empty()) {
			const int race = allowed[rand(rng, nullptr, 0, static_cast<int>(allowed.size()) - 1)];
			LogD("civilizations", "random civilization of player %d%s in the mode \"%s\" -> %d", slot, isAi ? " (AI)" : "", mode->key.c_str(), race);
			return race;
		}
	}
	std::vector<std::vector<int>> groups = CivRandomGroups(RandomCivs(civs, true), isAi, GameInRoom());
	if (groups.empty()) groups = CivRandomGroups(RandomCivs(civs, false), isAi, true);
	const std::vector<int> &group = groups[rand(rng, nullptr, 0, static_cast<int>(groups.size()) - 1)];
	const Civ *first = group[0] >= 10 ? FindByIndex(group[0]) : nullptr;
	const int race = first && first->replacesBase ? group[rand(rng, nullptr, 0, static_cast<int>(group.size()) - 1)] : group[0];
	static int logged = 0;
	if (logged++ < 20) LogD("civilizations", "random civilization of player %d%s -> %d", slot, isAi ? " (AI)" : "", race);
	return race;
}

bool PatchRandomDraw()
{
	auto site = reinterpret_cast<unsigned char *>(addr::kRandomDrawSite);
	if (memcmp(site, kRandomDrawOriginal, sizeof(kRandomDrawOriginal)) != 0) return false;
	unsigned char patch[21];
	memset(patch, 0x90, sizeof(patch));
	size_t n = 0;
	patch[n++] = 0x55;
	patch[n++] = 0x53;
	patch[n++] = 0xe8;
	const int rel = static_cast<int>(reinterpret_cast<intptr_t>(&PickRandomRace) - (reinterpret_cast<intptr_t>(site) + n + 4));
	memcpy(patch + n, &rel, 4);
	n += 4;
	patch[n++] = 0x83;
	patch[n++] = 0xc4;
	patch[n++] = 0x08;
	patch[n++] = 0x8b;
	patch[n++] = 0xc8;
	DWORD old = 0;
	if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(site, patch, sizeof(patch));
	VirtualProtect(site, sizeof(patch), old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
	return true;
}

int g_classLogged = 0;

const char *__fastcall HookClassStartElement(void *self, void *edx, unsigned p1, unsigned *element)
{
	int forced = -1;
	unsigned slot = 0;
	unsigned original = 0;
	if (g_ready && element && element[0] && _stricmp(reinterpret_cast<const char *>(element[0]), "properties") == 0) {
		const unsigned count = element[8] < 64 ? element[8] : 64;
		for (unsigned i = 0; i < count; i++) {
			if (_stricmp(reinterpret_cast<const char *>(element[9 + i * 2]), "race") != 0) continue;
			if (const Civ *c = FindByName(reinterpret_cast<const char *>(element[10 + i * 2]))) {
				forced = c->index;
				slot = 10 + i * 2;
				original = element[slot];
				element[slot] = reinterpret_cast<unsigned>("None");
				if (g_classLogged++ < 10) LogD("civilizations", "a class uses race=\"%s\" -> %d", c->id.c_str(), c->index);
			}
		}
	}
	const char *r = g_origClassStart(self, edx, p1, element);
	SpecialsOnClassElement(self, element);
	if (forced >= 0) {
		element[slot] = original;
		*reinterpret_cast<int *>(static_cast<char *>(self) + kClassRaceOffset) = forced;
	}
	return r;
}

struct SlotCiv {
	int slot, civ, base, type;
	bool ai;
};
bool g_inBuild = false;
DWORD g_buildThread = 0;
std::vector<SlotCiv> g_slotCivs;

std::vector<const Civ *> SortedCivs()
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::vector<const Civ *> v = g_civs;
	std::sort(v.begin(), v.end(), [](const Civ *a, const Civ *b) { return a->index < b->index; });
	return v;
}

int BaseOf(int race)
{
	if (race >= 10) {
		if (const Civ *c = FindByIndex(race)) return c->base;
	}
	return race;
}

void AddCivsToRoomCombos(void *dlg)
{
	const std::vector<const Civ *> civs = SortedCivs();
	if (civs.empty()) return;
	static std::vector<char *> texts;
	if (texts.size() != civs.size() + 1) {
		for (char *t : texts) free(t);
		texts.clear();
		texts.push_back(_strdup(CivsRoomSeparator()));
		for (const Civ *c : civs) texts.push_back(_strdup(CivDisplayName(*c).c_str()));
	}
	void **vt = *reinterpret_cast<void ***>(dlg);
	auto getChild = reinterpret_cast<GetChild_t>(vt[0x7c / 4]);
	int added = 0;
	for (int n = 0; n < 8; n++) {
		void *combo = getChild(dlg, nullptr, 0x1022 + n * 0x1000, *reinterpret_cast<const char **>(addr::kClassNameCUIComboBox));
		void *list = combo ? *reinterpret_cast<void **>(static_cast<char *>(combo) + 0x64) : nullptr;
		if (!list) continue;
		void **lvt = *reinterpret_cast<void ***>(list);
		for (char *t : texts) {
			reinterpret_cast<ListInsert_t>(lvt[0x98 / 4])(list, nullptr, t, nullptr, 0, 0, 0xffffffffu, 1);
			added++;
		}
	}
	LogI("civilizations", "%d mod civilizations added to the room dropdowns (%d items)", static_cast<int>(civs.size()), added);
}

int __fastcall HookSlotInfo(void *dlg, void *edx, int slot, const char **name, int *type, int *color, int *ready, int *race, int *team,
	int *extra)
{
	const int r = g_origSlotInfo(dlg, edx, slot, name, type, color, ready, race, team, extra);
	if (r && g_inBuild && g_buildThread == GetCurrentThreadId() && race && type && (*type == 1 || *type == 3) && *race >= 10) {
		const std::vector<const Civ *> civs = SortedCivs();
		const size_t pos = static_cast<size_t>(*race - 10);
		if (pos < civs.size()) {
			g_slotCivs.push_back({slot, civs[pos]->index, civs[pos]->base, *type, civs[pos]->ai});
			*race = civs[pos]->index;
		}
	}
	return r;
}

int g_templateLogged = 0;

typedef void *(__fastcall *PlaceTown_t)(void *self, void *edx, void *grid, float scale, unsigned tpl, void *x, int y, int player, void *outObj,
	const char *className, unsigned *outSettlement);
typedef void *(__fastcall *FindClass_t)(void *registry, void *edx, const char *name);
PlaceTown_t g_origPlaceTown = nullptr;
FindClass_t g_origFindClass = nullptr;
thread_local const Civ *t_cityScope = nullptr;
signed char g_genRaces[8] = {};
bool g_genActive = false;

std::string TemplateName(void *lib, unsigned idx)
{
	if (!lib || idx > 200) return "";
	const char *begin = *reinterpret_cast<const char **>(static_cast<char *>(lib) + 0x10);
	const char *end = *reinterpret_cast<const char **>(static_cast<char *>(lib) + 0x14);
	if (!begin || begin + (static_cast<size_t>(idx) + 1) * 0xb0 > end) return "";
	const char *entry = begin + static_cast<size_t>(idx) * 0xb0;
	const unsigned capacity = *reinterpret_cast<const unsigned *>(entry + 0x18);
	const char *text = capacity < 0x10 ? entry + 4 : *reinterpret_cast<const char *const *>(entry + 4);
	return text ? std::string(text, strnlen(text, 64)) : "";
}

int FindTemplateByName(void *lib, const std::string &name)
{
	if (!lib) return -1;
	for (unsigned i = 0; i < 200; i++) {
		const char *begin = *reinterpret_cast<const char **>(static_cast<char *>(lib) + 0x10);
		const char *end = *reinterpret_cast<const char **>(static_cast<char *>(lib) + 0x14);
		if (!begin || begin + (static_cast<size_t>(i) + 1) * 0xb0 > end) break;
		if (TemplateName(lib, i) == name) return static_cast<int>(i);
	}
	return -1;
}

const char *const kTemplatePrefix[8] = {"Gaul", "Republican Roman", "Carthaginian", "Iberian", "Imperial Roman", "Briton", "Egyptian", "German"};

bool ScopedName(const char *name, std::string &out)
{
	const Civ *c = t_cityScope;
	if (!c || !name || !name[0] || c->base < 0 || c->base > 7) return false;
	const char baseLetter = static_cast<char>(toupper(static_cast<unsigned char>(kGameCivPrefixes[c->base])));
	if (name[0] != baseLetter || !(isupper(static_cast<unsigned char>(name[1])) || strncmp(name + 1, "house", 5) == 0)) return false;
	out = c->letterUp + (name + 1);
	return true;
}

void *__fastcall HookFindClass(void *registry, void *edx, const char *name)
{
	std::string mapped;
	if (t_cityScope && ScopedName(name, mapped)) {
		const Civ *keep = t_cityScope;
		t_cityScope = nullptr;
		void *r = g_origFindClass(registry, edx, mapped.c_str());
		t_cityScope = keep;
		if (r) return r;
	}
	return g_origFindClass(registry, edx, name);
}

void *__fastcall HookPlaceTown(void *self, void *edx, void *grid, float scale, unsigned tpl, void *x, int y, int player, void *outObj,
	const char *className, unsigned *outSettlement)
{
	const Civ *civ = nullptr;
	int race = -1;
	if (player >= 1 && player <= 8) {
		if (g_genActive) race = g_genRaces[player - 1];
		else if (const char *gs = *reinterpret_cast<char **>(addr::kGameSettings)) race = *reinterpret_cast<const signed char *>(gs + 0x5c + (player - 1));
		if (race >= 10) civ = FindByIndex(race);
	}
	static int logged = 0;
	if (logged++ < 10) LogD("civilizations", "starting city of player %d (civilization %d%s)%s", player, race, g_genActive ? ", from the generator" : ", from the settings", civ ? ": its classes are translated" : "");
	if (civ && g_genActive && civ->biome >= 0 && civ->biome != civ->base && g_origTown && g_origVillage) {
		const std::string name = TemplateName(self, tpl);
		const std::string prefix = kTemplatePrefix[civ->biome];
		unsigned wanted = tpl;
		const char *kind = "none";
		if (name.compare(0, prefix.size() + 5, prefix + " Town") == 0) {
			wanted = static_cast<unsigned>(g_origTown(self, nullptr, civ->base, -1));
			kind = "town";
		}
		else if (name.compare(0, prefix.size() + 8, prefix + " Village") == 0) {
			wanted = static_cast<unsigned>(g_origVillage(self, nullptr, civ->base, -1));
			kind = "village";
		}
		static int logged2 = 0;
		if (logged2++ < 8) LogD("civilizations", "template of player %d: %u \"%s\" -> %u (%s of the base; the biome is another one)", player, tpl, name.c_str(), wanted, kind);
		tpl = wanted;
	}
	if (civ && !civ->templatePrefix.empty()) {
		const std::string asked = TemplateName(self, tpl);
		const char *kinds[] = {" Town", " Village"};
		for (const char *kind : kinds) {
			const size_t at = asked.find(kind);
			if (at == std::string::npos) continue;
			const std::string suffix = asked.substr(at + strlen(kind));
			int own = FindTemplateByName(self, civ->templatePrefix + kind + suffix);
			if (own < 0) own = FindTemplateByName(self, civ->templatePrefix + kind + " 1");
			if (own < 0) own = FindTemplateByName(self, civ->templatePrefix + kind);
			static int logged3 = 0;
			if (logged3++ < 8) {
				LogD("civilizations", "template of player %d: \"%s\" -> %s", player, asked.c_str(),
					own >= 0 ? TemplateName(self, static_cast<unsigned>(own)).c_str() : "(the base's: the civilization has none of that type)");
			}
			if (own >= 0) tpl = static_cast<unsigned>(own);
			break;
		}
	}
	const Civ *before = t_cityScope;
	t_cityScope = civ;
	void *r = g_origPlaceTown(self, edx, grid, scale, tpl, x, y, player, outObj, className, outSettlement);
	t_cityScope = before;
	return r;
}

int FindByOverride(void *lib, int base, const char *kind)
{
	if (base < 0 || base >= 8 || !g_baseOverride[base].prefix[0]) return -1;
	const std::string name = std::string(g_baseOverride[base].prefix) + kind;
	GameString s = {};
	s.text = name.c_str();
	s.size = static_cast<unsigned>(name.size());
	s.capacity = 0x1f;
	const int r = reinterpret_cast<int(__fastcall *)(void *, void *, GameString *, int)>(addr::kFindTemplateByPrefix)(lib, nullptr, &s, -1);
	static int logged = 0;
	if (logged++ < 8) LogD("civilizations", "settlement template of %s -> \"%s\" (%d)", kGameNames[base], name.c_str(), r);
	return r;
}

int __fastcall HookTown(void *lib, void *edx, int race, int extra)
{
	if (g_templateLogged < 8 && race >= 10) {
		g_templateLogged++;
		LogD("civilizations", "city template of civilization %d -> its base's (%d)", race, BaseOf(race));
	}
	const int own = FindByOverride(lib, BaseOf(race), " Town");
	if (own >= 0) return own;
	return g_origTown(lib, edx, BaseOf(race), extra);
}

int __fastcall HookVillage(void *lib, void *edx, int race, int extra)
{
	const int own = FindByOverride(lib, BaseOf(race), " Village");
	if (own >= 0) return own;
	return g_origVillage(lib, edx, BaseOf(race), extra);
}

void ApplyNeutralPrefixes()
{
	if (!g_hooked) return;
	auto first = reinterpret_cast<unsigned char *>(addr::kRandomMapRacePrefixes);
	if (!g_neutralPrefixRead) {
		for (int i = 0; i < 8; i++) {
			const unsigned char *ins = first + i * 8;
			if (ins[0] != 0xc7 || ins[1] != 0x44 || ins[2] != 0x24) {
				LogW("civilizations", "the random map's settlement names are not the expected code: base civilization templates stay as they are");
				return;
			}
			memcpy(&g_neutralPrefixOriginal[i], ins + 4, 4);
		}
		g_neutralPrefixRead = true;
	}
	for (int i = 0; i < 8; i++) {
		const char *text = reinterpret_cast<const char *>(static_cast<uintptr_t>(g_neutralPrefixOriginal[i]));
		uint32_t want = g_neutralPrefixOriginal[i];
		for (int b = 0; b < 8; b++) {
			if (!g_baseOverride[b].neutralPrefix[0]) continue;
			if (strcmp(text, (std::string(kTemplatePrefix[b]) + " ").c_str()) == 0) want = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_baseOverride[b].neutralPrefix));
		}
		unsigned char *imm = first + i * 8 + 4;
		uint32_t now = 0;
		memcpy(&now, imm, 4);
		if (now == want) continue;
		DWORD old = 0;
		if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
		memcpy(imm, &want, 4);
		VirtualProtect(imm, 4, old, &old);
		FlushInstructionCache(GetCurrentProcess(), imm, 4);
		LogD("civilizations", "random map settlements of \"%s\" -> \"%s\"", text, reinterpret_cast<const char *>(static_cast<uintptr_t>(want)));
	}
}

void *__cdecl HookRandomName(void *out, int race)
{
	return g_origRandomName(out, BaseOf(race));
}

int __cdecl HookCatapult(int race)
{
	return g_origCatapult(BaseOf(race));
}

Native_t g_origBestTemple = nullptr;
typedef int(__fastcall *ComputeNeeds_t)(void *self, void *edx, char *player, int a, int b);
ComputeNeeds_t g_origComputeNeeds = nullptr;

int __cdecl HookBestTemple(int *pSP)
{
	int *raceSlot = nullptr;
	const unsigned handle = *reinterpret_cast<unsigned *>(*pSP - 8) & 0xffff;
	char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle];
	if (base) {
		char *set = base + *reinterpret_cast<int *>(*pSP - 4);
		char *central = reinterpret_cast<char **>(addr::kHandleTable)[*reinterpret_cast<unsigned short *>(set + 0x8c)];
		char *cls = central ? *reinterpret_cast<char **>(central + 0x3c) : nullptr;
		if (cls) raceSlot = reinterpret_cast<int *>(cls + kClassRaceOffset);
	}
	if (!raceSlot || *raceSlot < 10) return g_origBestTemple(pSP);
	const int real = *raceSlot;
	static int logged = 0;
	if (logged++ < 5) LogD("civilizations", "BestTemple of civilization %d -> the base (%d)", real, BaseOf(real));
	*raceSlot = BaseOf(real);
	const int r = g_origBestTemple(pSP);
	*raceSlot = real;
	return r;
}

int __fastcall HookComputeNeeds(void *self, void *edx, char *player, int a, int b)
{
	CivRaceAsBase scope(player);
	return g_origComputeNeeds(self, edx, player, a, b);
}

void *__cdecl HookGenerateRandomMap(unsigned params, int *out)
{
	if (params) {
		signed char *races = reinterpret_cast<signed char *>(params + 0x30);
		char before[64], after[64];
		snprintf(before, sizeof(before), "%d %d %d %d %d %d %d %d", races[0], races[1], races[2], races[3], races[4], races[5], races[6], races[7]);
		for (int i = 0; i < 8; i++) {
			g_genRaces[i] = static_cast<unsigned char>(races[i]);
			if (races[i] >= 10) {
				const Civ *c = FindByIndex(races[i]);
				races[i] = static_cast<signed char>(c && c->biome >= 0 ? c->biome : BaseOf(races[i]));
			}
		}
		snprintf(after, sizeof(after), "%d %d %d %d %d %d %d %d", races[0], races[1], races[2], races[3], races[4], races[5], races[6], races[7]);
		LogD("civilizations", "random map, civilization of each player (8 = random): %s -> %s", before, after);
	}
	g_genActive = params != 0;
	void *r = g_origGenerateMap(params, out);
	g_genActive = false;
	return r;
}

void LoadCivFile(const std::string &folder, const std::string &path, const std::string &fileName, std::vector<Civ *> &out)
{
	Json j;
	std::string err;
	if (!ReadJsonFile(path, j, &err) || j.type != Json::Object) {
		AddError(folder, TrFmt("%s: not valid JSON (%s)", {fileName, err.empty() ? Tr("not an object") : err}));
		return;
	}
	auto civ = std::make_unique<Civ>();
	civ->modFolder = folder;
	if (!folder.empty()) {
		ModInfo owner;
		civ->modId = ModsFind(folder, owner) && !owner.id.empty() ? owner.id : folder;
	}
	std::string spelled = j.GetString("id");
	if (spelled.empty()) spelled = fileName.substr(0, fileName.rfind('.'));
	civ->id = Lower(spelled);
	if (!ValidId(civ->id)) {
		AddError(folder, TrFmt("%s: the id \"%s\" is not valid (letters, numbers and _; starts with a letter; up to 24)", {fileName, civ->id}));
		return;
	}
	civ->constant = spelled;
	civ->constant[0] = static_cast<char>(toupper(static_cast<unsigned char>(civ->constant[0])));
	const Json *index = j.Get("index");
	civ->automaticIndex = !index;
	if (index) {
		if (index->type != Json::Number || !(index->num >= kCivIndexFirst && index->num <= kCivIndexLast) || index->num != static_cast<int>(index->num)) {
			AddError(folder, TrFmt("%s: \"index\" must be a whole number between 10 and 127, or omitted for automatic assignment", {fileName}));
			return;
		}
		civ->index = static_cast<int>(index->num);
	}
	const std::string base = j.GetString("base", "Gaul");
	civ->base = GameCivByName(base);
	if (civ->base < 0) {
		AddError(folder, TrFmt("%s: the base \"%s\" is not a game civilization (Gaul, RepublicanRome, Carthage, Iberia, ImperialRome, Britain, Egypt, Germany)", {fileName, base}));
		return;
	}
	const std::string biome = j.GetString("biome");
	if (!biome.empty()) {
		civ->biome = GameCivByName(biome);
		if (civ->biome < 0) {
			AddError(folder, TrFmt("%s: the biome \"%s\" is not a game civilization (Gaul, RepublicanRome, Carthage, Iberia, ImperialRome, Britain, Egypt, Germany)", {fileName, biome}));
			return;
		}
	}
	civ->templatePrefix = j.GetString("template");
	std::string name = j.GetString("internalName");
	civ->internalName = name.empty() ? kGameNames[civ->base] : name;
	const Json *letter = j.Get("letter");
	if ((letter && letter->type != Json::String) || !CivSetPrefix(*civ, letter ? letter->str : std::string(1, kGameCivPrefixes[civ->base]))) {
		AddError(folder, TrFmt("%s: \"letter\" must be 1 to 4 characters (uppercase letter then letters or digits; a single letter or digit also works)", {fileName}));
		return;
	}
	civ->folder = j.GetString("folder");
	civ->random = j.GetBool("random", true);
	civ->variant = j.GetBool("variant");
	civ->replacesBase = j.GetBool("replacesBase");
	civ->ai = j.GetBool("ai", civ->variant);
	civ->ownAi = j.GetBool("ownAi");
	if (const Json *alias = j.Get("alias")) {
		if (alias->type != Json::Object) {
			AddError(folder, TrFmt("%s: \"alias\" is an object with \"classes\" and \"commands\"", {fileName}));
		}
		else {
			const std::pair<const char *, std::map<std::string, std::string> *> tables[] = {{"classes", &civ->aliasClasses}, {"commands", &civ->aliasOrders}};
			for (const auto &t : tables) {
				const Json *list = alias->Get(t.first);
				if (!list) continue;
				for (const auto &kv : list->obj) {
					if (kv.second.type == Json::String && !kv.first.empty() && !kv.second.str.empty()) (*t.second)[kv.first] = kv.second.str;
				}
			}
		}
	}
	if (const Json *n = j.Get("name")) {
		if (n->type == Json::String) civ->names.push_back({"", n->str});
		else {
			for (const auto &kv : n->obj) {
				if (kv.second.type == Json::String) civ->names.push_back({Lower(kv.first), kv.second.str});
			}
		}
	}
	out.push_back(civ.release());
}

}

const char *GameCivName(int index)
{
	return index >= 0 && index < 8 ? kGameNames[index] : "";
}

int CivsPlayerCiv(int player)
{
	const char *gs = *reinterpret_cast<char **>(addr::kGameSettings);
	if (!gs || player < 1 || player > 8) return -1;
	return *reinterpret_cast<const int *>(gs + 0xa4 + (player - 1) * 0xc0);
}

void *CivsLocalPlayer()
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	return game ? *reinterpret_cast<void **>(game + 0x12c8) : nullptr;
}

CivRaceAsBase::CivRaceAsBase(void *player)
{
	int *race = player ? reinterpret_cast<int *>(static_cast<char *>(player) + 0x7c) : nullptr;
	if (!race || *race < 10) return;
	const int base = BaseOf(*race);
	if (base == *race) return;
	slot = race;
	real = *race;
	*race = base;
}

CivRaceAsBase::~CivRaceAsBase()
{
	if (slot) *slot = real;
}

int GameCivByName(const std::string &name)
{
	const std::string n = Lower(name);
	for (int i = 0; i < 8; i++) {
		if (n == Lower(kGameNames[i])) return i;
	}
	if (n == "rome" || n == "roman") return 1;
	if (n == "britain" || n == "briton") return 5;
	return -1;
}

void CivsRebuild()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_errors.clear();
	}
	std::vector<Civ *> found;
	std::vector<std::string> dirs = ModsMountedDirs();
	std::vector<std::pair<std::string, std::string>> roots;
	for (const std::string &dir : dirs) roots.push_back({dir.substr(dir.find_last_of('\\') + 1), dir + "\\civs"});
	roots.push_back({"", GameDir() + "\\imperivm-resurrected\\civs"});
	for (const auto &root : roots) {
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA((root.second + "\\*.json").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) continue;
		do {
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) LoadCivFile(root.first, root.second + "\\" + fd.cFileName, fd.cFileName, found);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	std::vector<const Civ *> unique;
	for (const Civ *c : found) {
		bool dup = false;
		for (const Civ *u : unique) {
			if (u->id == c->id || (c->index != 0 && u->index == c->index)) {
				AddError(c->modFolder, TrFmt("\"%s\" (index %s) clashes with \"%s\" from content pack %s: ignored", {c->id, c->automaticIndex ? Tr("Automatic") : std::to_string(c->index), u->id, u->modFolder}));
				dup = true;
				break;
			}
		}
		if (dup) continue;
		const int owner = CivPrefixClash(*c, unique);
		if (owner >= 0) {
			std::string name = GameCivName(owner);
			std::string pack = Tr("Original game");
			for (const Civ *u : unique) {
				if (!u->ownLetter || u->letterLow != c->letterLow) continue;
				name = CivDisplayName(*u);
				pack = u->modFolder.empty() ? "imperivm-resurrected\\civs" : u->modFolder;
				break;
			}
			AddError(c->modFolder, TrFmt("\"%s\": prefix \"%s\" is already used by %s from content pack %s: ignored", {c->id, c->letterUp, name, pack}));
			continue;
		}
		unique.push_back(c);
	}
	std::vector<CivIndex> indices;
	for (const Civ *c : unique) indices.push_back({c->id, c->index});
	for (const std::string &id : CivIndicesAssign(indices)) {
		for (const Civ *c : unique) {
			if (c->id == id) AddError(c->modFolder, TrFmt("\"%s\": no free civilization index (up to 118 custom civilizations per profile): ignored", {id}));
		}
	}
	for (Civ *c : found) {
		if (c->index != 0) continue;
		const auto slot = std::find_if(indices.begin(), indices.end(), [&](const CivIndex &entry) { return entry.id == c->id; });
		if (slot != indices.end()) c->index = slot->index;
	}
	unique.erase(std::remove_if(unique.begin(), unique.end(), [](const Civ *c) { return c->index == 0; }), unique.end());
	indices.erase(std::remove_if(indices.begin(), indices.end(), [](const CivIndex &c) { return c.index == 0; }), indices.end());
	const bool automatic = std::any_of(unique.begin(), unique.end(), [](const Civ *c) { return c->automaticIndex; });
	CivIndicesSet(indices, automatic);
	BaseOverride overrides[8];
	const Civ *letterFrom[8] = {}, *prefixFrom[8] = {};
	for (const Civ *c : unique) {
		if (!c->replacesBase) continue;
		BaseOverride &o = overrides[c->base];
		if (c->ownLetter) {
			if (letterFrom[c->base] && letterFrom[c->base]->letterUp != c->letterUp) {
				AddError(c->modFolder, TrFmt("\"%s\" gives %s the letter %s, but \"%s\" from content pack %s gives it %s: the letter of %s is not changed",
					{c->id, kGameNames[c->base], c->letterUp, letterFrom[c->base]->id, letterFrom[c->base]->modFolder, letterFrom[c->base]->letterUp, c->id}));
			}
			else if (!letterFrom[c->base]) {
				letterFrom[c->base] = c;
				o.letterUp = c->letterUp;
				o.letterLow = c->letterLow;
			}
		}
		if (!c->templatePrefix.empty()) {
			if (c->templatePrefix.size() >= sizeof(o.prefix) - 1) {
				AddError(c->modFolder, TrFmt("%s: \"template\" is too long (up to %s characters)", {c->id, std::to_string(sizeof(o.prefix) - 2)}));
			}
			else if (prefixFrom[c->base] && prefixFrom[c->base]->templatePrefix != c->templatePrefix) {
				AddError(c->modFolder, TrFmt("\"%s\" gives %s the templates \"%s\", but \"%s\" from content pack %s gives it \"%s\": the templates of %s are not used for the base",
					{c->id, kGameNames[c->base], c->templatePrefix, prefixFrom[c->base]->id, prefixFrom[c->base]->modFolder, prefixFrom[c->base]->templatePrefix, c->id}));
			}
			else if (!prefixFrom[c->base]) {
				prefixFrom[c->base] = c;
				snprintf(o.prefix, sizeof(o.prefix), "%s", c->templatePrefix.c_str());
				snprintf(o.neutralPrefix, sizeof(o.neutralPrefix), "%s ", c->templatePrefix.c_str());
			}
		}
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_civs = unique;
		g_ready = true;
		for (int b = 0; b < 8; b++) g_baseOverride[b] = overrides[b];
	}
	for (int b = 0; b < 8; b++) {
		if (letterFrom[b]) LogI("civilizations", "%s: the letter of %s is now %s (it replaces the base)", letterFrom[b]->id.c_str(), kGameNames[b], letterFrom[b]->letterUp.c_str());
		if (prefixFrom[b]) LogI("civilizations", "%s: the settlements of %s use the templates \"%s\"", prefixFrom[b]->id.c_str(), kGameNames[b], prefixFrom[b]->templatePrefix.c_str());
	}
	ApplyNeutralPrefixes();
	for (const Civ *c : unique) {
		LogI("civilizations", "%s (index %d, base %s, letter %s, mod %s)", c->id.c_str(), c->index, kGameNames[c->base], c->letterUp.c_str(),
			c->modFolder.empty() ? "imperivm-resurrected\\civs" : c->modFolder.c_str());
		if (c->variant) LogI("civilizations", "%s: variant of %s (the scripts see the base as the player's civilization)", c->id.c_str(), kGameNames[c->base]);
		if (c->replacesBase) LogI("civilizations", "%s: replaces %s in the room and in the random draw", c->id.c_str(), kGameNames[c->base]);
		if (c->ai) {
			LogI("civilizations", "%s: the AI plays with it (%s; %d classes and %d alias commands)", c->id.c_str(),
				c->ownAi ? "with the content package's scripts" : "with the game's scripts, which see the base", static_cast<int>(c->aliasClasses.size()),
				static_cast<int>(c->aliasOrders.size()));
		}
	}
	CivAiRebuild();
}

const Civ *CivByIndex(int index)
{
	return FindByIndex(index);
}

const Civ *CivByName(const std::string &name)
{
	return FindByName(name);
}

std::vector<Civ> CivsList()
{
	std::vector<Civ> out;
	std::lock_guard<std::mutex> lock(g_lock);
	for (const Civ *c : g_civs) out.push_back(*c);
	return out;
}

std::string CivDisplayName(const Civ &c)
{
	const std::string lang = ModsLanguage();
	for (const char *want : {lang.c_str(), "english", ""}) {
		for (const auto &n : c.names) {
			if (n.first == want) return n.second;
		}
	}
	if (!c.names.empty()) return c.names[0].second;
	std::string s = c.id;
	if (!s.empty()) s[0] = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
	return s;
}

std::vector<std::string> CivsErrorsFor(const std::string &folder)
{
	std::lock_guard<std::mutex> lock(g_lock);
	auto it = g_errors.find(folder);
	return it == g_errors.end() ? std::vector<std::string>() : it->second;
}

void CivsRegisterConstants(void *engine)
{
	std::vector<Civ> civs = CivsList();
	for (const Civ &c : civs) {
		std::string lowered = c.id;
		lowered[0] = static_cast<char>(toupper(static_cast<unsigned char>(lowered[0])));
		std::vector<std::string> names = {c.constant};
		if (lowered != c.constant) names.push_back(lowered);
		for (const std::string &name : names) {
			const char *err = reinterpret_cast<RegisterIntConstant_t>(addr::kRegisterIntConstant)(engine, nullptr, name.c_str(), c.index);
			if (err) LogE("civilizations", "could not register the constant %s for the scripts (%s)", name.c_str(), err);
			else LogD("civilizations", "script constant %s = %d", name.c_str(), c.index);
		}
	}
}

const char *CivsCityClassName(void *registry, const char *name, std::string &buf)
{
	std::string mapped;
	if (!t_cityScope || !ScopedName(name, mapped)) return name;
	const Civ *keep = t_cityScope;
	t_cityScope = nullptr;
	void *probe = g_origFindClass ? g_origFindClass(registry, nullptr, mapped.c_str()) : nullptr;
	t_cityScope = keep;
	if (!probe) return name;
	static int logged = 0;
	if (logged++ < 12) LogD("civilizations", "the starting city creates %s instead of %s", mapped.c_str(), name);
	buf = mapped;
	return buf.c_str();
}

const char *CivsRoomSeparator()
{
	return "------------";
}

namespace {
std::vector<RoomCiv> RoomCivs()
{
	std::vector<RoomCiv> out;
	for (const Civ *c : SortedCivs()) out.push_back({c->base, c->replacesBase, CivInRoom(*c)});
	return out;
}
}

std::vector<int> CivsRoomOrder(int items)
{
	return CivRoomOrder(RoomCivs(), items, GameInRoom());
}

bool CivsRoomAllows(int race)
{
	if (race == 8) return true;
	if (race >= 10) {
		const Civ *c = FindByIndex(race);
		return !c || CivInRoom(*c);
	}
	if (GameInRoom()) return true;
	for (const Civ *other : SortedCivs()) {
		if (other->replacesBase && other->base == race && CivInRoom(*other)) return true;
	}
	return false;
}

std::vector<CivSet> CivsSets()
{
	std::vector<CivSet> out = {{kCivSetGame, Tr("Game")}};
	for (const Civ *c : SortedCivs()) {
		if (c->modId.empty()) continue;
		if (std::any_of(out.begin(), out.end(), [&](const CivSet &s) { return s.id == c->modId; })) continue;
		ModInfo m;
		out.push_back({c->modId, ModsFind(c->modFolder, m) && !m.name.empty() ? m.name : c->modFolder});
	}
	return out;
}

int CivsRoomReplacement(int item, int items)
{
	return CivRoomReplacement(RoomCivs(), item, items);
}

int CivsRaceOfRoomItem(int item)
{
	if (item < 10) return CivRoomBaseOfItem(item);
	const std::vector<const Civ *> civs = SortedCivs();
	const size_t pos = static_cast<size_t>(item - 10);
	return pos < civs.size() ? civs[pos]->index : -1;
}

int CivsRoomItemOfRace(int race)
{
	if (race < 10) return CivRoomItemOfBase(race);
	const std::vector<const Civ *> civs = SortedCivs();
	for (size_t i = 0; i < civs.size(); i++) {
		if (civs[i]->index == race) return 10 + static_cast<int>(i);
	}
	return -1;
}

void CivsOnRoomShown(void *dlg)
{
	AddCivsToRoomCombos(dlg);
}

void CivsBuildBegin()
{
	g_slotCivs.clear();
	g_inBuild = true;
	g_buildThread = GetCurrentThreadId();
}

void CivsBuildEnd(void *settings)
{
	g_inBuild = false;
	if (g_slotCivs.empty()) return;
	char *gs = settings ? *reinterpret_cast<char **>(settings) : nullptr;
	if (!gs) {
		g_slotCivs.clear();
		return;
	}
	std::vector<int> entries;
	for (int i = 0; i < 8; i++) {
		const char *entry = gs + 0xa4 + i * 0xc0;
		if (*reinterpret_cast<const int *>(entry + 0xa0) == i) entries.push_back(i);
	}
	for (const SlotCiv &s : g_slotCivs) {
		if (s.slot < 0 || s.slot >= static_cast<int>(entries.size())) {
			LogW("civilizations", "could not find the player of room slot %d (%d candidates)", s.slot, static_cast<int>(entries.size()));
			continue;
		}
		const int i = entries[s.slot];
		char *entry = gs + 0xa4 + i * 0xc0;
		const int entryType = *reinterpret_cast<int *>(entry + 8);
		if ((s.type == 3 && entryType != 0) || (s.type == 1 && entryType != 1)) {
			LogW("civilizations", "slot %d (type %d) does not match player %d (type %d); its civilization is not changed", s.slot,
				s.type, i, entryType);
			continue;
		}
		const bool useBase = s.type == 1 && !s.ai;
		const int race = useBase ? s.base : s.civ;
		*reinterpret_cast<int *>(entry) = race;
		*reinterpret_cast<signed char *>(gs + 0x5c + i) = static_cast<signed char>(race);
		LogD("civilizations", "game player %d (slot %d) plays civilization %d%s", i, s.slot, race,
			useBase ? " (AI: the base of the chosen civilization)" : "");
	}
	g_slotCivs.clear();
}

void InstallCivs(bool knownExe)
{
	if (!knownExe) return;
	struct Site {
		uintptr_t addr;
		void *detour;
		void **orig;
		const char *what;
	} sites[] = {
		{addr::kGetRaceName, reinterpret_cast<void *>(&HookGetRaceName), reinterpret_cast<void **>(&g_origName), "GetRaceName"},
		{addr::kGetRaceLetter, reinterpret_cast<void *>(&HookGetRaceLetter), reinterpret_cast<void **>(&g_origLetter), "GetRaceLetter"},
		{addr::kGetRaceFolderName, reinterpret_cast<void *>(&HookGetRaceFolder), reinterpret_cast<void **>(&g_origFolder), "GetRaceFolderName"},
		{addr::kScriptGetRaceStrPref, reinterpret_cast<void *>(&HookRaceLetterNative<false, false>), reinterpret_cast<void **>(&g_origGetStrPref), "GetRaceStrPref"},
		{addr::kScriptGetRaceStrPrefLow, reinterpret_cast<void *>(&HookRaceLetterNative<false, true>), reinterpret_cast<void **>(&g_origGetStrPrefLow), "GetRaceStrPrefLow"},
		{addr::kScriptGetPlayerRace, reinterpret_cast<void *>(&HookGetPlayerRace), reinterpret_cast<void **>(&g_origPlayerRace), "GetPlayerRace"},
		{addr::kScriptObjRaceStrPref, reinterpret_cast<void *>(&HookRaceLetterNative<true, false>), reinterpret_cast<void **>(&g_origObjStrPref), "Obj::raceStrPref"},
		{addr::kScriptObjRaceStrPrefLow, reinterpret_cast<void *>(&HookRaceLetterNative<true, true>), reinterpret_cast<void **>(&g_origObjStrPrefLow), "Obj::raceStrPrefLow"},
		{addr::kClassStartElement, reinterpret_cast<void *>(&HookClassStartElement), reinterpret_cast<void **>(&g_origClassStart), "CVXScriptClass::OnXmlStartElement"},
		{addr::kGetSlotInfo, reinterpret_cast<void *>(&HookSlotInfo), reinterpret_cast<void **>(&g_origSlotInfo), "CVXUIMultiplayerGame::GetSlotInfo"},
		{addr::kFindTownTemplate, reinterpret_cast<void *>(&HookTown), reinterpret_cast<void **>(&g_origTown), "FindTownTemplateForRace"},
		{addr::kPlaceTownTemplate, reinterpret_cast<void *>(&HookPlaceTown), reinterpret_cast<void **>(&g_origPlaceTown), "PlaceTownTemplate"},
		{addr::kFindClassByName, reinterpret_cast<void *>(&HookFindClass), reinterpret_cast<void **>(&g_origFindClass), "CVXSCRegistry::FindByName"},
		{addr::kFindVillageTemplate, reinterpret_cast<void *>(&HookVillage), reinterpret_cast<void **>(&g_origVillage), "FindVillageTemplateForRace"},
		{addr::kMakeRandomName, reinterpret_cast<void *>(&HookRandomName), reinterpret_cast<void **>(&g_origRandomName), "MakeRandomPlayerName"},
		{addr::kCatapultByRace, reinterpret_cast<void *>(&HookCatapult), reinterpret_cast<void **>(&g_origCatapult), "GetCatapultTypeByRace"},
		{addr::kGenerateRandomMap, reinterpret_cast<void *>(&HookGenerateRandomMap), reinterpret_cast<void **>(&g_origGenerateMap), "GenerateRandomMap"},
		{addr::kBestTemple, reinterpret_cast<void *>(&HookBestTemple), reinterpret_cast<void **>(&g_origBestTemple), "Settlement::BestTemple"},
		{addr::kComputeNeeds, reinterpret_cast<void *>(&HookComputeNeeds), reinterpret_cast<void **>(&g_origComputeNeeds), "CCounterUnits::ComputeNeeds"},
	};
	for (const Site &s : sites) {
		if (HookCreate(reinterpret_cast<void *>(s.addr), s.detour, s.orig) != MH_OK || HookEnable(reinterpret_cast<void *>(s.addr)) != MH_OK) {
			LogE("civilizations", "could not hook %s", s.what);
		}
	}
	if (!PatchRandomDraw()) LogW("civilizations", "could not change the random civilization draw (the code is not the expected one)");
	g_hooked = true;
	ApplyNeutralPrefixes();
	LogI("civilizations", "hooked");
}

namespace {

std::vector<std::string> CivProblemsFor(const std::string &folder)
{
	std::vector<std::string> out;
	for (const std::string &e : CivsErrorsFor(folder)) out.push_back(Tr("Civilization ") + e);
	return out;
}

const bool g_featureRegistered = (ModsRegisterFeature({20, &CivsRebuild, &CivProblemsFor}), true);

const bool g_classNameMapperRegistered = (DiagSetClassNameMapper(&CivsCityClassName), true);

}
