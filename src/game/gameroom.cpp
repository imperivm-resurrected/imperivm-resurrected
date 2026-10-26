// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <deque>
#include <functional>
#include <mutex>
#include <algorithm>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "game/civs.h"
#include "core/crashlog.h"
#include "game/gameroom.h"
#include "game/gamemodes.h"
#include "hooks/gamefiles.h"
#include "mods/civsets.h"
#include "mods/mods.h"
#include "mods/modoptions.h"
#include "mods/modvars.h"
#include "game/netlink.h"
#include "core/log.h"
#include "core/names.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "core/uiport.h"

namespace {
const unsigned kMsgClick = 0x12345604;

typedef void(__fastcall *ThisOnly_t)(void *self);
typedef int(__fastcall *IntThis_t)(void *self);
typedef int(__fastcall *GetSlotInfo_t)(void *dlg, void *edx, int slot, const char **name, int *type, int *color, int *ready,
	int *race, int *team, int *field30);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef int(__fastcall *ComboGetSelected_t)(void *combo);
typedef void(__fastcall *ComboSetSelected_t)(void *combo, void *edx, int index, int notify);
typedef void(__fastcall *SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);
typedef void(__fastcall *SetValue_t)(void *ctrl, void *edx, int value, int notify);
typedef void(__fastcall *GetTextA_t)(void *text, void *edx, char *buf, int size);
typedef void(__fastcall *SelectExclusive_t)(void *list, void *edx, unsigned index, int notify, int redraw);
typedef int(__fastcall *PickerOnShow_t)(void *picker);

ThisOnly_t g_origOnShow = nullptr;
ThisOnly_t g_origDtor = nullptr;

void *g_dlg = nullptr;
void *g_picker = nullptr;
int g_scenarioRace[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
typedef void(__fastcall *UpdateRow_t)(void *, void *, unsigned);
unsigned g_siteFlags = 0;
unsigned g_ruleFlags = 0;

std::string RoomIniPath()
{
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char *slash = strrchr(path, '\\')) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	return path;
}

unsigned LoadRoomFlags(const char *key, unsigned def)
{
	const UINT v = GetPrivateProfileIntA("Room", key, -1, RoomIniPath().c_str());
	return v == 0xFFFFFFFFu ? def : v;
}

void SaveRoomFlags(const char *key, unsigned value)
{
	WritePrivateProfileStringA("Room", key, std::to_string(value).c_str(), RoomIniPath().c_str());
}
typedef int(__fastcall *GetGameOptions_t)(void *dlg);
PickerOnShow_t g_origPickerOnShow = nullptr;
ThisOnly_t g_origPickerDtor = nullptr;
std::mutex g_lock;
GameRoomSnapshot g_snap;
std::deque<std::function<void()>> g_actions;

void *Child(void *dlg, int id, uintptr_t className)
{
	if (!dlg) return nullptr;
	void **vt = *reinterpret_cast<void ***>(dlg);
	return reinterpret_cast<GetChild_t>(vt[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(className));
}

void *Combo(void *dlg, int id) { return Child(dlg, id, addr::kClassNameCUIComboBox); }
void *Button(void *dlg, int id) { return Child(dlg, id, addr::kClassNameCUIButton); }

std::vector<std::string> ListItems(void *list)
{
	std::vector<std::string> out;
	if (!list) return out;
	const char *l = static_cast<const char *>(list);
	const unsigned count = *reinterpret_cast<const unsigned *>(l + 0x58);
	const char *items = *reinterpret_cast<char *const *>(l + 0x94);
	for (unsigned i = 0; items && i < count && i < 512; i++) {
		const char *text = *reinterpret_cast<const char *const *>(items + i * 0x14);
		out.push_back(text ? text : "");
	}
	return out;
}

std::string ScenarioName(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string AnsiToUtf8(const char *text)
{
	wchar_t w[2048];
	const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, w, 2048);
	if (n <= 0) return text;
	char out[4096];
	return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof(out), nullptr, nullptr) > 0 ? out : text;
}

std::vector<std::string> ComboItems(void *combo)
{
	std::vector<std::string> out;
	if (!combo) return out;
	char *list = *reinterpret_cast<char **>(static_cast<char *>(combo) + 0x64);
	if (!list) return out;
	const unsigned count = *reinterpret_cast<unsigned *>(list + 0x58);
	char *items = *reinterpret_cast<char **>(list + 0x94);
	for (unsigned i = 0; items && i < count && i < 128; i++) {
		const char *text = *reinterpret_cast<const char **>(items + i * 0x14);
		out.push_back(text ? text : "");
	}
	return out;
}

int ComboSel(void *combo)
{
	return combo ? reinterpret_cast<ComboGetSelected_t>(addr::kComboGetSelected)(combo) : -1;
}

bool Checked(void *button)
{
	return button && *reinterpret_cast<short *>(static_cast<char *>(button) + 0x7e) == 1;
}

void Click(void *button)
{
	if (!button) return;
	char *b = static_cast<char *>(button);
	void **vt = *reinterpret_cast<void ***>(button);
	reinterpret_cast<SendNotify_t>(vt[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgClick,
		*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}

void ToggleCheck(void *button)
{
	if (!button) return;
	void **vt = *reinterpret_cast<void ***>(button);
	reinterpret_cast<SetValue_t>(vt[0x70 / 4])(button, nullptr, Checked(button) ? 0 : 1, 0);
	Click(button);
}

void SelectCombo(void *combo, int index)
{
	if (combo) reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(combo, nullptr, index, 1);
}

const int kMapSizeCombo = 0x01000008;
const int kMapSizes = 3;
const size_t kTemplateEntrySize = 0x44;
const size_t kTemplateSizeField = 0x38;

const char *TemplateName(const char *entry)
{
	const unsigned capacity = *reinterpret_cast<const unsigned *>(entry + 0x34);
	return capacity < 0x10 ? entry + 0x20 : *reinterpret_cast<const char *const *>(entry + 0x20);
}

bool SizesOfMapType(void *options, void *typeCombo, int selected, bool fits[kMapSizes])
{
	for (int i = 0; i < kMapSizes; i++) fits[i] = false;
	__try {
		const char *list = *reinterpret_cast<char *const *>(static_cast<char *>(typeCombo) + 0x64);
		const unsigned count = *reinterpret_cast<const unsigned *>(list + 0x58);
		if (selected <= 0 || static_cast<unsigned>(selected) >= count) return false;
		const char *items = *reinterpret_cast<const char *const *>(list + 0x94);
		const unsigned chosen = *reinterpret_cast<const unsigned *>(items + selected * 0x14 + 0xc);
		const char *begin = *reinterpret_cast<char *const *>(static_cast<char *>(options) + 0x6c);
		const char *end = *reinterpret_cast<char *const *>(static_cast<char *>(options) + 0x70);
		if (!begin || end <= begin) return false;
		const unsigned total = static_cast<unsigned>((end - begin) / kTemplateEntrySize);
		if (chosen >= total) return false;
		const char *want = TemplateName(begin + chosen * kTemplateEntrySize);
		for (unsigned i = 0; i < total; i++) {
			const char *entry = begin + i * kTemplateEntrySize;
			if (_stricmp(TemplateName(entry), want) != 0) continue;
			const int size = *reinterpret_cast<const int *>(entry + kTemplateSizeField);
			if (size >= 0 && size < kMapSizes) fits[size] = true;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}


struct OptionDef {
	int id;
	const char *label;
	bool checkbox;
};
const OptionDef kOptions[] = {
	{0x1000, "Map type", false}, {0x01000008, "Map size", false}, {0x1002, "Game type", false},
	{0x1003, "Limit", false}, {0x1004, "Population", false}, {0x1005, "Starting gold", false},
	{0x1007, "No fog", true}, {0x1008, "Map pre-explored", true}, {0x1009, "Shared support", true},
	{0x100a, "Shared control", true}, {0x10a7, "Disable bonuses", true},
};

int AlternativeItem(const std::vector<std::string> &items)
{
	for (size_t i = 1; i < items.size(); i++) {
		if (_stricmp(items[i].c_str(), "Alternative") == 0) return static_cast<int>(i);
	}
	return -1;
}

std::vector<int> DisplayOrder(int id, const std::vector<std::string> &items)
{
	std::vector<int> order;
	for (size_t i = 0; i < items.size(); i++) order.push_back(static_cast<int>(i));
	if (id == 0x1002) {
		const int alt = AlternativeItem(items);
		if (alt > 0) {
			order.clear();
			order.push_back(alt);
			for (size_t i = 1; i < items.size(); i++) {
				if (static_cast<int>(i) != alt) order.push_back(static_cast<int>(i));
			}
		}
	}
	if (id == 0x1005) {
		for (size_t i = 1; i < items.size(); i++) {
			if (_stricmp(items[i].c_str(), "Por defecto") == 0 || _stricmp(items[i].c_str(), "Default") == 0) {
				std::rotate(order.begin(), order.begin() + i, order.begin() + i + 1);
				break;
			}
		}
	}
	return order;
}

bool g_optionsLogged = false;

int PickerCount(void *picker)
{
	__try {
		const char *first = *reinterpret_cast<char *const *>(static_cast<char *>(picker) + 0x64);
		const char *last = *reinterpret_cast<char *const *>(static_cast<char *>(picker) + 0x68);
		if (!first || last < first || (last - first) % 0x1a0 != 0 || (last - first) / 0x1a0 > 4096) return 0;
		return static_cast<int>((last - first) / 0x1a0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

bool PickerPath(void *picker, int i, char *buf, unsigned bufSize)
{
	__try {
		const char *e = *reinterpret_cast<char *const *>(static_cast<char *>(picker) + 0x64) + i * 0x1a0;
		const unsigned size = *reinterpret_cast<const unsigned *>(e + 0x14);
		const unsigned cap = *reinterpret_cast<const unsigned *>(e + 0x18);
		const char *text = cap < 0x10 ? e + 4 : *reinterpret_cast<const char *const *>(e + 4);
		unsigned k = 0;
		for (; text && k < size && k + 1 < bufSize; k++) buf[k] = text[k];
		buf[k] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		buf[0] = 0;
		return false;
	}
}

std::vector<std::string> PickerPaths(void *picker)
{
	std::vector<std::string> out;
	const int n = PickerCount(picker);
	for (int i = 0; i < n; i++) {
		char buf[264];
		PickerPath(picker, i, buf, sizeof(buf));
		out.emplace_back(buf);
	}
	return out;
}

const int kBonusEntrySize = 0x200, kBonusMax = 64;

int BonusCount(void *dlg)
{
	__try {
		const char *begin = *reinterpret_cast<char *const *>(static_cast<char *>(dlg) + 0x68);
		const char *end = *reinterpret_cast<char *const *>(static_cast<char *>(dlg) + 0x6c);
		if (!begin || end < begin) return 0;
		const size_t bytes = static_cast<size_t>(end - begin);
		if (bytes % kBonusEntrySize || bytes / kBonusEntrySize > kBonusMax) return 0;
		return static_cast<int>(bytes / kBonusEntrySize);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

bool BonusEntry(void *dlg, int index, char *name, char *path)
{
	__try {
		const char *entry = *reinterpret_cast<char *const *>(static_cast<char *>(dlg) + 0x68) + index * kBonusEntrySize;
		strncpy_s(name, 256, entry, _TRUNCATE);
		strncpy_s(path, 256, entry + 0x100, _TRUNCATE);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		name[0] = path[0] = 0;
		return false;
	}
}

std::string BonusLanguageCode()
{
	const std::string &language = ModsLanguage();
	if (language == "italian") return "it";
	if (language == "spanish" || language.empty()) return "es";
	return "en";
}

std::vector<BonusDef> g_bonusDefs;
void *g_bonusFor = nullptr;
int g_bonusCount = 0;
std::string g_bonusLanguage;

const std::vector<BonusDef> &RoomBonuses(void *dlg)
{
	const int count = BonusCount(dlg);
	const std::string language = BonusLanguageCode();
	if (dlg == g_bonusFor && count == g_bonusCount && language == g_bonusLanguage) return g_bonusDefs;
	g_bonusFor = dlg;
	g_bonusCount = count;
	g_bonusLanguage = language;
	g_bonusDefs.clear();
	std::vector<int> numbers;
	for (int i = 0; i < count; i++) {
		char name[256], path[256];
		if (!BonusEntry(dlg, i, name, path)) break;
		const int number = BonusFileNumber(path);
		if (number < 0 || number >= kBonusRoomLimit) break;
		std::vector<char> bytes;
		std::string script;
		if (path[0] && ReadGameFile(path, 1, bytes)) script.assign(bytes.begin(), bytes.end());
		g_bonusDefs.push_back(BonusDefine(name, path, script, language));
		numbers.push_back(number);
	}
	for (size_t i = 0; i < g_bonusDefs.size(); i++) {
		bool repeated = false;
		for (size_t k = 0; k < g_bonusDefs.size(); k++) repeated = repeated || (k != i && g_bonusDefs[k].name == g_bonusDefs[i].name);
		if (repeated) g_bonusDefs[i].name += " (" + std::to_string(numbers[i]) + ")";
	}
	LogD("room", "bonuses: %d", static_cast<int>(g_bonusDefs.size()));
	return g_bonusDefs;
}

bool SlotHandshaking(int row)
{
	if (!g_dlg || row < 0 || row > 7) return false;
	char *shell = *reinterpret_cast<char **>(static_cast<char *>(g_dlg) + 0x8228);
	if (!shell) return false;
	const int id = *(static_cast<char *>(g_dlg) + 0xa0 + row);
	char *record = *reinterpret_cast<char **>(shell + 8) + id * 0x21;
	return *reinterpret_cast<void **>(record + 0x1d) != nullptr && *reinterpret_cast<int *>(record + 8) == 0;
}

void TakeSnapshot()
{
	GameRoomSnapshot s;
	if (g_dlg) {
		s.active = true;
		s.host = reinterpret_cast<IntThis_t>(addr::kIsHostOrOffline)(g_dlg) != 0;
		{
			static void *defaultsFor = nullptr;
			if (s.host && g_dlg != defaultsFor) {
				defaultsFor = g_dlg;
				if (ModOptionsDefaults()) GameRoomSetRules(g_ruleFlags);
				GameRoomRefreshChoices();
			}
		}
		s.online = *reinterpret_cast<void **>(static_cast<char *>(g_dlg) + 0x8228) != nullptr;
		s.bonuses = RoomBonuses(g_dlg);
		s.localSlot = reinterpret_cast<IntThis_t>(addr::kGetLocalSlot)(g_dlg);
		memcpy(s.colors, reinterpret_cast<const void *>(addr::kPlayerColorTable), sizeof(s.colors));
		for (int i = 0; i < 8; i++) {
			GameRoomSlot &slot = s.slots[i];
			const char *name = nullptr;
			int type = 0, color = 0, ready = 0, race = 0, team = 0xff, extra = 0;
			slot.used = reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(g_dlg, nullptr, i, &name, &type, &color, &ready, &race,
							&team, &extra) != 0 && (type == 1 || type == 3);
			slot.name = name ? name : "";
			slot.joining = s.host && SlotHandshaking(i);
			slot.type = type;
			slot.color = color;
			slot.ready = ready != 0;
			slot.team = team;
			slot.bonus = extra;
			slot.local = i == s.localSlot;
			const int base = (i + 1) << 12;
			void *raceCombo = Combo(g_dlg, base | 0x22);
			slot.raceNames = ComboItems(raceCombo);
			slot.race = ComboSel(raceCombo);
			if (raceCombo && slot.used && (slot.race < 0 || slot.race >= static_cast<int>(slot.raceNames.size())) && race >= 0 &&
				race < static_cast<int>(slot.raceNames.size())) {
				static int fixed = 0;
				if (fixed++ < 5) LogD("room", "the civilization of slot %d (%d) was not selected in the dropdown (%d); setting it again", i, race, slot.race);
				reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(raceCombo, nullptr, race, 0);
				slot.race = ComboSel(raceCombo);
			}
			if (raceCombo && slot.used && !slot.raceNames.empty() && (slot.race < 0 || slot.race >= static_cast<int>(slot.raceNames.size())) &&
				race >= 10 && !CivByIndex(race)) {
				static int reset = 0;
				if (reset++ < 5) LogW("room", "the civilization of slot %d (%d) no longer exists (mod disabled?); setting \"%s\"", i, slot.race, slot.raceNames[0].c_str());
				reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(raceCombo, nullptr, 0, 0);
				slot.race = ComboSel(raceCombo);
			}
			slot.fixedRace = *reinterpret_cast<int *>(static_cast<char *>(g_dlg) + 0x9c) == 0 ? g_scenarioRace[i] : -1;
			if (slot.fixedRace >= 0 && slot.fixedRace < static_cast<int>(slot.raceNames.size())) slot.race = slot.fixedRace;
			void *nameCombo = Combo(g_dlg, base | 0x02);
			slot.nameItems = ComboItems(nameCombo);
			slot.nameSel = ComboSel(nameCombo);
			const unsigned raw = *reinterpret_cast<unsigned short *>(static_cast<char *>(g_dlg) + 0xcc);
			const unsigned positions = raw && raw <= 8 ? raw : 8;
			const unsigned char mark = *(static_cast<unsigned char *>(g_dlg) + 0x84 + 0x2c + i);
			slot.exists = static_cast<unsigned>(i) < positions && mark != 1;
			slot.aiAllowed = slot.exists && mark == 2;
		}
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		if (s.host && opts) {
			for (const OptionDef &d : kOptions) {
				GameRoomOption o;
				o.id = d.id;
				o.label = d.label;
				o.checkbox = d.checkbox;
				if (d.checkbox) {
					void *b = Button(opts, d.id);
					if (!b) continue;
					o.value = Checked(b);
					s.optionsText += std::string(d.label) + ": " + (o.value ? "yes" : "no") + "\n";
				}
				else {
					void *c = Combo(opts, d.id);
					if (!c) continue;
					const std::vector<std::string> real = ComboItems(c);
					const int realSel = ComboSel(c);
					const std::vector<int> order = DisplayOrder(d.id, real);
					for (size_t k = 0; k < order.size(); k++) {
						const bool eliminationWithStats = d.id == 0x1002 && order[k] != 0 && order[k] == AlternativeItem(real);
						o.items.push_back(eliminationWithStats ? real[0] : real[order[k]]);
						if (order[k] == realSel) o.sel = static_cast<int>(k);
					}
					if (o.sel < 0 && d.id == 0x1002 && realSel == 0 && AlternativeItem(real) > 0) o.sel = 0;
					if (o.sel >= 0 && o.sel < static_cast<int>(o.items.size())) {
						s.optionsText += std::string(d.label) + ": " + o.items[o.sel] + "\n";
					}
				}
				if (d.id == kMapSizeCombo && !d.checkbox) {
					void *typeCombo = Combo(opts, 0x1000);
					bool fits[kMapSizes];
					if (typeCombo && SizesOfMapType(opts, typeCombo, ComboSel(typeCombo), fits)) {
						o.itemDisabled.assign(o.items.size(), 0);
						for (size_t k = 0; k < o.items.size() && k < kMapSizes; k++) o.itemDisabled[k] = fits[k] ? 0 : 1;
					}
				}
				s.options.push_back(o);
			}
			if (!g_optionsLogged && !s.options.empty()) {
				g_optionsLogged = true;
				if (!NetMultiplayerSaveLoading()) {
					void *gold = Combo(opts, 0x1005);
					const std::vector<std::string> real = ComboItems(gold);
					int def = -1;
					for (size_t i = 0; i < real.size(); i++) {
						if (_stricmp(real[i].c_str(), "Por defecto") == 0 || _stricmp(real[i].c_str(), "Default") == 0) def = static_cast<int>(i);
					}
					const int now = ComboSel(gold);
					if (def >= 0 && def != now) {
						LogD("room", "starting gold set to \"%s\" (was \"%s\")", real[def].c_str(), now >= 0 && now < static_cast<int>(real.size()) ? real[now].c_str() : "?");
						SelectCombo(gold, def);
					}
				}
				for (const GameRoomOption &o : s.options) {
					if (o.checkbox) {
						LogI("room", "options: 0x%x %s = %s", o.id, o.label.c_str(), o.value ? "yes" : "no");
						continue;
					}
					std::string list;
					for (size_t i = 0; i < o.items.size(); i++) list += (i ? " | " : "") + std::to_string(i) + "=\"" + o.items[i] + "\"";
					LogI("room", "options: 0x%x %s, chosen %d of %d: %s", o.id, o.label.c_str(), o.sel, static_cast<int>(o.items.size()), list.c_str());
				}
			}
		}
		{
			const char *ro = reinterpret_cast<const char *>(reinterpret_cast<GetGameOptions_t>(addr::kGetGameOptions)(g_dlg));
			const unsigned bits = ro ? *reinterpret_cast<const unsigned *>(ro + 0x34) : 0;
			s.siteFlags = bits & kSiteMask;
			s.rules = (s.host ? g_ruleFlags : bits) & kRuleMask;
			s.randomMap = ro ? *reinterpret_cast<const int *>(ro + 0x18) != 0 : *reinterpret_cast<int *>(static_cast<char *>(g_dlg) + 0x9c) != 0;
			if (ro) s.mode.assign(ro + 0x60, strnlen(ro + 0x60, 0x20));
			if (const GameMode *mode = GameModeFor(s.mode.c_str())) {
				if (mode->bonus >= 0) {
					for (GameRoomSlot &slot : s.slots) slot.bonus = mode->bonus;
				}
			}
		}
		if (s.host && s.randomMap) {
			static const struct {
				unsigned bit;
				const char *label;
			} kSites[] = {{kSiteRuins, "No ruins"}, {kSiteCaves, "No caves"}, {kSiteItems, "No item sites"},
				{kSiteStrongholds, "No neutral strongholds"}};
			for (const auto &site : kSites) s.optionsText += std::string(site.label) + ": " + ((s.siteFlags & site.bit) ? "yes" : "no") + "\n";
		}
		if (s.host) {
			s.optionsText += std::string("Hidden civilizations: ") + ((s.rules & kRuleBlindPick) ? "yes" : "no") + "\n";
			s.optionsText += std::string("Free unit attachment: ") + ((s.rules & kRuleFreeAttach) ? "yes" : "no") + "\n";
			s.optionsText += std::string("Diplomacy locked: ") + ((s.rules & kRuleLockDiplomacy) ? "yes" : "no") + "\n";
			s.optionsText += std::string("Cheats: ") + ((s.rules & kRuleCheats) ? "yes" : "no") + "\n";
			for (const ModOption &o : ModOptions()) {
				if (o.id == kSeasonOptionId && !s.randomMap) continue;
				const int v = ModOptionGet(o, s.rules);
				if (o.values.empty()) {
					s.optionsText += o.name + (v ? ": yes\n" : ": no\n");
				}
				else s.optionsText += o.name + ": " + o.values[v < static_cast<int>(o.values.size()) ? v : 0] + "\n";
			}
		}
		if (*reinterpret_cast<int *>(static_cast<char *>(g_dlg) + 0x9c) == 0) {
			const char *path = static_cast<const char *>(g_dlg) + 0x114;
			s.scenario.assign(path, strnlen(path, 260));
			if (!s.scenario.empty()) s.optionsText += "Scenario: " + ScenarioName(s.scenario) + "\n";
		}
		if (g_picker) {
			s.picker = true;
			s.pickerItems = ListItems(Child(g_picker, 0xa105, addr::kClassNameCUISList));
			void *desc = Child(g_picker, 0xa100, addr::kClassNameCUITextW);
			if (desc) {
				char buf[2048] = {};
				reinterpret_cast<GetTextA_t>(addr::kTextGetTextA)(desc, nullptr, buf, sizeof(buf) - 1);
				s.pickerDescription = AnsiToUtf8(buf);
			}
			s.pickerPaths = PickerPaths(g_picker);
		}
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_snap = std::move(s);
}


void Queue(std::function<void()> fn)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_actions.push_back(std::move(fn));
}

void __fastcall HookOnShow(void *dlg)
{
	g_origOnShow(dlg);
	CivsOnRoomShown(dlg);
	g_dlg = dlg;
	g_optionsLogged = false;
	g_siteFlags = LoadRoomFlags("Sites", 0) & kSiteMask;
	g_ruleFlags = LoadRoomFlags("Rules", ModOptionsDefaults()) & kRuleMask;
	LogI("room", "open (managed by the mod's interface)");
}

void __fastcall HookDtor(void *dlg)
{
	if (dlg == g_dlg) {
		g_dlg = nullptr;
		std::lock_guard<std::mutex> lock(g_lock);
		g_snap = GameRoomSnapshot();
		g_actions.clear();
		LogI("room", "closed");
	}
	g_origDtor(dlg);
}

int __fastcall HookPickerOnShow(void *picker)
{
	const int r = g_origPickerOnShow(picker);
	g_picker = picker;
	LogI("room", "scenario selector open");
	return r;
}

void __fastcall HookPickerDtor(void *picker)
{
	if (picker == g_picker) g_picker = nullptr;
	g_origPickerDtor(picker);
}

bool HookAt(uintptr_t addr, void *detour, void **orig)
{
	void *t = reinterpret_cast<void *>(addr);
	return HookCreate(t, detour, orig) == MH_OK && HookEnable(t) == MH_OK;
}

typedef int(__fastcall *ApplyToGame_t)(void *self, void *edx, char *opts);
typedef unsigned(__cdecl *AskYesNo_t)(const char *text);
ApplyToGame_t g_origApplyToGame = nullptr;
AskYesNo_t g_origAskYesNo = nullptr;
char *g_applyOpts = nullptr;

typedef void(__fastcall *SetSlotInfo_t)(void *room, void *edx, unsigned slot, const char *name, int *type, unsigned *color, int *ready, int *race, int *team, int *extra);
SetSlotInfo_t g_origSetSlotInfo = nullptr;
int g_keptRaces = 0;

void __fastcall HookSetSlotInfo(void *room, void *edx, unsigned slot, const char *name, int *type, unsigned *color, int *ready, int *race, int *team, int *extra)
{
	if (g_applyOpts && race && slot < 8 && !strcmp(static_cast<char *>(room) + 0x114, g_applyOpts + 0x90)) {
		const char *currentName = nullptr;
		int currentType = 0, currentColor = 0, currentReady = 0, currentRace = 0, currentTeam = 0, currentExtra = 0;
		if (reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(room, nullptr, static_cast<int>(slot), &currentName, &currentType, &currentColor, &currentReady, &currentRace, &currentTeam,
				&currentExtra) &&
			currentRace != *race) {
			g_keptRaces++;
			*race = currentRace;
		}
	}
	g_origSetSlotInfo(room, edx, slot, name, type, color, ready, race, team, extra);
}

void LogApplied(const char *opts)
{
	const bool random = *reinterpret_cast<const int *>(opts + 0x18) != 0;
	const unsigned positions = *reinterpret_cast<const unsigned short *>(opts + 0x48);
	char mapName[0x20] = {};
	if (random) strncpy_s(mapName, opts + 0x24, _TRUNCATE);
	char mode[0x21] = {};
	strncpy_s(mode, opts + 0x60, _TRUNCATE);
	void *dialog = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
	void *sizeCombo = dialog ? Combo(dialog, kMapSizeCombo) : nullptr;
	const int size = sizeCombo ? ComboSel(sizeCombo) : -1;
	char line[300];
	sprintf_s(line, "%s%s, size %d, %u positions, mode \"%s\", flags 0x%x, %u mods loaded", random ? "random map " : "scenario", random ? mapName : "", size, positions, mode,
		*reinterpret_cast<const unsigned *>(opts + 0x34), static_cast<unsigned>(ModsMountedProfile().mods.size()));
	static std::string last;
	if (last == line) return;
	last = line;
	LogI("room", "options applied: %s", line);
}

int __fastcall HookApplyToGame(void *self, void *edx, char *opts)
{
	char *prev = g_applyOpts;
	g_applyOpts = opts;
	g_keptRaces = 0;
	const int r = g_origApplyToGame(self, edx, opts);
	g_applyOpts = prev;
	if (g_keptRaces) LogD("room", "applying the options kept the civilization of %d slots instead of the map's default", g_keptRaces);
	if (r && opts) {
		unsigned &flags = *reinterpret_cast<unsigned *>(opts + 0x34);
		const bool random = *reinterpret_cast<int *>(opts + 0x18) != 0;
		flags = (flags & ~(kSiteMask | kRuleMask)) | (random ? g_siteFlags : 0) | (g_ruleFlags & (random ? ~0u : ~kSeasonBits));
		if (!NetMultiplayerSaveLoading()) GameModeApplyOptions(opts);
		LogApplied(opts);
	}
	return r;
}

unsigned g_pendingRules = 0;
DWORD g_pendingRulesTick = 0;
bool g_gameRunning = false;

unsigned PeekGameRules()
{
	unsigned savedRules = 0;
	unsigned long savedTick = 0;
	const bool haveSaved = ModRulesFromSavePeek(savedRules, savedTick) && static_cast<int>(GetTickCount() - savedTick) < 120000;
	if (haveSaved && (!g_pendingRulesTick || static_cast<int>(savedTick - g_pendingRulesTick) > 0)) return savedRules & kRuleMask;
	if (g_pendingRulesTick && static_cast<int>(GetTickCount() - g_pendingRulesTick) < 120000) return g_pendingRules;
	if (NetRejoinLoading()) return NetRoomRules() & kRuleMask;
	return 0;
}

typedef void *(__fastcall *CreateWorldCore_t)(void *game, void *edx, void *helper, unsigned a, int b, unsigned c, unsigned d);
CreateWorldCore_t g_origCreateWorldCore = nullptr;

void *__fastcall HookCreateWorldCore(void *game, void *edx, void *helper, unsigned a, int b, unsigned c, unsigned d)
{
	if (!ModRulesActive()) {
		const unsigned late = PeekGameRules();
		if (late) {
			unsigned consumed = 0;
			unsigned long tick = 0;
			ModRulesFromSaveTake(consumed, tick);
			ModRulesSetActive(late);
			LogI("room", "game rules: 0x%x (the saved game, read after the game started)", late);
		}
	}
	for (const ModOption &o : ModOptions()) {
		if (o.id != kSeasonOptionId) continue;
		const unsigned active = ModRulesActive();
		int choice = ModOptionGet(o, active ? active : PeekGameRules());
		if (choice < 0 || choice > 2) choice = 0;
		LogD("room", "world creation: season option %d, season of the game %d", choice, *reinterpret_cast<int *>(static_cast<char *>(game) + 0x174));
		if (choice > 0) {
			*reinterpret_cast<int *>(static_cast<char *>(game) + 0x174) = choice + 1;
			LogI("room", "season of the game: %s", choice == 1 ? "autumn" : "winter");
		}
		break;
	}
	return g_origCreateWorldCore(game, edx, helper, a, b, c, d);
}

typedef void(__fastcall *BuildSettings_t)(void *dlg, void *edx, void *settings);
BuildSettings_t g_origBuildSettings = nullptr;

void __fastcall HookBuildSettings(void *dlg, void *edx, void *settings)
{
	CivsBuildBegin();
	g_origBuildSettings(dlg, edx, settings);
	CivsBuildEnd(settings);
	if (!NetMultiplayerSaveLoading()) GameModeApplySettings(static_cast<char *>(settings));
	const char *opts = reinterpret_cast<const char *>(reinterpret_cast<GetGameOptions_t>(addr::kGetGameOptions)(dlg));
	const unsigned rules = opts ? *reinterpret_cast<const unsigned *>(opts + 0x34) & kRuleMask : 0;
	g_pendingRules = rules;
	g_pendingRulesTick = GetTickCount() | 1;
	if (g_gameRunning) ModRulesSetActive(rules);
	LogI("room", "game rules: (room) 0x%x%s", rules, g_gameRunning ? " (a game was in progress)" : "");
}

typedef long(__fastcall *EnvGetInt_t)(void *env, void *edx, const char *key);
EnvGetInt_t g_origEnvGetInt = nullptr;

long __fastcall HookEnvGetInt(void *env, void *edx, const char *key)
{
	if ((ModRulesActive() & kRuleFreeAttach) && key) {
		if (strcmp(key, "modo") == 0) return 0;
		if (strcmp(key, "GM_FreeAttachment") == 0) return 1;
	}
	const long value = g_origEnvGetInt(env, edx, key);
	if (value == 0 && key && strcmp(key, "modo") == 0) return 1;
	return value;
}

typedef void(__fastcall *ThisVoid_t)(void *self);
ThisVoid_t g_origDiplomacyExecute = nullptr, g_origDiplomacyApply = nullptr;

void __fastcall HookDiplomacyExecute(void *cmd)
{
	if ((ModRulesActive() & kRuleLockDiplomacy) && *reinterpret_cast<int *>(static_cast<char *>(cmd) + 0x10) != -1) {
		LogW("room", "rules: diplomacy change of player %d ignored (diplomacy locked)", *reinterpret_cast<int *>(static_cast<char *>(cmd) + 0xc));
		return;
	}
	g_origDiplomacyExecute(cmd);
}

void __fastcall HookDiplomacyApply(void *dlg)
{
	if (ModRulesActive() & kRuleLockDiplomacy) {
		UiToast("Diplomacy is locked in this game.");
		return;
	}
	g_origDiplomacyApply(dlg);
}

unsigned __cdecl HookAskYesNo(const char *text)
{
	if (g_dlg && g_applyOpts && text && strstr(text, "does not support that number of players")) {
		const unsigned positions = *reinterpret_cast<unsigned short *>(g_applyOpts + 0x48);
		std::vector<std::string> names;
		for (int i = 0; i < 8; i++) {
			const char *name = nullptr;
			int type = 0, color = 0, ready = 0, race = 0, team = 0xff, extra = 0;
			if (!reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(g_dlg, nullptr, i, &name, &type, &color, &ready, &race, &team, &extra)) continue;
			const unsigned char mark = *reinterpret_cast<unsigned char *>(g_applyOpts + 0x2c + i);
			if (type == 3 && (mark == 1 || static_cast<unsigned>(i) >= positions)) names.push_back(name ? name : "");
		}
		LogW("room", "the new map is for %u players; %u do not fit", positions, static_cast<unsigned>(names.size()));
		const bool yes = UiAskDropPlayers(names, static_cast<int>(positions));
		LogI("room", "new map with fewer places: %s", yes ? "those who do not fit are removed and the map is used" : "the map is left as it was");
		return yes ? 1 : 0;
	}
	return g_origAskYesNo(text);
}

typedef void(__fastcall *RefreshOptions_t)(void *room);
typedef void *(__cdecl *CreateSettings_t)(int *path, int *out, int type, void *xml);
RefreshOptions_t g_origRefreshOptions = nullptr;
CreateSettings_t g_origCreateSettings = nullptr;
DWORD g_refreshThread = 0;

int RaceIdToSlotChoice(int id)
{
	static const int kChoice[10] = {2, 0, 4, 3, 1, 7, 6, 5, 8, 9};
	return id >= 0 && id < 10 ? kChoice[id] : id;
}

const GameMode *CurrentMode()
{
	if (!g_dlg) return nullptr;
	const char *ro = reinterpret_cast<const char *>(reinterpret_cast<GetGameOptions_t>(addr::kGetGameOptions)(g_dlg));
	if (!ro) return nullptr;
	char key[0x21] = {};
	memcpy(key, ro + 0x60, 0x20);
	return GameModeFor(key);
}

bool LockedByMode(int id)
{
	const GameMode *mode = CurrentMode();
	if (!GameModeLocksOption(mode, id)) return false;
	LogD("room", "option 0x%x is fixed by the mode \"%s\"; not changed", id, mode->key.c_str());
	return true;
}

void EnforceModeRaces()
{
	if (!g_dlg || NetMultiplayerSaveLoading()) return;
	if (!reinterpret_cast<IntThis_t>(addr::kIsHostOrOffline)(g_dlg)) return;
	const GameMode *mode = CurrentMode();
	if (mode && mode->civs.empty()) mode = nullptr;
	const bool sets = !CivRoomSets().empty();
	if (!mode && !sets) return;
	char *choice = static_cast<char *>(g_dlg) + 0xa8;
	for (int i = 0; i < 8; i++) {
		const char *name = nullptr;
		int type = 0, color = 0, ready = 0, race = 0, team = 0xff, extra = 0;
		if (!reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(g_dlg, nullptr, i, &name, &type, &color, &ready, &race, &team, &extra) || (type != 1 && type != 3)) continue;
		// A scenario map fixes the civilization of its slots (EnforceFixedRaces); the others follow the room's sets and mode.
		if (*reinterpret_cast<int *>(static_cast<char *>(g_dlg) + 0x9c) == 0 && g_scenarioRace[i] >= 0) continue;
		// The stored choice only follows the game's own civilizations: for a mod's it stays at 0 while the dropdown holds the real item.
		void *combo = Combo(g_dlg, ((i + 1) << 12) | 0x22);
		const int shownItem = ComboSel(combo);
		const int now = CivsRaceOfRoomItem(shownItem >= 0 ? shownItem : static_cast<unsigned char>(choice[i]));
		if (now < 0 || ((!mode || GameModeAllows(*mode, now)) && (!sets || CivsRoomAllows(now)))) continue;
		int item = -1;
		for (int shown : CivsRoomOrder(10 + static_cast<int>(CivsList().size()))) {
			const int race = CivsRaceOfRoomItem(shown);
			if (race >= 0 && (!mode || GameModeAllows(*mode, race)) && CivsRoomAllows(race)) {
				item = shown;
				break;
			}
		}
		if (item < 0 && mode) item = CivsRoomItemOfRace(GameModeFirstAllowed(*mode, false));
		if (item < 0) continue;
		LogI("room", "slot %d: the room does not allow civilization %d; it is changed to %d", i + 1, now, CivsRaceOfRoomItem(item));
		choice[i] = static_cast<char>(item);
		reinterpret_cast<UpdateRow_t>(addr::kUpdateSlotRow)(g_dlg, nullptr, static_cast<unsigned>(i));
		// The dropdown is what the room shows and sends; changing the stored choice alone does not move it.
		SelectCombo(combo, item);
	}
}

void EnforceFixedRaces()
{
	if (!g_dlg || *reinterpret_cast<int *>(static_cast<char *>(g_dlg) + 0x9c) != 0) return;
	char *choice = static_cast<char *>(g_dlg) + 0xa8;
	for (int i = 0; i < 8; i++) {
		const int want = g_scenarioRace[i];
		if (want < 0 || choice[i] == want) continue;
		static int logged = 0;
		if (logged++ < 12) LogD("room", "slot %d has the map's fixed civilization (%d); setting it again (was %d)", i + 1, want, static_cast<int>(choice[i]));
		choice[i] = static_cast<char>(want);
		reinterpret_cast<UpdateRow_t>(addr::kUpdateSlotRow)(g_dlg, nullptr, static_cast<unsigned>(i));
	}
}

void *__cdecl HookCreateSettings(int *path, int *out, int type, void *xml)
{
	void *result = g_origCreateSettings(path, out, type, xml);
	if (!result && out && *out && g_refreshThread == GetCurrentThreadId()) {
		char *settings = reinterpret_cast<char *>(*out);
		char line[128] = "";
		for (int i = 0; i < 8; i++) {
			int *race = reinterpret_cast<int *>(settings + 0xa4 + i * 0xc0);
			const int id = *race;
			*race = RaceIdToSlotChoice(id);
			g_scenarioRace[i] = *race >= 0 && *race < 8 ? *race : -1;
			sprintf_s(line + strlen(line), sizeof(line) - strlen(line), "%s%d>%d", i ? " " : "", id, *race);
		}
		static int logged = 0;
		if (logged++ < 8) LogD("room", "scenario civilizations per slot (identifier>dropdown): %s", line);
	}
	return result;
}

void __fastcall HookRefreshOptions(void *room)
{
	for (int &fixed : g_scenarioRace) fixed = -1;
	g_refreshThread = GetCurrentThreadId();
	g_origRefreshOptions(room);
	g_refreshThread = 0;
	if (room && *reinterpret_cast<int *>(static_cast<char *>(room) + 0x9c) == 0) {
		const unsigned char *marks = reinterpret_cast<const unsigned char *>(static_cast<char *>(room) + 0x84 + 0x2c);
		const unsigned positions = *reinterpret_cast<unsigned short *>(static_cast<char *>(room) + 0xcc);
		char line[192];
		sprintf_s(line, "positions %u; control per slot %u %u %u %u %u %u %u %u; fixed civilization per slot %d %d %d %d %d %d %d %d", positions,
			marks[0], marks[1], marks[2], marks[3], marks[4], marks[5], marks[6], marks[7], g_scenarioRace[0], g_scenarioRace[1], g_scenarioRace[2],
			g_scenarioRace[3], g_scenarioRace[4], g_scenarioRace[5], g_scenarioRace[6], g_scenarioRace[7]);
		static std::string last;
		if (last != line) {
			last = line;
			LogD("room", "preset map: %s", line);
		}
	}
}

}

void GameRoomInstall(bool knownExe)
{
	if (!knownExe) return;
	const bool ok = HookAt(addr::kMpGameOnShow, reinterpret_cast<void *>(&HookOnShow), reinterpret_cast<void **>(&g_origOnShow)) &&
		HookAt(addr::kMpGameDtor, reinterpret_cast<void *>(&HookDtor), reinterpret_cast<void **>(&g_origDtor)) &&
		HookAt(addr::kPickerOnShow, reinterpret_cast<void *>(&HookPickerOnShow), reinterpret_cast<void **>(&g_origPickerOnShow)) &&
		HookAt(addr::kPickerDtor, reinterpret_cast<void *>(&HookPickerDtor), reinterpret_cast<void **>(&g_origPickerDtor)) &&
		HookAt(addr::kSetSlotInfo, reinterpret_cast<void *>(&HookSetSlotInfo), reinterpret_cast<void **>(&g_origSetSlotInfo)) &&
		HookAt(addr::kApplyToGame, reinterpret_cast<void *>(&HookApplyToGame), reinterpret_cast<void **>(&g_origApplyToGame)) &&
		HookAt(addr::kAskYesNo, reinterpret_cast<void *>(&HookAskYesNo), reinterpret_cast<void **>(&g_origAskYesNo)) &&
		HookAt(addr::kBuildSettings, reinterpret_cast<void *>(&HookBuildSettings), reinterpret_cast<void **>(&g_origBuildSettings)) &&
		HookAt(addr::kEnvGetInt, reinterpret_cast<void *>(&HookEnvGetInt), reinterpret_cast<void **>(&g_origEnvGetInt)) &&
		HookAt(addr::kDiplomacyExecute, reinterpret_cast<void *>(&HookDiplomacyExecute), reinterpret_cast<void **>(&g_origDiplomacyExecute)) &&
		HookAt(addr::kDiplomacyApply, reinterpret_cast<void *>(&HookDiplomacyApply), reinterpret_cast<void **>(&g_origDiplomacyApply)) &&
		HookAt(addr::kCreateSettings, reinterpret_cast<void *>(&HookCreateSettings), reinterpret_cast<void **>(&g_origCreateSettings)) &&
		HookAt(addr::kRefreshOptions, reinterpret_cast<void *>(&HookRefreshOptions), reinterpret_cast<void **>(&g_origRefreshOptions));
	if (ok) HookAt(addr::kCreateWorldCore, reinterpret_cast<void *>(&HookCreateWorldCore), reinterpret_cast<void **>(&g_origCreateWorldCore));
	LogAt(ok ? kLogInfo : kLogError, "room", "interface: %s", ok ? "connected to the mod's interface" : "could NOT hook");
}

int g_typeTarget[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
int g_typeTries[8] = {};
int g_typeClicked[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
DWORD g_typeClickTime[8] = {};

int SlotType(int slot)
{
	const char *name = nullptr;
	int type = 0, color = 0, ready = 0, race = 0, team = 0xff, extra = 0;
	if (!reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(g_dlg, nullptr, slot, &name, &type, &color, &ready, &race, &team, &extra)) {
		return 0;
	}
	return type;
}

void StepSlotTypes()
{
	for (int i = 0; i < 8; i++) {
		if (g_typeTarget[i] < 0) continue;
		if (SlotHandshaking(i)) {
			LogD("room", "slot %d was taken by a joining player; the type change is left", i + 1);
			g_typeTarget[i] = -1;
			continue;
		}
		const int type = SlotType(i);
		if (g_typeClicked[i] >= 0 && type == g_typeClicked[i] && GetTickCount() - g_typeClickTime[i] < 1500) continue;
		g_typeClicked[i] = -1;
		if (type == g_typeTarget[i] || type == 3 || type == 4 || g_typeTries[i] >= 4) {
			if (type != g_typeTarget[i]) LogD("room", "slot %d stays at type %d (%d was requested)", i + 1, type, g_typeTarget[i]);
			g_typeTarget[i] = -1;
			continue;
		}
		g_typeTries[i]++;
		g_typeClicked[i] = type;
		g_typeClickTime[i] = GetTickCount();
		LogD("room", "slot %d, type %d -> clicking (%d requested)", i + 1, type, g_typeTarget[i]);
		Click(Child(g_dlg, ((i + 1) << 12) | 0x12, addr::kClassNameCUIImgButton));
	}
}

void GameRoomTick()
{
	for (;;) {
		std::function<void()> fn;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (g_actions.empty()) break;
			fn = std::move(g_actions.front());
			g_actions.pop_front();
		}
		if (g_dlg) fn();
	}
	if (g_dlg) {
		StepSlotTypes();
		EnforceFixedRaces();
		EnforceModeRaces();
	}
	else {
		for (int &t : g_typeTarget) t = -1;
	}
	TakeSnapshot();
	bool busy = false;
	for (int t : g_typeTarget) busy = busy || t >= 0;
	std::lock_guard<std::mutex> lock(g_lock);
	g_snap.busy = busy || !g_actions.empty();
}

GameRoomSnapshot GameRoomGet()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_snap;
}

bool GameRoomActive()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_snap.active;
}

void GameRoomSetRace(int slot, int index)
{
	Queue([slot, index] { SelectCombo(Combo(g_dlg, ((slot + 1) << 12) | 0x22), index); });
}

void GameRoomSetSlotItem(int slot, int index)
{
	Queue([slot, index] {
		if (SlotType(slot) == 1) SelectCombo(Combo(g_dlg, ((slot + 1) << 12) | 0x02), index);
	});
}

void GameRoomSetSlotType(int slot, int type)
{
	Queue([slot, type] {
		if (g_typeTarget[slot & 7] >= 0) return;
		if (SlotHandshaking(slot & 7)) {
			LogW("room", "slot %d is being taken by a joining player; not changed", (slot & 7) + 1);
			return;
		}
		g_typeTarget[slot & 7] = type;
		g_typeTries[slot & 7] = 0;
		g_typeClicked[slot & 7] = -1;
	});
}

void GameRoomCycleTeam(int slot)
{
	Queue([slot] { Click(Button(g_dlg, ((slot + 1) << 12) | 0x32)); });
}

void GameRoomSetBonus(int slot, int index)
{
	Queue([slot, index] {
		typedef void(__fastcall * SendPlayerInfo_t)(void *, void *, int, unsigned, int);
		typedef void(__fastcall * UpdateSlotRow_t)(void *, void *, unsigned);
		if (!g_dlg || slot < 0 || slot > 7 || LockedByMode(0x10a7)) return;
		if (index < 0 || index >= static_cast<int>(RoomBonuses(g_dlg).size())) return;
		char *d = static_cast<char *>(g_dlg);
		const int player = static_cast<int>(*reinterpret_cast<signed char *>(d + 0xa0 + slot));
		char *shell = *reinterpret_cast<char **>(d + 0x8228);
		if (!shell) {
			*reinterpret_cast<int *>(player * 0x34 + *reinterpret_cast<char **>(d + 0x174) + 0x30) = index;
		}
		else {
			char *rec = *reinterpret_cast<char **>(shell + 8) + player * 0x21;
			*reinterpret_cast<int *>(rec + 8) = 1;
			*reinterpret_cast<int *>(*reinterpret_cast<char **>(rec) + 0x6c) = index;
			reinterpret_cast<SendPlayerInfo_t>(addr::kSendPlayerInfo)(shell, nullptr, player, 0xffff, 0);
		}
		reinterpret_cast<ThisOnly_t>(addr::kUpdatePlayerCounts)(g_dlg);
		reinterpret_cast<UpdateSlotRow_t>(addr::kUpdateSlotRow)(g_dlg, nullptr, static_cast<unsigned>(slot));
	});
}

void GameRoomToggleReady()
{
	Queue([] { Click(Button(g_dlg, 0x9008)); });
}

void GameRoomStart()
{
	Queue([] { Click(Button(g_dlg, 0x9006)); });
}

void GameRoomCancel()
{
	Queue([] { Click(Button(g_dlg, 0x9007)); });
}

void GameRoomRefreshChoices()
{
	Queue([] {
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		if (!opts) return;
		static const int kIds[] = {0x1000, 0x1002};
		for (int id : kIds) {
			void *combo = Combo(opts, id);
			if (!combo) continue;
			const int sel = ComboSel(combo);
			const int count = static_cast<int>(ComboItems(combo).size());
			if (count <= 0) continue;
			int use = sel >= 0 && sel < count ? sel : 0;
			if (id == 0x1000 && use == 0) continue;
			if (id == 0x1002 && use == 0 && !NetMultiplayerSaveLoading()) {
				const int alt = AlternativeItem(ComboItems(combo));
				if (alt > 0) use = alt;
			}
			LogD("room", "option 0x%x (%d of %d) is chosen again so it applies at start", id, use, count);
			SelectCombo(combo, use);
		}
	});
}

void GameRoomSetOption(int id, int index)
{
	Queue([id, index] {
		if (LockedByMode(id)) return;
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		void *combo = Combo(opts, id);
		const std::vector<std::string> items = ComboItems(combo);
		const std::vector<int> order = DisplayOrder(id, items);
		SelectCombo(combo, index >= 0 && index < static_cast<int>(order.size()) ? order[index] : index);
	});
}

void GameRoomSetSiteFlags(unsigned bits)
{
	Queue([bits] {
		g_siteFlags = bits & kSiteMask;
		SaveRoomFlags("Sites", g_siteFlags);
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		typedef void(__fastcall *OnApply_t)(void *opts);
		if (opts) reinterpret_cast<OnApply_t>(addr::kOnApplyOptions)(opts);
		LogD("room", "neutral sites removed 0x%x", g_siteFlags);
	});
}

void GameRoomSetRules(unsigned bits)
{
	Queue([bits] {
		g_ruleFlags = bits & kRuleMask;
		SaveRoomFlags("Rules", g_ruleFlags);
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		typedef void(__fastcall *OnApply_t)(void *opts);
		if (opts) reinterpret_cast<OnApply_t>(addr::kOnApplyOptions)(opts);
		LogI("room", "rules 0x%x", g_ruleFlags);
	});
}


unsigned GameRuleMask() { return kRuleBaseMask | ModOptionsMask(); }

void GameRulesOnGameStart()
{
	CrashLogLoadingBegin();
	g_gameRunning = true;
	const char *source = "none";
	unsigned rules = 0;
	unsigned savedRules = 0;
	unsigned long savedTick = 0;
	const bool haveSaved = ModRulesFromSaveTake(savedRules, savedTick) && static_cast<int>(GetTickCount() - savedTick) < 120000;
	if (haveSaved && (!g_pendingRulesTick || static_cast<int>(savedTick - g_pendingRulesTick) > 0)) {
		rules = savedRules & kRuleMask;
		source = "the saved game";
	}
	else if (g_pendingRulesTick && static_cast<int>(GetTickCount() - g_pendingRulesTick) < 120000) {
		rules = g_pendingRules;
		source = "room";
	}
	else if (NetRejoinLoading()) {
		rules = NetRoomRules() & kRuleMask;
		source = "Steam lobby (rejoining the game)";
	}
	g_pendingRulesTick = 0;
	ModRulesSetActive(rules);
	LogI("room", "game rules: 0x%x (%s)", rules, source);
}

void GameRulesOnGameEnd()
{
	CrashLogLoadingEnd();
	g_gameRunning = false;
	ModRulesSetActive(0);
	if (!g_pendingRulesTick || static_cast<int>(GetTickCount() - g_pendingRulesTick) > 5000) g_pendingRulesTick = 0;
}

void GameRoomToggleOption(int id)
{
	Queue([id] {
		if (LockedByMode(id)) return;
		void *opts = *reinterpret_cast<void **>(addr::kGameOptionsDialog);
		ToggleCheck(Button(opts, id));
	});
}

static void ApplyColor(int slot, int start, int dir, bool relative)
{
	Queue([slot, start, dir, relative] {
		typedef unsigned(__fastcall * FindFreeColor_t)(void *, void *, unsigned, int);
		typedef void(__fastcall * SendPlayerInfo_t)(void *, void *, int, unsigned, int);
		typedef void(__fastcall * UpdateSlotRow_t)(void *, void *, unsigned);
		char *d = static_cast<char *>(g_dlg);
		const int player = static_cast<int>(*reinterpret_cast<signed char *>(d + 0xa0 + slot));
		char *shell = *reinterpret_cast<char **>(d + 0x8228);
		unsigned color = 0;
		if (!shell) {
			color = *reinterpret_cast<unsigned *>(player * 0x34 + *reinterpret_cast<char **>(d + 0x174) + 0x20);
		}
		else {
			char *rec = *reinterpret_cast<char **>(shell + 8) + player * 0x21;
			if (!*reinterpret_cast<int *>(rec + 4)) return;
			color = *reinterpret_cast<unsigned *>(*reinterpret_cast<char **>(rec) + 0x60);
		}
		const unsigned from = relative ? (color + 12 + dir) % 12 : static_cast<unsigned>(start) % 12;
		if (!relative && from == color) return;
		color = reinterpret_cast<FindFreeColor_t>(addr::kFindFreeColor)(g_dlg, nullptr, from, dir);
		if (!shell) {
			*reinterpret_cast<unsigned *>(player * 0x34 + *reinterpret_cast<char **>(d + 0x174) + 0x20) = color;
		}
		else {
			char *rec = *reinterpret_cast<char **>(shell + 8) + player * 0x21;
			*reinterpret_cast<int *>(rec + 8) = 1;
			*reinterpret_cast<unsigned *>(*reinterpret_cast<char **>(rec) + 0x60) = color;
			reinterpret_cast<SendPlayerInfo_t>(addr::kSendPlayerInfo)(shell, nullptr, player, 0xffff, 0);
		}
		reinterpret_cast<ThisOnly_t>(addr::kUpdatePlayerCounts)(g_dlg);
		reinterpret_cast<UpdateSlotRow_t>(addr::kUpdateSlotRow)(g_dlg, nullptr, static_cast<unsigned>(slot));
	});
}

void GameRoomCycleColor(int slot, int dir)
{
	ApplyColor(slot, 0, dir, true);
}

void GameRoomSwapSlots(int a, int b)
{
	typedef void(__fastcall * SwapSlots_t)(void *dlg, void *edx, unsigned a, unsigned b);
	Queue([a, b] {
		if (a < 0 || a >= 8 || b < 0 || b >= 8 || a == b) return;
		if (SlotHandshaking(a) || SlotHandshaking(b)) {
			LogW("room", "slots %d and %d are not swapped: one is being taken by a joining player", a + 1, b + 1);
			return;
		}
		reinterpret_cast<SwapSlots_t>(addr::kSwapSlots)(g_dlg, nullptr, a, b);
	});
}

void GameRoomSetColor(int slot, int color)
{
	ApplyColor(slot, color, 1, false);
}

namespace {
std::string g_savedProfileName;
bool g_profileNameSwapped = false;
}

static void SetProfileName(const std::string &name)
{
	typedef void *(__fastcall * Assign_t)(void *, void *, const char *, unsigned);
	void *profile = *reinterpret_cast<void **>(addr::kPlayerProfile);
	if (!profile) return;
	reinterpret_cast<Assign_t>(addr::kStringAssign)(profile, nullptr, name.c_str(), static_cast<unsigned>(name.size()));
}

static std::string ProfileName()
{
	const char *p = *reinterpret_cast<const char **>(addr::kPlayerProfile);
	if (!p) return "";
	const unsigned size = *reinterpret_cast<const unsigned *>(p + 0x14);
	const unsigned cap = *reinterpret_cast<const unsigned *>(p + 0x18);
	const char *text = cap < 0x10 ? p + 4 : *reinterpret_cast<const char *const *>(p + 4);
	return text && size < 256 ? std::string(text, size) : std::string();
}

void GameRoomUseOnlineName(const std::string &name)
{
	if (name.empty() || !*reinterpret_cast<void **>(addr::kPlayerProfile)) return;
	if (!g_profileNameSwapped) {
		g_savedProfileName = ProfileName();
		g_profileNameSwapped = true;
	}
	SetProfileName(name);
	LogD("room", "player name \"%s\" (Steam) instead of \"%s\"", name.c_str(), g_savedProfileName.c_str());
}

void GameRoomRestoreProfileName()
{
	if (!g_profileNameSwapped) return;
	g_profileNameSwapped = false;
	SetProfileName(g_savedProfileName);
	LogD("room", "profile name restored (\"%s\")", g_savedProfileName.c_str());
}

bool GameRoomOnlineNameActive()
{
	return g_profileNameSwapped;
}

void GameRoomPickerSelect(int index)
{
	Queue([index] {
		void *list = g_picker ? Child(g_picker, 0xa105, addr::kClassNameCUISList) : nullptr;
		if (list) reinterpret_cast<SelectExclusive_t>(addr::kListSelectExclusive)(list, nullptr, static_cast<unsigned>(index), 1, 1);
	});
}

void GameRoomPickerAccept()
{
	Queue([] {
		if (g_picker) Click(Child(g_picker, 0xa001, addr::kClassNameCUIButton));
	});
}

void GameRoomPickerCancel()
{
	Queue([] {
		if (g_picker) Click(Child(g_picker, 0xa002, addr::kClassNameCUIButton));
	});
}

namespace {
void RemoveHumanFromSlot(int slot)
{
	void *shell = *reinterpret_cast<void **>(static_cast<char *>(g_dlg) + 0x8228);
	if (!shell) return;
	const int id = *(static_cast<char *>(g_dlg) + 0xa0 + slot);
	reinterpret_cast<void *(__fastcall *)(void *, void *, unsigned)>(addr::kShellRemovePlayer)(shell, nullptr, static_cast<unsigned>(id));
	reinterpret_cast<int(__cdecl *)(void *, int, int, int)>(addr::kNetUiCallback)(shell, 10, id, id);
}
}

void GameRoomKick(int slot)
{
	Queue([slot] {
		if (slot < 0 || slot > 7 || SlotType(slot) != 3) return;
		LogD("room", "kicking the player in slot %d", slot + 1);
		RemoveHumanFromSlot(slot);
	});
}

void GameRoomPlayerLeft(const std::string &name)
{
	Queue([name] {
		const int local = reinterpret_cast<IntThis_t>(addr::kGetLocalSlot)(g_dlg);
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < 8; i++) {
				const char *slotName = nullptr;
				int type = 0, color = 0, ready = 0, race = 0, team = 0xff, extra = 0;
				if (!reinterpret_cast<GetSlotInfo_t>(addr::kGetSlotInfo)(g_dlg, nullptr, i, &slotName, &type, &color, &ready, &race, &team, &extra) ||
					type != 3 || i == local || !slotName) {
					continue;
				}
				if (!(pass == 0 ? SamePlayerName(slotName, name) : PlayerNameMatches(slotName, name))) continue;
				LogD("room", "%s is no longer in the Steam lobby; freeing slot %d", name.c_str(), i + 1);
				RemoveHumanFromSlot(i);
				return;
			}
		}
	});
}
