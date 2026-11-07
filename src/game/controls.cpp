// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "game/controls.h"
#include "core/legacy.h"
#include "i18n/tr.h"
#include "core/log.h"
#include "mods/mods.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <string>
#include <vector>
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/ui.h"
#include "addr/world.h"

void ComputeBarKeys();

namespace {
const unsigned kDescName = 0x08;
const unsigned kDescCtrl = 0x7c;
const unsigned kDescTitle = 0x80;
const unsigned kDescKey = 0x1c4;
const char kHotkeyIcon[] = "<imagetransp gameres/infobar/common/hotkey.bmp>";

enum : unsigned char { kCtrl = 1, kShift = 2, kAlt = 4 };

using KeySpec = ControlKey;

enum Kind : unsigned char {
	kGeneral,
	kCamera,
	kInfobar,
	kNotif,
	kMod,
};

struct Action {
	const char *id;
	int page;
	const char *name;
	const char *hint;
	unsigned short defVk;
	unsigned char defMods;
	Kind kind;
	KeyRule rule;
};

const char kGroupHint[] = "Ctrl creates; Shift adds";

const Action kActions[] = {
	{"camera_left", kPageCamera, "Move camera left", "", VK_LEFT, 0, kCamera, kRulePlain},
	{"camera_up", kPageCamera, "Move camera up", "", VK_UP, 0, kCamera, kRulePlain},
	{"camera_right", kPageCamera, "Move camera right", "", VK_RIGHT, 0, kCamera, kRulePlain},
	{"camera_down", kPageCamera, "Move camera down", "", VK_DOWN, 0, kCamera, kRulePlain},
	{"center", kPageCamera, "Center on selection", "", VK_HOME, 0, kGeneral, kRuleAny},
	{"last_alert", kPageCamera, "Go to last alert", "", 'Q', 0, kNotif, kRuleAny},

	{"cancel", kPageSelection, "Cancel", "the command or the selection; if there is none, opens the menu", VK_ESCAPE, 0, kGeneral, kRuleAny},
	{"next_hero", kPageSelection, "Next hero", "with Shift, the previous one", VK_OEM_COMMA, 0, kGeneral, kRulePlain},
	{"hero_group", kPageSelection, "Select the hero's group", "", VK_F7, 0, kGeneral, kRuleAny},
	{"keep_healthiest", kPageSelection, "Keep the healthiest", "half; with Ctrl, those above 2/3 health", VK_PRIOR, 0, kGeneral, kRulePlain},
	{"keep_wounded", kPageSelection, "Keep the most wounded", "half", VK_NEXT, 0, kGeneral, kRulePlain},
	{"keep_highest_level", kPageSelection, "Keep the highest level", "half; with Ctrl, only max-level ones", VK_INSERT, 0, kGeneral, kRulePlain},
	{"keep_lowest_level", kPageSelection, "Keep the lowest level", "half", VK_DELETE, 0, kGeneral, kRulePlain},

	{"group_1", kPageGroups, "Group 1", kGroupHint, '1', 0, kGeneral, kRulePlain},
	{"group_2", kPageGroups, "Group 2", kGroupHint, '2', 0, kGeneral, kRulePlain},
	{"group_3", kPageGroups, "Group 3", kGroupHint, '3', 0, kGeneral, kRulePlain},
	{"group_4", kPageGroups, "Group 4", kGroupHint, '4', 0, kGeneral, kRulePlain},
	{"group_5", kPageGroups, "Group 5", kGroupHint, '5', 0, kGeneral, kRulePlain},
	{"group_6", kPageGroups, "Group 6", kGroupHint, '6', 0, kGeneral, kRulePlain},
	{"group_7", kPageGroups, "Group 7", kGroupHint, '7', 0, kGeneral, kRulePlain},
	{"group_8", kPageGroups, "Group 8", kGroupHint, '8', 0, kGeneral, kRulePlain},
	{"group_9", kPageGroups, "Group 9", kGroupHint, '9', 0, kGeneral, kRulePlain},
	{"group_0", kPageGroups, "Group 0", kGroupHint, '0', 0, kGeneral, kRulePlain},

	{"menu", kPageGame, "Game menu", "", VK_F10, 0, kGeneral, kRuleAny},
	{"help", kPageGame, "Help", "", VK_F1, 0, kGeneral, kRuleAny},
	{"save", kPageGame, "Save the game", "", VK_F2, 0, kGeneral, kRuleAny},
	{"load", kPageGame, "Load a game", "", VK_F3, 0, kGeneral, kRuleAny},
	{"quick_save", kPageGame, "Quick save", "", VK_F6, 0, kGeneral, kRuleAny},
	{"quick_load", kPageGame, "Quick load", "", VK_F9, 0, kGeneral, kRuleAny},
	{"diplomacy", kPageGame, "Diplomacy", "", VK_F5, 0, kGeneral, kRuleAny},
	{"notes", kPageGame, "Notes", "", VK_F8, 0, kGeneral, kRuleAny},
	{"chat", kPageGame, "Type in chat", "", VK_RETURN, 0, kGeneral, kRuleAny},
	{"screenshot", kPageGame, "Screenshot", "saved in the game folder", VK_SNAPSHOT, 0, kGeneral, kRuleAny},

	{"scoreboard", kPageInterface, "Change scoreboard", "", VK_F4, 0, kGeneral, kRuleAny},
	{"next_scoreboard", kPageInterface, "Next scoreboard", "with Shift, the previous one", VK_OEM_2, 0, kGeneral, kRulePlain},
	{"compact_scoreboard", kPageInterface, "Compact scoreboard", "", VK_OEM_PERIOD, 0, kGeneral, kRuleAny},
	{"health_bars", kPageInterface, "Health bars", "with Ctrl, changes which ones are shown", VK_OEM_3, 0, kGeneral, kRulePlain},
	{"see_through", kPageInterface, "See through buildings and trees", "while held down", VK_OEM_5, 0, kGeneral, kRulePlain},
	{"info_panel", kPageInterface, "Switch the info panel tab", "", 'I', 0, kInfobar, kRuleAny},

	{"minimap", kPageInterface, "Minimap", "a tap opens or closes it; held down, only while you press it", VK_SPACE, 0, kGeneral, kRuleAny},

	{"performance_panel", kPageMod, "Performance panel", "show or hide; works everywhere", 'P', kCtrl, kMod, kRuleShortcut},
	{"replay_pause", kPageMod, "Replay: pause", "pause or resume while watching a replay or a broadcast", VK_SPACE, kCtrl, kMod, kRuleShortcut},
	{"replay_faster", kPageMod, "Replay: faster", "x1, x2, x4 and x8", VK_UP, kCtrl, kMod, kRuleShortcut},
	{"replay_slower", kPageMod, "Replay: slower", "", VK_DOWN, kCtrl, kMod, kRuleShortcut},
	{"replay_back", kPageMod, "Replay: back 10 seconds", "", VK_LEFT, kCtrl, kMod, kRuleShortcut},
	{"replay_forward", kPageMod, "Replay: forward 10 seconds", "", VK_RIGHT, kCtrl, kMod, kRuleShortcut},
	{"replay_live", kPageMod, "Broadcast: jump to live", "", VK_END, kCtrl, kMod, kRuleShortcut},
	{"replay_hide_ui", kPageMod, "Replay: hide the game interface", "to watch the game without bars", 'H', kCtrl, kMod, kRuleShortcut},
	{"replay_collapse_bar", kPageMod, "Replay: collapse the control bar", "", 'B', kCtrl, kMod, kRuleShortcut},
	{"cheats_panel", kPageMod, "Cheats: open or close the panel", "only if the game allows cheats", 'T', kCtrl, kMod, kRuleShortcut},
};
const int kActionCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));
const int kFirstModAction = kActionCount - 10;

const char *kPageNames[kPageCount] = {"Camera", "Selection", "Groups", "Game", "Interface", "Resurrected", "Units", "Hero", "Druid",
	"Buildings", "Content"};

struct OrderFile {
	const char *file;
	int page;
	const char *label;
};
const OrderFile kOrderFiles[] = {
	{"UNIT", kPageUnits, "Units"}, {"CATAPULT", kPageUnits, "Catapult"}, {"WAGON", kPageUnits, "Wagon"},
	{"SHIP", kPageUnits, "Ship"}, {"SENTRY", kPageUnits, "Sentry"}, {"EAGLE", kPageUnits, "Eagle"},
	{"HERO", kPageHero, "Hero"}, {"DRUID", kPageDruid, "Druid"},
	{"TOWNHALL", kPageBuildings, "Town Hall"}, {"VILLAGE", kPageBuildings, "Village"}, {"TAVERN", kPageBuildings, "Tavern"},
	{"INN", kPageBuildings, "Inn"}, {"ARENA", kPageBuildings, "Arena"}, {"BLACKSMITH", kPageBuildings, "Blacksmith"},
	{"BARRACKS", kPageBuildings, "Barracks"}, {"RBARRACKS", kPageBuildings, "Roman Barracks"},
	{"MBARRACKS", kPageBuildings, "Roman Barracks (M)"}, {"GBARRACKS", kPageBuildings, "Gallic Barracks"},
	{"IBARRACKS", kPageBuildings, "Iberian Barracks"}, {"BBARRACKS", kPageBuildings, "British Barracks"},
	{"CBARRACKS", kPageBuildings, "Carthaginian Barracks"}, {"EBARRACKS", kPageBuildings, "Egyptian Barracks"},
	{"TBARRACKS", kPageBuildings, "German Barracks"}, {"TTENT", kPageBuildings, "Teuton Tent"},
	{"TEMPLE", kPageBuildings, "Temple"}, {"CTEMPLE", kPageBuildings, "Carthaginian Temple"},
	{"ETEMPLEOFOSIRIS", kPageBuildings, "Temple of Osiris"}, {"BTEMPLEOFTHOR", kPageBuildings, "Temple of Thor"},
	{"TTEMPLEOFNEPTUS", kPageBuildings, "Temple of Neptus"}, {"SANCTUARY", kPageBuildings, "Sanctuary"},
	{"DRUID HOUSE", kPageBuildings, "Druid House"}, {"SHIPYARD", kPageBuildings, "Shipyard"},
	{"GATE", kPageBuildings, "Gate"}, {"TOWER", kPageBuildings, "Tower"}, {"OUTPOST", kPageBuildings, "Outpost"},
	{"OUTPOST_COMMERCIAL", kPageBuildings, "Trade Outpost"},
};

KeySpec g_bind[kActionCount];
KeySpec g_bind2[kActionCount];

void NormalizePair(KeySpec &a, KeySpec &b)
{
	if (!a.vk && b.vk) {
		a = b;
		b = KeySpec();
	}
	if (b == a) b = KeySpec();
}

struct OrderOverride {
	std::string name;
	KeySpec key, key2;
};
std::vector<OrderOverride> g_orderOverrides;

struct Order {
	uintptr_t desc;
	std::string name, key, title;
	short ctrl;
	size_t index;
	std::string file;
	std::string display;
};
struct FileRange {
	std::string file;
	size_t from, to;
};
std::vector<FileRange> g_fileRanges;
uintptr_t g_loadingRegistry = 0;
std::vector<Order> g_orders;

bool g_gridMode = false;
bool g_barLetters = true;
int g_zoomBoxMode = kZoomBoxCtrl;
const char kGridKeys[] = "QWERTYUIOPASDFGHJKLZXCVBNM";

FILETIME g_fileTime = {};
bool g_fileSeen = false;

struct KeyName {
	unsigned short vk;
	const char *name;
};
const KeyName kKeyNames[] = {
	{VK_ESCAPE, "Esc"}, {VK_RETURN, "Enter"}, {VK_SPACE, "Space"}, {VK_TAB, "Tab"}, {VK_BACK, "Backspace"},
	{VK_HOME, "Home"}, {VK_END, "End"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"}, {VK_INSERT, "Insert"},
	{VK_DELETE, "Del"}, {VK_SNAPSHOT, "PrintScreen"}, {VK_PAUSE, "Pause"}, {VK_LEFT, "Left"}, {VK_UP, "Up"},
	{VK_RIGHT, "Right"}, {VK_DOWN, "Down"}, {VK_MULTIPLY, "Num*"}, {VK_ADD, "Num+"}, {VK_SUBTRACT, "Num-"},
	{VK_DECIMAL, "Num."}, {VK_DIVIDE, "Num/"}, {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_MINUS, "Minus"},
	{VK_OEM_PLUS, "Plus"}, {VK_OEM_1, "OEM1"}, {VK_OEM_2, "OEM2"}, {VK_OEM_3, "OEM3"}, {VK_OEM_4, "OEM4"},
	{VK_OEM_5, "OEM5"}, {VK_OEM_6, "OEM6"}, {VK_OEM_7, "OEM7"}, {VK_OEM_102, "OEM102"},
	{VK_MBUTTON, "MiddleMouse"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
};

bool IsMouseVk(unsigned short vk)
{
	return vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

unsigned short ParseKeyName(const std::string &s)
{
	if (s.size() == 1) {
		const char c = static_cast<char>(toupper(static_cast<unsigned char>(s[0])));
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<unsigned short>(c);
	}
	if ((s[0] == 'F' || s[0] == 'f') && s.size() <= 3) {
		const int n = atoi(s.c_str() + 1);
		if (n >= 1 && n <= 24) return static_cast<unsigned short>(VK_F1 + n - 1);
	}
	if (s.size() == 4 && _strnicmp(s.c_str(), "Num", 3) == 0 && s[3] >= '0' && s[3] <= '9') {
		return static_cast<unsigned short>(VK_NUMPAD0 + s[3] - '0');
	}
	for (const KeyName &k : kKeyNames) {
		if (_stricmp(k.name, s.c_str()) == 0) return k.vk;
	}
	if (const unsigned short legacy = LegacyKeyVk(s)) return legacy;
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		const unsigned long v = strtoul(s.c_str() + 2, nullptr, 16);
		if (v > 0 && v < 0xff) return static_cast<unsigned short>(v);
	}
	return 0;
}

KeySpec ParseSpec(const char *text, bool &ok)
{
	KeySpec k;
	ok = true;
	std::string s(text);
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
	size_t start = 0;
	while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
	s = s.substr(start);
	if (s.empty()) return k;
	std::vector<std::string> parts;
	size_t from = 0;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '+' && i > from && i + 1 < s.size()) {
			parts.push_back(s.substr(from, i - from));
			from = i + 1;
		}
	}
	parts.push_back(s.substr(from));
	for (size_t i = 0; i + 1 < parts.size(); i++) {
		const char *m = parts[i].c_str();
		if (_stricmp(m, "Ctrl") == 0 || _stricmp(m, "Control") == 0) k.mods |= kCtrl;
		else if (LegacyIsShiftName(m) || _stricmp(m, "Shift") == 0) k.mods |= kShift;
		else if (_stricmp(m, "Alt") == 0) k.mods |= kAlt;
		else ok = false;
	}
	k.vk = ParseKeyName(parts.back());
	if (!k.vk) ok = false;
	if (!ok) k = KeySpec();
	return k;
}

void ParseSpecPair(const char *text, KeySpec &a, KeySpec &b, bool &okA, bool &okB)
{
	const std::string t(text);
	const size_t comma = t.find(',');
	a = ParseSpec(t.substr(0, comma).c_str(), okA);
	okB = true;
	b = KeySpec();
	if (comma != std::string::npos) b = ParseSpec(t.substr(comma + 1).c_str(), okB);
}

unsigned char CurrentMods()
{
	unsigned char m = 0;
	if (GetKeyState(VK_CONTROL) & 0x8000) m |= kCtrl;
	if (GetKeyState(VK_SHIFT) & 0x8000) m |= kShift;
	if (GetKeyState(VK_MENU) & 0x8000) m |= kAlt;
	return m;
}

char OrderChar(unsigned short vk)
{
	if (vk >= 'A' && vk <= 'Z') return static_cast<char>(vk - 'A' + 'a');
	switch (vk) {
	case VK_OEM_1: return ';';
	case VK_OEM_PLUS: return '=';
	case VK_OEM_COMMA: return ',';
	case VK_OEM_MINUS: return '-';
	case VK_OEM_PERIOD: return '.';
	case VK_OEM_2: return '/';
	case VK_OEM_3: return '`';
	case VK_OEM_4: return '[';
	case VK_OEM_5: return '\\';
	case VK_OEM_6: return ']';
	case VK_OEM_7: return '\'';
	}
	return 0;
}

std::atomic<HKL> g_layout{nullptr};

HKL Layout()
{
	const HKL l = g_layout.load();
	return l ? l : GetKeyboardLayout(0);
}

std::string KeyLabel(unsigned short vk)
{
	if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
	const UINT ch = MapVirtualKeyExW(vk, MAPVK_VK_TO_CHAR, Layout()) & 0x7fffffff;
	if (ch) {
		wchar_t w[2] = {static_cast<wchar_t>(towupper(static_cast<wint_t>(ch))), 0};
		char out[8] = {};
		WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof(out), nullptr, nullptr);
		return out;
	}
	return "?";
}

std::string ControlsPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "controls.ini");
	return path;
}

bool RawString(uintptr_t obj, char *buf, unsigned bufSize, unsigned &len)
{
	__try {
		const unsigned size = *reinterpret_cast<unsigned *>(obj + 0x14);
		const unsigned cap = *reinterpret_cast<unsigned *>(obj + 0x18);
		if (size > cap || size >= bufSize) return false;
		const char *p = cap < 16 ? reinterpret_cast<const char *>(obj + 4) : *reinterpret_cast<const char **>(obj + 4);
		memcpy(buf, p, size);
		buf[size] = 0;
		len = size;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool ReadString(uintptr_t obj, std::string &out)
{
	char buf[1024];
	unsigned len = 0;
	if (!RawString(obj, buf, sizeof(buf), len)) return false;
	out.assign(buf, len);
	return true;
}

void WriteString(uintptr_t obj, const std::string &s)
{
	typedef void(__fastcall * Assign_t)(void *self, void *edx, const char *s, unsigned n);
	reinterpret_cast<Assign_t>(addr::kStringAssign)(reinterpret_cast<void *>(obj), nullptr, s.c_str(), static_cast<unsigned>(s.size()));
}

bool ReadDescList(uintptr_t registry, uintptr_t &begin, uintptr_t &end)
{
	__try {
		begin = *reinterpret_cast<uintptr_t *>(registry - 0x10);
		end = *reinterpret_cast<uintptr_t *>(registry - 0x0c);
		return begin && end >= begin && (end - begin) / 4 < 10000;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

std::string TitleText(const std::string &title)
{
	std::string out;
	bool tag = false;
	for (size_t i = 0; i < title.size(); i++) {
		const char c = title[i];
		if (c == '<') {
			if (!out.empty()) break;
			tag = true;
		}
		else if (c == '>') tag = false;
		else if (!tag) out += c;
	}
	while (!out.empty() && out.back() == ' ') out.pop_back();
	while (!out.empty() && out[0] == ' ') out.erase(0, 1);
	return out;
}

void CaptureOrders(uintptr_t registry)
{
	g_orders.clear();
	uintptr_t begin = 0, end = 0;
	if (!ReadDescList(registry, begin, end)) {
		LogW("controls", "could not read the command list");
		return;
	}
	for (uintptr_t p = begin; p < end; p += 4) {
		const uintptr_t desc = *reinterpret_cast<uintptr_t *>(p);
		if (!desc) continue;
		Order o;
		o.desc = desc;
		if (!ReadString(desc + kDescName, o.name) || !ReadString(desc + kDescKey, o.key) || !ReadString(desc + kDescTitle, o.title)) {
			continue;
		}
		o.ctrl = *reinterpret_cast<short *>(desc + kDescCtrl);
		const size_t index = (p - begin) / 4;
		o.index = index;
		for (const FileRange &r : g_fileRanges) {
			if (index >= r.from && index < r.to) o.file = r.file;
		}
		o.display = TitleText(o.title);
		g_orders.push_back(o);
	}
	int withKey = 0;
	for (const Order &o : g_orders) {
		if (!o.key.empty()) withKey++;
	}
	LogI("controls", "%d commands loaded (%d with a key)", static_cast<int>(g_orders.size()), withKey);
}

std::string TitleWithKey(const std::string &title, const std::string &label)
{
	std::string base = title;
	const size_t icon = base.find(kHotkeyIcon);
	if (icon != std::string::npos) {
		base.resize(icon);
		while (!base.empty() && base.back() == ' ') base.pop_back();
	}
	if (label.empty()) return base;
	return base + "  " + kHotkeyIcon + " <color 255 255 255>" + label;
}

void ApplyOrders()
{
	int changed = 0;
	for (const Order &o : g_orders) {
		const OrderOverride *ov = nullptr;
		for (const OrderOverride &x : g_orderOverrides) {
			if (_stricmp(x.name.c_str(), o.name.c_str()) == 0) {
				ov = &x;
				break;
			}
		}
		std::string key = o.key, title = o.title;
		short ctrl = o.ctrl;
		if (ov) {
			const char ch = OrderChar(ov->key.vk), ch2 = OrderChar(ov->key2.vk);
			key.clear();
			if (ch) key += ch;
			if (ch2 && ch) key += ch2;
			std::string label;
			if (ch) {
				const std::string ctrlText = (ov->key.mods & kCtrl) ? "Ctrl+" : "";
				label = ctrlText + KeyLabel(ov->key.vk);
				if (ch2) label += " / " + ctrlText + KeyLabel(ov->key2.vk);
				if (ov->key.mods & kCtrl) ctrl = 1;
				else if (ctrl == 1) ctrl = 2;
			}
			title = TitleWithKey(o.title, label);
			changed++;
		}
		std::string curKey, curTitle;
		if (!ReadString(o.desc + kDescKey, curKey) || !ReadString(o.desc + kDescTitle, curTitle)) continue;
		if (curKey != key) WriteString(o.desc + kDescKey, key);
		if (curTitle != title) WriteString(o.desc + kDescTitle, title);
		*reinterpret_cast<short *>(o.desc + kDescCtrl) = ctrl;
	}
	if (changed) LogD("controls", "%d commands with another key", changed);
}

bool FileTime(FILETIME &t)
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExA(ControlsPath().c_str(), GetFileExInfoStandard, &data)) return false;
	t = data.ftLastWriteTime;
	return true;
}

void LoadControls()
{
	const std::string path = ControlsPath();
	g_fileSeen = FileTime(g_fileTime);
	int changed = 0;
	for (int i = 0; i < kActionCount; i++) {
		g_bind[i].vk = kActions[i].defVk;
		g_bind[i].mods = kActions[i].defMods;
		g_bind2[i] = KeySpec();
		char buf[96];
		GetPrivateProfileStringA("Keys", kActions[i].id, "\x01", buf, sizeof(buf), path.c_str());
		if (buf[0] == '\x01') continue;
		KeySpec k, k2;
		bool ok, ok2;
		ParseSpecPair(buf, k, k2, ok, ok2);
		std::string why;
		if (!ok || (k.vk && !ControlsKeyAllowed(kActions[i].rule, k, why))) {
			LogW("controls", "%s=%s is not valid (%s); keeping the default", kActions[i].id, buf, ok ? why.c_str() : "not understood");
			continue;
		}
		if (!ok2 || (k2.vk && !ControlsKeyAllowed(kActions[i].rule, k2, why))) {
			LogW("controls", "the alternative of %s=%s is not valid; removed", kActions[i].id, buf);
			k2 = KeySpec();
		}
		NormalizePair(k, k2);
		g_bind[i] = k;
		g_bind2[i] = k2;
		changed++;
	}
	char mode[32];
	GetPrivateProfileStringA("General", "Layout", "", mode, sizeof(mode), path.c_str());
	g_gridMode = _stricmp(mode, "grid") == 0;
	g_barLetters = GetPrivateProfileIntA("General", "LettersOnBar", 1, path.c_str()) != 0;
	{
		char box[16];
		GetPrivateProfileStringA("General", "ZoomBox", "ctrl", box, sizeof(box), path.c_str());
		g_zoomBoxMode = _stricmp(box, "alt") == 0 ? kZoomBoxAlt : _stricmp(box, "shift") == 0 ? kZoomBoxShift : _stricmp(box, "off") == 0 ? kZoomBoxOff : kZoomBoxCtrl;
	}
	g_orderOverrides.clear();
	std::vector<char> section(32768);
	const DWORD n = GetPrivateProfileSectionA("Commands", section.data(), static_cast<DWORD>(section.size()), path.c_str());
	for (const char *p = section.data(); n && *p; p += strlen(p) + 1) {
		const char *eq = strchr(p, '=');
		if (!eq || *p == ';') continue;
		std::string name(p, eq - p);
		while (!name.empty() && name.back() == ' ') name.pop_back();
		KeySpec k, k2;
		bool ok, ok2;
		ParseSpecPair(eq + 1, k, k2, ok, ok2);
		if (!ok || (k.vk && !OrderChar(k.vk)) || (k.mods & ~kCtrl)) {
			LogW("controls", "invalid key for command %s: \"%s\" (a letter or a sign, with optional Ctrl+)", name.c_str(), eq + 1);
			continue;
		}
		if (!ok2 || (k2.vk && (!OrderChar(k2.vk) || k2.mods != k.mods))) k2 = KeySpec();
		NormalizePair(k, k2);
		g_orderOverrides.push_back({name, k, k2});
	}
	if (g_fileSeen) {
		LogI("controls", "controls.ini with %d keys and %d changed commands%s", changed, static_cast<int>(g_orderOverrides.size()),
			g_gridMode ? "; commands by position (grid)" : "");
	}
}

void CheckReload()
{
	FILETIME t;
	const bool seen = FileTime(t);
	if (seen == g_fileSeen && (!seen || CompareFileTime(&t, &g_fileTime) == 0)) return;
	LoadControls();
	ApplyOrders();
}

enum Act : unsigned char { kPass, kSwallow, kTranslate, kPassNoInfobar, kPassNoNotif };
struct Decision {
	Act act = kPass;
	unsigned short target = 0;
};

Decision Decide(unsigned short vk, unsigned char mods)
{
	Decision d;
	int best = -1, bestScore = -1;
	for (int i = 0; i < kActionCount; i++) {
		for (int slot = 0; slot < 2; slot++) {
			const KeySpec &b = slot ? g_bind2[i] : g_bind[i];
			if (kActions[i].kind == kMod || !b.vk || b.vk != vk || (mods & b.mods) != b.mods) continue;
			int bits = 0;
			for (unsigned char m = b.mods; m; m >>= 1) bits += m & 1;
			const bool custom = slot || b.vk != kActions[i].defVk || b.mods != kActions[i].defMods;
			const int score = bits * 2 + (custom ? 1 : 0);
			if (score > bestScore) {
				best = i;
				bestScore = score;
			}
		}
	}
	if (best >= 0) {
		const Action &a = kActions[best];
		if (a.defVk == vk) return d;
		if (a.kind == kCamera) {
			d.act = kSwallow;
			return d;
		}
		d.act = kTranslate;
		d.target = a.defVk;
		return d;
	}
	for (int i = 0; i < kActionCount; i++) {
		const Action &a = kActions[i];
		const bool stillUses = (g_bind[i].vk == vk && g_bind[i].mods == a.defMods) || (g_bind2[i].vk == vk && g_bind2[i].mods == a.defMods);
		if (a.kind == kMod || a.defVk != vk || stillUses) continue;
		switch (a.kind) {
		case kCamera: return d;
		case kInfobar: d.act = kPassNoInfobar; return d;
		case kNotif: d.act = kPassNoNotif; return d;
		default: d.act = kSwallow; return d;
		}
	}
	return d;
}

bool g_blockOrders = false, g_blockInfobar = false, g_blockNotif = false;
Decision g_down[256];
bool g_downSeen[256];
bool g_eatChar = false;

bool InEditor()
{
	return *reinterpret_cast<int *>(addr::kEditorMode) != 0;
}

typedef void(__fastcall *OnKey_t)(void *self, void *edx, int msg, unsigned key, unsigned mods);
OnKey_t g_origMapViewOnKey = nullptr;

void __fastcall HookMapViewOnKey(void *self, void *edx, int msg, unsigned key, unsigned mods)
{
	if (msg != 0x201 && msg != 0x202) {
		g_origMapViewOnKey(self, edx, msg, key, mods);
		return;
	}
	if (InEditor()) {
		g_origMapViewOnKey(self, edx, msg, key, mods);
		return;
	}
	const unsigned short vk = static_cast<unsigned short>(key);
	Decision d;
	if (msg == 0x201) {
		if (vk < 256 && g_downSeen[vk]) {
			d = g_down[vk];
		}
		else {
			ControlsNoteLayout();
			CheckReload();
			d = Decide(vk, CurrentMods());
			if (vk < 256) {
				g_down[vk] = d;
				g_downSeen[vk] = true;
			}
		}
	}
	else {
		const bool seen = vk < 256 && g_downSeen[vk];
		d = seen ? g_down[vk] : Decide(vk, CurrentMods());
		if (vk < 256) g_downSeen[vk] = false;
		if (!seen && vk == VK_SNAPSHOT && d.act == kTranslate) {
			g_blockOrders = true;
			g_origMapViewOnKey(self, edx, 0x201, (key & 0xffff0000u) | d.target, mods);
			g_origMapViewOnKey(self, edx, 0x202, (key & 0xffff0000u) | d.target, mods);
			g_blockOrders = false;
			return;
		}
	}
	switch (d.act) {
	case kPass:
		g_origMapViewOnKey(self, edx, msg, key, mods);
		break;
	case kSwallow:
		break;
	case kTranslate:
		g_blockOrders = true;
		g_origMapViewOnKey(self, edx, msg, (key & 0xffff0000u) | d.target, mods);
		g_blockOrders = false;
		break;
	case kPassNoInfobar:
		g_blockInfobar = true;
		g_origMapViewOnKey(self, edx, msg, key, mods);
		g_blockInfobar = false;
		break;
	case kPassNoNotif:
		g_blockNotif = true;
		g_origMapViewOnKey(self, edx, msg, key, mods);
		g_blockNotif = false;
		break;
	}
}

typedef int(__cdecl *ScrollHook_t)(char *msg, void *out);
ScrollHook_t g_origScrollHook = nullptr;

unsigned short CameraKey(unsigned short vk)
{
	for (int i = 0; i < kActionCount; i++) {
		if (kActions[i].kind == kCamera && (g_bind[i].vk == vk || g_bind2[i].vk == vk)) return kActions[i].defVk;
	}
	for (int i = 0; i < kActionCount; i++) {
		if (kActions[i].kind == kCamera && kActions[i].defVk == vk) return 0x07;
	}
	return vk;
}

unsigned g_heldBits = 0;
unsigned short g_heldVk[16] = {};

unsigned ArrowBit(unsigned short vk)
{
	switch (vk) {
	case VK_LEFT: return 8;
	case VK_UP: return 2;
	case VK_RIGHT: return 4;
	case VK_DOWN: return 1;
	}
	return 0;
}

int __cdecl HookScrollHook(char *msg, void *out)
{
	const int id = *reinterpret_cast<int *>(msg + 0x10);
	volatile unsigned *bits = reinterpret_cast<volatile unsigned *>(addr::kScrollBits);
	volatile int *token = reinterpret_cast<volatile int *>(addr::kScrollToken);
	if (id == 0x101 && g_heldBits) {
		for (unsigned bit = 1; bit <= 8; bit <<= 1) {
			if ((g_heldBits & bit) && !(GetAsyncKeyState(g_heldVk[bit]) & 0x8000)) g_heldBits &= ~bit;
		}
		if (g_heldBits) {
			const int saved = *token;
			const int r = g_origScrollHook(msg, out);
			*token = saved;
			*bits |= g_heldBits;
			return r;
		}
	}
	if (id != 0x201 && id != 0x202) return g_origScrollHook(msg, out);
	unsigned short &vk = *reinterpret_cast<unsigned short *>(msg + 0x16);
	static unsigned short camDown[256] = {};
	const unsigned short was = vk;
	unsigned short now = CameraKey(was);
	if (was < 256) {
		if (id == 0x201) {
			if (camDown[was]) now = camDown[was];
			else camDown[was] = now ? now : 0xffff;
		}
		else {
			if (camDown[was]) now = camDown[was] == 0xffff ? 0 : camDown[was];
			camDown[was] = 0;
		}
	}
	if (now != was) vk = now;
	const int r = g_origScrollHook(msg, out);
	vk = was;
	if (const unsigned bit = ArrowBit(now)) {
		if (id == 0x201 && (*bits & bit)) {
			g_heldBits |= bit;
			g_heldVk[bit] = was;
		}
		else if (id == 0x202) {
			g_heldBits &= ~bit;
		}
	}
	return r;
}

int VisibleButtons(uintptr_t bar, int *ids, int max)
{
	int n = 0;
	__try {
		const uintptr_t begin = *reinterpret_cast<uintptr_t *>(bar + 0x78), end = *reinterpret_cast<uintptr_t *>(bar + 0x7c);
		if (!begin || end < begin || (end - begin) / 4 > 64) return 0;
		for (uintptr_t p = begin; p < end && n < max; p += 4) {
			const uintptr_t button = *reinterpret_cast<uintptr_t *>(p);
			if (button && (*reinterpret_cast<unsigned *>(button + 0x28) & 2)) ids[n++] = *reinterpret_cast<int *>(button + 0x80);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
	return n;
}

int GridIndex(unsigned ch)
{
	const char c = static_cast<char>(toupper(static_cast<int>(ch & 0xff)));
	const char *at = c ? strchr(kGridKeys, c) : nullptr;
	return at ? static_cast<int>(at - kGridKeys) : -1;
}

typedef void(__fastcall *HotkeyChar_t)(void *self, void *edx, unsigned ch);
HotkeyChar_t g_origHotkeyChar = nullptr;

void __fastcall HookHotkeyChar(void *self, void *edx, unsigned ch)
{
	if (g_blockOrders) return;
	if (!g_gridMode) {
		g_origHotkeyChar(self, edx, ch);
		return;
	}
	const int index = GridIndex(ch);
	if (index < 0) return;
	int ids[64];
	const int n = VisibleButtons(reinterpret_cast<uintptr_t>(self), ids, 64);
	if (index >= n) return;
	typedef void(__fastcall * Clicked_t)(void *bar, void *edx, int id);
	reinterpret_cast<Clicked_t>(addr::kOnCommandClicked)(self, nullptr, ids[index]);
}

typedef void(__fastcall *Rebuild_t)(void *bar);
Rebuild_t g_origRebuild = nullptr;
bool g_inRebuild = false;

bool GridTitles(uintptr_t bar)
{
	int ids[64];
	const int n = VisibleButtons(bar, ids, 64);
	bool changed = false;
	for (int i = 0; i < n; i++) {
		for (const Order &o : g_orders) {
			if (static_cast<int>(o.index) != ids[i]) continue;
			const std::string title = TitleWithKey(o.title, i < static_cast<int>(sizeof(kGridKeys) - 1) ? std::string(1, kGridKeys[i]) : "");
			std::string cur;
			if (ReadString(o.desc + kDescTitle, cur) && cur != title) {
				WriteString(o.desc + kDescTitle, title);
				changed = true;
			}
			break;
		}
	}
	return changed;
}

uintptr_t g_cmdBar = 0;
uintptr_t g_cmdBarGame = 0;

void __fastcall HookRebuild(void *bar)
{
	g_cmdBar = reinterpret_cast<uintptr_t>(bar);
	g_cmdBarGame = *reinterpret_cast<uintptr_t *>(addr::kGame);
	g_origRebuild(bar);
	if (g_inRebuild) return;
	if (g_gridMode && GridTitles(reinterpret_cast<uintptr_t>(bar))) {
		g_inRebuild = true;
		g_origRebuild(bar);
		g_inRebuild = false;
	}
	ComputeBarKeys();
}

typedef void(__fastcall *NotifOnKey_t)(void *self, void *edx, int msg, int key, int x);
NotifOnKey_t g_origNotifOnKey = nullptr;

void __fastcall HookNotifOnKey(void *self, void *edx, int msg, int key, int x)
{
	if (g_blockNotif) return;
	g_origNotifOnKey(self, edx, msg, key, x);
}

typedef void(__fastcall *InfobarSwitch_t)(void *self);
InfobarSwitch_t g_origInfobarSwitch = nullptr;

void __fastcall HookInfobarSwitch(void *self)
{
	if (g_blockInfobar) return;
	g_origInfobarSwitch(self);
}

typedef int(__fastcall *RegisterCommands_t)(void *self, void *edx, void *arg);
RegisterCommands_t g_origRegisterCommands = nullptr;

int __fastcall HookRegisterCommands(void *self, void *edx, void *arg)
{
	g_fileRanges.clear();
	g_loadingRegistry = reinterpret_cast<uintptr_t>(self);
	const int r = g_origRegisterCommands(self, edx, arg);
	g_loadingRegistry = 0;
	CaptureOrders(reinterpret_cast<uintptr_t>(self));
	ApplyOrders();
	return r;
}

typedef void *(__fastcall *ParseFile_t)(void *self, void *edx, const char *path, int n);
ParseFile_t g_origParseFile = nullptr;

size_t DescCount(uintptr_t registry)
{
	uintptr_t begin = 0, end = 0;
	return ReadDescList(registry, begin, end) ? (end - begin) / 4 : 0;
}

void *__fastcall HookParseFile(void *self, void *edx, const char *path, int n)
{
	if (!g_loadingRegistry) return g_origParseFile(self, edx, path, n);
	const size_t before = DescCount(g_loadingRegistry);
	void *r = g_origParseFile(self, edx, path, n);
	std::string file = path ? path : "";
	const size_t slash = file.find_last_of("/\\");
	if (slash != std::string::npos) file = file.substr(slash + 1);
	const size_t dot = file.rfind('.');
	if (dot != std::string::npos) file.resize(dot);
	for (char &c : file) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
	g_fileRanges.push_back({file, before, DescCount(g_loadingRegistry)});
	return r;
}

bool Hook(uintptr_t addr, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr)) == MH_OK) {
		return true;
	}
	LogE("controls", "could not hook %s (%08x)", what, static_cast<unsigned>(addr));
	return false;
}

}

bool ControlsModKeyPressed(int id, unsigned vk)
{
	if (id < kModKeyPanel || id > kModKeyCheats) return false;
	const int index = kFirstModAction + id;
	const unsigned char mods = CurrentMods();
	for (const KeySpec *k : {&g_bind[index], &g_bind2[index]}) {
		if (k->vk && k->vk == vk && mods == k->mods) return true;
	}
	return false;
}

ControlKey ControlsModKey(int id)
{
	if (id < kModKeyPanel || id > kModKeyCheats) return {};
	const int index = kFirstModAction + id;
	return g_bind[index].vk ? g_bind[index] : g_bind2[index];
}

void InstallControls(bool knownExe)
{
	LoadControls();
	if (!knownExe) return;
	Hook(addr::kMapViewOnKey, reinterpret_cast<void *>(&HookMapViewOnKey), reinterpret_cast<void **>(&g_origMapViewOnKey), "CVXMapView::OnKey");
	Hook(addr::kMapScrollHook, reinterpret_cast<void *>(&HookScrollHook), reinterpret_cast<void **>(&g_origScrollHook), "MapScrollMessageHook");
	Hook(addr::kOnHotkeyChar, reinterpret_cast<void *>(&HookHotkeyChar), reinterpret_cast<void **>(&g_origHotkeyChar), "CUICmdBar::OnHotkeyChar");
	Hook(addr::kNotificationOnKey, reinterpret_cast<void *>(&HookNotifOnKey), reinterpret_cast<void **>(&g_origNotifOnKey), "CNotificationMgr::OnKey");
	Hook(addr::kInfobarSwitch, reinterpret_cast<void *>(&HookInfobarSwitch), reinterpret_cast<void **>(&g_origInfobarSwitch), "ActivateInfobarSwitch");
	Hook(addr::kRegisterCommands, reinterpret_cast<void *>(&HookRegisterCommands), reinterpret_cast<void **>(&g_origRegisterCommands),
		"CCmdRegistry::RegisterCommandClasses");
	Hook(addr::kParseFile, reinterpret_cast<void *>(&HookParseFile), reinterpret_cast<void **>(&g_origParseFile), "CXmlParser::ParseFile");
	Hook(addr::kCmdBarRebuild, reinterpret_cast<void *>(&HookRebuild), reinterpret_cast<void **>(&g_origRebuild), "CUICmdBar::Rebuild");
}

namespace {
const OrderFile *FindOrderFile(const std::string &file)
{
	for (const OrderFile &f : kOrderFiles) {
		if (_stricmp(f.file, file.c_str()) == 0) return &f;
	}
	return nullptr;
}

unsigned short OrderCharVk(char c)
{
	if (c >= 'a' && c <= 'z') return static_cast<unsigned short>(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z') return static_cast<unsigned short>(c);
	for (unsigned short vk = VK_OEM_1; vk <= VK_OEM_7; vk++) {
		if (OrderChar(vk) == c) return vk;
	}
	return 0;
}

ControlKey OrderDefault(const Order &o)
{
	ControlKey k;
	if (!o.key.empty()) k.vk = OrderCharVk(o.key[0]);
	if (k.vk && o.ctrl == 1) k.mods = kCtrl;
	return k;
}

const OrderOverride *FindOverride(const std::string &name)
{
	for (const OrderOverride &x : g_orderOverrides) {
		if (_stricmp(x.name.c_str(), name.c_str()) == 0) return &x;
	}
	return nullptr;
}

std::string SpecText(ControlKey k)
{
	if (!k.vk) return "";
	std::string s;
	if (k.mods & kCtrl) s += "Ctrl+";
	if (k.mods & kShift) s += "Shift+";
	if (k.mods & kAlt) s += "Alt+";
	char buf[16];
	if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) return s + static_cast<char>(k.vk);
	if (k.vk >= VK_F1 && k.vk <= VK_F24) {
		sprintf_s(buf, "F%d", k.vk - VK_F1 + 1);
		return s + buf;
	}
	if (k.vk >= VK_NUMPAD0 && k.vk <= VK_NUMPAD9) {
		sprintf_s(buf, "Num%d", k.vk - VK_NUMPAD0);
		return s + buf;
	}
	for (const KeyName &n : kKeyNames) {
		if (n.vk == k.vk) return s + n.name;
	}
	sprintf_s(buf, "0x%02X", k.vk);
	return s + buf;
}

std::string SpecPair(ControlKey a, ControlKey b)
{
	return b.vk ? SpecText(a) + ", " + SpecText(b) : SpecText(a);
}

void SaveControls()
{
	const std::string path = ControlsPath();
	int count = 0;
	WritePrivateProfileStringA("General", nullptr, nullptr, path.c_str());
	WritePrivateProfileStringA("Keys", nullptr, nullptr, path.c_str());
	WritePrivateProfileStringA("Commands", nullptr, nullptr, path.c_str());
	if (g_gridMode) {
		WritePrivateProfileStringA("General", "Layout", "grid", path.c_str());
		count++;
	}
	if (!g_barLetters) {
		WritePrivateProfileStringA("General", "LettersOnBar", "0", path.c_str());
		count++;
	}
	if (g_zoomBoxMode != kZoomBoxCtrl) {
		static const char *const kNames[kZoomBoxModes] = {"ctrl", "alt", "shift", "off"};
		WritePrivateProfileStringA("General", "ZoomBox", kNames[g_zoomBoxMode], path.c_str());
		count++;
	}
	for (int i = 0; i < kActionCount; i++) {
		if (g_bind[i].vk == kActions[i].defVk && g_bind[i].mods == kActions[i].defMods && !g_bind2[i].vk) continue;
		WritePrivateProfileStringA("Keys", kActions[i].id, SpecPair(g_bind[i], g_bind2[i]).c_str(), path.c_str());
		count++;
	}
	for (const OrderOverride &x : g_orderOverrides) {
		WritePrivateProfileStringA("Commands", x.name.c_str(), SpecPair(x.key, x.key2).c_str(), path.c_str());
		count++;
	}
	WritePrivateProfileStringA(nullptr, nullptr, nullptr, path.c_str());
	if (!count) DeleteFileA(path.c_str());
	g_fileSeen = FileTime(g_fileTime);
}

}

const char *ControlsPageName(int page)
{
	return page >= 0 && page < kPageCount ? kPageNames[page] : "";
}

bool ControlsPageIsOrders(int page)
{
	return page >= kPageUnits;
}

std::vector<ControlInfo> ControlsList()
{
	std::vector<ControlInfo> out;
	for (int i = 0; i < kActionCount; i++) {
		const Action &a = kActions[i];
		ControlInfo c;
		c.id = a.id;
		c.name = a.name;
		c.hint = a.hint;
		c.page = a.page;
		c.rule = a.rule;
		c.context = a.kind == kMod ? -1 : 0;
		c.withOrders = a.kind == kInfobar || a.kind == kNotif;
		c.def.vk = a.defVk;
		c.def.mods = a.defMods;
		c.key = g_bind[i];
		c.key2 = g_bind2[i];
		out.push_back(c);
	}
	std::vector<std::string> seen;
	auto add = [&](const Order &o, int page, const std::string &section, int context) {
		if (o.display.empty()) return;
		for (const std::string &n : seen) {
			if (_stricmp(n.c_str(), o.name.c_str()) == 0) return;
		}
		seen.push_back(o.name);
		const ControlKey def = OrderDefault(o);
		for (ControlInfo &prev : out) {
			if (prev.order && prev.page == page && prev.section == section && prev.name == o.display && prev.def == def) {
				prev.ids.push_back(o.name);
				return;
			}
		}
		ControlInfo c;
		c.id = o.name;
		c.ids.push_back(o.name);
		c.name = o.display;
		c.section = section;
		c.page = page;
		c.order = true;
		c.rule = kRuleOrder;
		c.context = context;
		c.def = OrderDefault(o);
		const OrderOverride *ov = FindOverride(o.name);
		c.key = ov ? ov->key : c.def;
		if (ov) c.key2 = ov->key2;
		out.push_back(c);
	};
	const int fileCount = static_cast<int>(sizeof(kOrderFiles) / sizeof(kOrderFiles[0]));
	for (int page = kPageUnits; page < kPageCount; page++) {
		for (int f = 0; f < fileCount; f++) {
			const OrderFile &file = kOrderFiles[f];
			if (file.page != page) continue;
			const int context = page == kPageBuildings ? 100 + f : 1;
			for (const Order &o : g_orders) {
				if (_stricmp(o.file.c_str(), file.file) == 0) add(o, page, file.label, context);
			}
		}
	}
	int extra = 0;
	std::vector<std::string> files;
	for (const Order &o : g_orders) {
		if (FindOrderFile(o.file)) continue;
		if (std::find(files.begin(), files.end(), o.file) == files.end()) files.push_back(o.file);
	}
	std::vector<ModInfo> mods;
	for (const std::string &dir : ModsMountedDirs()) {
		ModInfo m;
		if (ModsFind(dir.substr(dir.find_last_of('\\') + 1), m)) mods.push_back(m);
	}
	const std::string lang = ModsLanguage();
	for (const std::string &file : files) {
		std::string label = file.empty() ? "Others" : file;
		for (size_t i = 1; i < label.size(); i++) label[i] = static_cast<char>(tolower(static_cast<unsigned char>(label[i])));
		int page = kPageBuildings;
		std::string lowerFile = file;
		for (char &c : lowerFile) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		const std::string path = "data/commands/" + lowerFile + ".xml";
		for (const ModInfo &m : mods) {
			if (std::find(m.paths.begin(), m.paths.end(), path) == m.paths.end()) continue;
			std::string name;
			for (const ModOrderLabel &l : m.orderLabels) {
				if (l.file != file) continue;
				for (const char *want : {lang.c_str(), "english", ""}) {
					for (const auto &n : l.names) {
						if (name.empty() && n.first == want) name = n.second;
					}
				}
				if (name.empty()) name = l.names[0].second;
			}
			int fromThisMod = 0;
			for (const std::string &f : files) {
				std::string lf = f;
				for (char &c : lf) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
				if (std::find(m.paths.begin(), m.paths.end(), "data/commands/" + lf + ".xml") != m.paths.end()) fromThisMod++;
			}
			label = name.empty() ? (fromThisMod > 1 ? m.name + " · " + label : m.name) : m.name + " · " + name;
			page = kPageModOrders;
			break;
		}
		for (const Order &o : g_orders) {
			if (o.file == file) add(o, page, label, 500 + extra);
		}
		extra++;
	}
	return out;
}

void ControlsSetKey(const std::string &id, bool order, int slot, ControlKey key)
{
	if (order) {
		ControlKey def;
		bool found = false;
		for (const Order &o : g_orders) {
			if (_stricmp(o.name.c_str(), id.c_str()) == 0) {
				def = OrderDefault(o);
				found = true;
				break;
			}
		}
		if (!found) return;
		const OrderOverride *ov = FindOverride(id);
		KeySpec k1 = ov ? ov->key : def, k2 = ov ? ov->key2 : KeySpec();
		(slot ? k2 : k1) = key;
		NormalizePair(k1, k2);
		g_orderOverrides.erase(std::remove_if(g_orderOverrides.begin(), g_orderOverrides.end(),
								   [&](const OrderOverride &x) { return _stricmp(x.name.c_str(), id.c_str()) == 0; }),
			g_orderOverrides.end());
		if (k1 != def || k2.vk) g_orderOverrides.push_back({id, k1, k2});
		ApplyOrders();
	}
	else {
		for (int i = 0; i < kActionCount; i++) {
			if (id != kActions[i].id) continue;
			(slot ? g_bind2[i] : g_bind[i]) = key;
			NormalizePair(g_bind[i], g_bind2[i]);
		}
	}
	LogI("controls", "%s (%s) -> %s", id.c_str(), slot ? "alternative" : "primary", key.vk ? SpecText(key).c_str() : "no key");
	SaveControls();
}

void ControlsResetRow(const std::string &id, bool order)
{
	if (order) {
		g_orderOverrides.erase(std::remove_if(g_orderOverrides.begin(), g_orderOverrides.end(),
								   [&](const OrderOverride &x) { return _stricmp(x.name.c_str(), id.c_str()) == 0; }),
			g_orderOverrides.end());
		ApplyOrders();
	}
	else {
		for (int i = 0; i < kActionCount; i++) {
			if (id != kActions[i].id) continue;
			g_bind[i].vk = kActions[i].defVk;
			g_bind[i].mods = kActions[i].defMods;
			g_bind2[i] = KeySpec();
		}
	}
	LogI("controls", "%s as in the game", id.c_str());
	SaveControls();
}

void ControlsResetAll()
{
	for (int i = 0; i < kActionCount; i++) {
		g_bind[i].vk = kActions[i].defVk;
		g_bind[i].mods = kActions[i].defMods;
		g_bind2[i] = KeySpec();
	}
	g_orderOverrides.clear();
	g_gridMode = false;
	g_barLetters = true;
	g_zoomBoxMode = kZoomBoxCtrl;
	ApplyOrders();
	SaveControls();
	LogI("controls", "everything as in the original game");
}

std::string ControlsKeyLabel(ControlKey k)
{
	if (!k.vk) return "";
	std::string s;
	if (k.mods & kCtrl) s += "Ctrl + ";
	if (k.mods & kShift) s += "Shift + ";
	if (k.mods & kAlt) s += "Alt + ";
	struct Label {
		unsigned short vk;
		const char *text;
	};
	static const Label kLabels[] = {
		{VK_LEFT, "←"}, {VK_UP, "↑"}, {VK_RIGHT, "→"}, {VK_DOWN, "↓"}, {VK_ESCAPE, "Esc"}, {VK_RETURN, "Enter"},
		{VK_SPACE, "Space"}, {VK_TAB, "Tab"}, {VK_BACK, "Backspace"}, {VK_HOME, "Home"}, {VK_END, "End"},
		{VK_PRIOR, "PgUp"}, {VK_NEXT, "PgDn"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Del"}, {VK_SNAPSHOT, "PrtScn"},
		{VK_PAUSE, "Pause"}, {VK_MULTIPLY, "Num *"}, {VK_ADD, "Num +"}, {VK_SUBTRACT, "Num -"}, {VK_DECIMAL, "Num ."},
		{VK_DIVIDE, "Num /"}, {VK_MBUTTON, "Wheel (click)"}, {VK_XBUTTON1, "Mouse 4"}, {VK_XBUTTON2, "Mouse 5"},
	};
	for (const Label &l : kLabels) {
		if (l.vk == k.vk) return s + l.text;
	}
	char buf[16];
	if (k.vk >= VK_F1 && k.vk <= VK_F24) {
		sprintf_s(buf, "F%d", k.vk - VK_F1 + 1);
		return s + buf;
	}
	if (k.vk >= VK_NUMPAD0 && k.vk <= VK_NUMPAD9) {
		sprintf_s(buf, "Num %d", k.vk - VK_NUMPAD0);
		return s + buf;
	}
	if (k.vk >= '0' && k.vk <= '9') return s + static_cast<char>(k.vk);
	const std::string l = KeyLabel(k.vk);
	if (l != "?") return s + l;
	sprintf_s(buf, "Key %02X", k.vk);
	return s + buf;
}

bool ControlsKeyAllowed(KeyRule rule, ControlKey k, std::string &why)
{
	switch (k.vk) {
	case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
	case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
		why = "that key can't be used by itself";
		return false;
	}
	if ((k.mods & kAlt) && (k.vk == VK_RETURN || k.vk == VK_TAB || k.vk == VK_F4 || k.vk == VK_ESCAPE)) {
		why = "Alt + Enter, Alt + Tab, Alt + F4 and Alt + Esc belong to Windows or Imperivm Resurrected";
		return false;
	}
	switch (rule) {
	case kRulePlain:
		if (k.mods) {
			why = "this action already uses Ctrl and Shift for other things: pick a single key";
			return false;
		}
		break;
	case kRuleOrder:
		if (!OrderChar(k.vk)) {
			why = "commands can only use letters and symbols";
			return false;
		}
		if (k.mods & ~kCtrl) {
			why = "commands can only take Ctrl";
			return false;
		}
		break;
	case kRuleShortcut:
		if (IsMouseVk(k.vk)) {
			why = "mouse buttons only work in-game";
			return false;
		}
		if (!(k.mods & (kCtrl | kAlt)) && !(k.vk >= VK_F1 && k.vk <= VK_F24)) {
			why = "this shortcut also works while you type: it needs Ctrl or Alt, or an F key";
			return false;
		}
		break;
	default:
		break;
	}
	return true;
}

bool ControlsGridMode()
{
	return g_gridMode;
}

std::string ControlsSetGridMode(bool on)
{
	if (on == g_gridMode) return "";
	g_gridMode = on;
	std::string note;
	struct Move {
		const char *id;
		unsigned short from, to;
		const char *what;
	};
	const Move kMoves[] = {{"last_alert", 'Q', VK_BACK, "Go to last alert"}, {"info_panel", 'I', VK_TAB, "the info tab"}};
	for (const Move &m : kMoves) {
		for (int i = 0; i < kActionCount; i++) {
			if (strcmp(kActions[i].id, m.id) != 0) continue;
			const unsigned short want = on ? m.from : m.to, put = on ? m.to : m.from;
			if (!(g_bind[i].vk == want && !g_bind[i].mods) && !(!on && !g_bind[i].vk)) continue;
			const char *taken = nullptr;
			for (int j = 0; j < kActionCount && !taken; j++) {
				if (j == i || kActions[j].kind == kMod) continue;
				if ((g_bind[j].vk == put && !g_bind[j].mods) || (g_bind2[j].vk == put && !g_bind2[j].mods)) taken = kActions[j].name;
			}
			if (on && taken) {
				g_bind[i].vk = 0;
				g_bind[i].mods = 0;
				note += std::string(note.empty() ? "" : "; ") + m.what + Tr(" is left without a key (") + ControlsKeyLabel({put, 0}) +
					Tr(" already belongs to «") + taken + "»)";
				continue;
			}
			g_bind[i].vk = put;
			g_bind[i].mods = 0;
			if (on) note += std::string(note.empty() ? "" : "; ") + m.what + Tr(" moves to ") + ControlsKeyLabel({put, 0});
		}
	}
	ApplyOrders();
	SaveControls();
	LogI("controls", "commands %s", on ? "by position (grid)" : "by letter");
	return note;
}

std::string ControlsGridKeys()
{
	return kGridKeys;
}

std::string ControlsSnapshot()
{
	std::string out = "0";
	FILE *f = nullptr;
	if (fopen_s(&f, ControlsPath().c_str(), "rb") == 0 && f) {
		out = "1";
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
		fclose(f);
	}
	return out;
}

void ControlsRestore(const std::string &snapshot)
{
	const std::string path = ControlsPath();
	if (snapshot.empty() || snapshot[0] != '1') {
		DeleteFileA(path.c_str());
	}
	else {
		FILE *f = nullptr;
		if (fopen_s(&f, path.c_str(), "wb") == 0 && f) {
			fwrite(snapshot.data() + 1, 1, snapshot.size() - 1, f);
			fclose(f);
			LegacyMigrateControls(path);
		}
	}
	LoadControls();
	ApplyOrders();
	LogI("controls", "controls.ini reloaded (undo or paste)");
}

namespace {
bool g_mouseDown[3];

bool MouseBound(unsigned short vk)
{
	for (int i = 0; i < kActionCount; i++) {
		if (kActions[i].kind == kMod) continue;
		if (g_bind[i].vk == vk || g_bind2[i].vk == vk) return true;
	}
	return false;
}

}

bool ControlsMouseButton(unsigned msg, unsigned wp, unsigned &vk, bool &down)
{
	int index;
	switch (msg) {
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
		vk = VK_MBUTTON;
		down = true;
		index = 0;
		break;
	case WM_MBUTTONUP:
		vk = VK_MBUTTON;
		down = false;
		index = 0;
		break;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONDBLCLK:
	case WM_XBUTTONUP:
		vk = HIWORD(wp) == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1;
		down = msg != WM_XBUTTONUP;
		index = vk == VK_XBUTTON2 ? 2 : 1;
		break;
	default:
		return false;
	}
	if (down) {
		if (!MouseBound(static_cast<unsigned short>(vk))) return false;
		g_mouseDown[index] = true;
		return true;
	}
	if (!g_mouseDown[index]) return false;
	g_mouseDown[index] = false;
	return true;
}

void ControlsNoteLayout()
{
	const HKL now = GetKeyboardLayout(0);
	const HKL before = g_layout.exchange(now);
	if (before == now) return;
	char name[KL_NAMELENGTH] = {};
	GetKeyboardLayoutNameA(name);
	LogI("controls", "keyboard layout %s", name);
	if (before) ApplyOrders();
}

HKL ControlsLayout()
{
	return Layout();
}

namespace {
struct BarKey {
	RECT r;
	std::string label;
};
std::mutex g_barMx;
std::vector<BarKey> g_barKeys;
DWORD g_barTick = 0;

bool MapViewShown()
{
	__try {
		const uintptr_t map = *reinterpret_cast<uintptr_t *>(addr::kMapView);
		return map && (*reinterpret_cast<unsigned *>(map + 0x28) & 2);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool MapViewFocused()
{
	__try {
		void *root = *reinterpret_cast<void **>(addr::kUIRoot);
		void *map = *reinterpret_cast<void **>(addr::kMapView);
		if (!root || !map) return false;
		typedef void *(__fastcall * Focus_t)(void *self);
		return reinterpret_cast<Focus_t>((*reinterpret_cast<void ***>(root))[0x74 / 4])(root) == map;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

int BarButtons(uintptr_t bar, RECT *rects, int *ids, int max)
{
	int n = 0;
	__try {
		if (!(*reinterpret_cast<unsigned *>(bar + 0x28) & 2)) return 0;
		const uintptr_t begin = *reinterpret_cast<uintptr_t *>(bar + 0x78), end = *reinterpret_cast<uintptr_t *>(bar + 0x7c);
		if (!begin || end < begin || (end - begin) / 4 > 64) return 0;
		for (uintptr_t p = begin; p < end && n < max; p += 4) {
			const uintptr_t b = *reinterpret_cast<uintptr_t *>(p);
			if (!b || !(*reinterpret_cast<unsigned *>(b + 0x28) & 2)) continue;
			const short *r = reinterpret_cast<const short *>(b + 0x10);
			rects[n] = {r[0], r[1], r[2] + 1, r[3] + 1};
			ids[n] = *reinterpret_cast<int *>(b + 0x80);
			n++;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
	return n;
}

}

struct EmptyButton {
	int id;
	const char *action;
};
const EmptyButton kEmptyButtons[] = {
	{0x1008, "help"}, {0x1005, "diplomacy"}, {0x1003, "quick_save"}, {0x1009, "hero_group"}, {0x1002, "notes"},
	{0x1004, "quick_load"}, {0x1001, "menu"}, {0x1006, "chat"}, {0x1007, "minimap"},
};

bool EmptyButtonRect(uintptr_t menu, int id, RECT &r)
{
	__try {
		if (!(*reinterpret_cast<unsigned *>(menu + 0x28) & 2)) return false;
		typedef void *(__fastcall * GetChild_t)(void *dlg, void *edx, int id, const char *cls);
		void **vt = *reinterpret_cast<void ***>(menu);
		const uintptr_t b = reinterpret_cast<uintptr_t>(reinterpret_cast<GetChild_t>(vt[0x7c / 4])(reinterpret_cast<void *>(menu), nullptr, id,
			*reinterpret_cast<const char **>(addr::kClassNameCUIButton)));
		if (!b || !(*reinterpret_cast<unsigned *>(b + 0x28) & 2)) return false;
		const short *m = reinterpret_cast<const short *>(menu + 0x10);
		if (m[0] <= 0 && m[1] <= 0) return false;
		const short *p = reinterpret_cast<const short *>(b + 0x10);
		r = {p[0], p[1], p[2] + 1, p[3] + 1};
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

uintptr_t EmptyMenu()
{
	__try {
		const uintptr_t sel = *reinterpret_cast<uintptr_t *>(addr::kSelectionManager);
		return sel ? *reinterpret_cast<uintptr_t *>(sel + 0x150) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

std::string BadgeLabel(ControlKey k)
{
	if (!k.vk) return "";
	std::string base;
	switch (k.vk) {
	case VK_SPACE: base = "Spc"; break;
	case VK_BACK: base = "Bksp"; break;
	case VK_RETURN: base = "Enter"; break;
	default: base = ControlsKeyLabel({k.vk, 0});
	}
	std::string mods;
	if (k.mods & kCtrl) mods += "Ctrl+";
	if (k.mods & kShift) mods += "May+";
	if (k.mods & kAlt) mods += "Alt+";
	return mods + base;
}

std::atomic<bool> g_barChanged{false};

void ControlsGameTick()
{
	static DWORD last = 0;
	const DWORD now = GetTickCount();
	if (now - last < 40) return;
	last = now;
	ComputeBarKeys();
}

std::atomic<bool> g_barDrawn{false};

bool ControlsBarKeysChanged()
{
	if (g_barChanged.exchange(false)) return true;
	if (g_barDrawn) {
		const bool noGame = *reinterpret_cast<void **>(addr::kGame) == nullptr;
		std::lock_guard<std::mutex> lock(g_barMx);
		if (noGame || GetTickCount() - g_barTick > 300) {
			g_barDrawn = false;
			LogD("controls", "letters removed from the image (%s)", noGame ? "no game" : "without catching up");
			return true;
		}
	}
	return false;
}

void ComputeBarKeys()
{
	const DWORD now = GetTickCount();
	std::vector<BarKey> keys;
	const uintptr_t game = *reinterpret_cast<uintptr_t *>(addr::kGame);
	const bool playing = g_barLetters && game && g_cmdBarGame == game && MapViewShown() && !InEditor();
	static bool wasPlaying = false;
	if (playing != wasPlaying) {
		wasPlaying = playing;
		LogI("controls", "bar letters %s", playing ? "in view (game on screen)" : "hidden");
	}
	if (playing && g_cmdBar) {
		RECT rects[64];
		int ids[64];
		const int n = BarButtons(g_cmdBar, rects, ids, 64);
		for (int i = 0; i < n; i++) {
			std::string label;
			if (g_gridMode) {
				if (i < static_cast<int>(sizeof(kGridKeys) - 1)) label = KeyLabel(static_cast<unsigned short>(kGridKeys[i]));
			}
			else {
				for (const Order &o : g_orders) {
					if (static_cast<int>(o.index) != ids[i]) continue;
					std::string key;
					if (ReadString(o.desc + kDescKey, key) && !key.empty()) {
						const unsigned short vk = OrderCharVk(key[0]);
						if (vk) label = KeyLabel(vk);
					}
					break;
				}
			}
			if (!label.empty()) keys.push_back({rects[i], label});
		}
	}
	if (playing) {
		if (const uintptr_t menu = EmptyMenu()) {
			for (const EmptyButton &eb : kEmptyButtons) {
				RECT r;
				if (!EmptyButtonRect(menu, eb.id, r)) continue;
				for (int i = 0; i < kActionCount; i++) {
					if (strcmp(kActions[i].id, eb.action) != 0) continue;
					const std::string label = BadgeLabel(g_bind[i]);
					if (!label.empty()) keys.push_back({r, label});
				}
			}
		}
	}
	std::lock_guard<std::mutex> lock(g_barMx);
	bool same = keys.size() == g_barKeys.size();
	for (size_t i = 0; same && i < keys.size(); i++) {
		same = EqualRect(&keys[i].r, &g_barKeys[i].r) && keys[i].label == g_barKeys[i].label;
	}
	if (!same) g_barChanged = true;
	g_barKeys.swap(keys);
	g_barTick = now;
}

void ControlsDrawBarKeys(uint32_t *buf, int w, int h)
{
	std::vector<BarKey> keys;
	{
		std::lock_guard<std::mutex> lock(g_barMx);
		if (GetTickCount() - g_barTick > 300) return;
		if (*reinterpret_cast<void **>(addr::kGame) == nullptr) return;
		keys = g_barKeys;
	}
	if (keys.empty()) return;
	g_barDrawn = true;
	static HDC dc = nullptr;
	static HBITMAP bmp = nullptr;
	static uint32_t *bits = nullptr;
	static int bw = 0, bh = 0;
	static HFONT font = nullptr;
	static int fontPx = 0;
	for (const BarKey &k : keys) {
		const int side = std::max(12, std::min(26, static_cast<int>((k.r.bottom - k.r.top) * 0.36f)));
		const int tw = std::max(side, static_cast<int>(side * (0.5f + 0.42f * k.label.size()))), th = side;
		if (!dc || tw > bw || th > bh) {
			if (!dc) {
				dc = CreateCompatibleDC(nullptr);
				if (!dc) return;
			}
			SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
			fontPx = 0;
			if (bmp) {
				SelectObject(dc, CreateCompatibleBitmap(dc, 1, 1));
				DeleteObject(bmp);
				bmp = nullptr;
			}
			bits = nullptr;
			bw = bh = 0;
			BITMAPINFO bi = {};
			bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
			bi.bmiHeader.biWidth = std::max(tw, 64);
			bi.bmiHeader.biHeight = -std::max(th, 32);
			bi.bmiHeader.biPlanes = 1;
			bi.bmiHeader.biBitCount = 32;
			bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void **>(&bits), nullptr, 0);
			if (!bmp || !bits) {
				bits = nullptr;
				return;
			}
			HGDIOBJ old = SelectObject(dc, bmp);
			if (old && old != bmp) DeleteObject(old);
			SetBkMode(dc, TRANSPARENT);
			bw = std::max(tw, 64);
			bh = std::max(th, 32);
		}
		const int px = std::max(9, static_cast<int>(side * 0.78f));
		if (!font || px != fontPx) {
			SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
			if (font) DeleteObject(font);
			font = CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			fontPx = px;
		}
		RECT r = {0, 0, tw, th};
		HBRUSH edge = CreateSolidBrush(RGB(200, 160, 80)), face = CreateSolidBrush(RGB(24, 18, 12));
		FillRect(dc, &r, edge);
		RECT in = {1, 1, tw - 1, th - 1};
		FillRect(dc, &in, face);
		DeleteObject(edge);
		DeleteObject(face);
		SelectObject(dc, font);
		SetTextColor(dc, RGB(255, 232, 160));
		const std::string label = Tr(k.label);
		int wl = MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, nullptr, 0);
		std::wstring wtext(wl > 0 ? wl - 1 : 0, L'\0');
		if (wl > 1) MultiByteToWideChar(CP_UTF8, 0, label.c_str(), -1, &wtext[0], wl);
		DrawTextW(dc, wtext.c_str(), static_cast<int>(wtext.size()), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		GdiFlush();
		const int ox = k.r.left + 2, oy = k.r.top + 2;
		for (int y = 0; y < th; y++) {
			const int ty = oy + y;
			if (ty < 0 || ty >= h) continue;
			for (int x = 0; x < tw; x++) {
				const int tx = ox + x;
				if (tx < 0 || tx >= w) continue;
				const uint32_t src = bits[static_cast<size_t>(y) * bw + x];
				uint32_t &dst = buf[static_cast<size_t>(ty) * w + tx];
				const uint32_t a = 225;
				const uint32_t rb = ((src & 0xff00ff) * a + (dst & 0xff00ff) * (255 - a)) >> 8;
				const uint32_t g = ((src & 0x00ff00) * a + (dst & 0x00ff00) * (255 - a)) >> 8;
				dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
			}
		}
	}
}

bool ControlsBarLetters()
{
	return g_barLetters;
}

void ControlsSetBarLetters(bool on)
{
	g_barLetters = on;
	SaveControls();
}

int ControlsZoomBoxMode()
{
	return g_zoomBoxMode;
}

void ControlsSetZoomBoxMode(int mode)
{
	g_zoomBoxMode = mode >= 0 && mode < kZoomBoxModes ? mode : kZoomBoxCtrl;
	SaveControls();
}

unsigned ControlsZoomBoxVk()
{
	switch (g_zoomBoxMode) {
	case kZoomBoxAlt: return VK_MENU;
	case kZoomBoxShift: return VK_SHIFT;
	case kZoomBoxOff: return 0;
	default: return VK_CONTROL;
	}
}

unsigned ControlsZoomBoxAddVk()
{
	return g_zoomBoxMode == kZoomBoxShift ? VK_CONTROL : VK_SHIFT;
}

std::string ControlsExport()
{
	const std::string snap = ControlsSnapshot();
	return snap.size() > 1 ? snap.substr(1) : std::string();
}

bool ControlsImport(const std::string &text)
{
	if (text.find("[Keys]") == std::string::npos && text.find("[Commands]") == std::string::npos && text.find("[General]") == std::string::npos &&
		!LegacyLooksLikeControls(text)) {
		return false;
	}
	ControlsRestore("1" + text);
	return true;
}

bool ControlsEatChar(unsigned msg, unsigned wp)
{
	if (msg == WM_KEYDOWN) {
		g_eatChar = false;
		if (wp >= 256 || wp == VK_RETURN || InEditor() || !*reinterpret_cast<void **>(addr::kGame) || !MapViewFocused()) return false;
		if (!MapVirtualKeyA(wp, MAPVK_VK_TO_CHAR)) return false;
		const Decision d = g_downSeen[wp] ? g_down[wp] : Decide(static_cast<unsigned short>(wp), CurrentMods());
		g_eatChar = d.act == kTranslate && d.target == VK_RETURN;
		return false;
	}
	if (msg == WM_CHAR && g_eatChar) {
		g_eatChar = false;
		return true;
	}
	return false;
}
