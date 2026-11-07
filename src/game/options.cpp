// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstdint>

#include "game/options.h"
#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "hooks/displaymode.h"
#include "mods/lang.h"
#include "core/log.h"
#include "mods/mods.h"
#include "mods/updater.h"
#include "hooks/tweaks.h"
#include "i18n/tr.h"

#include "addr/engine.h"
#include "addr/media.h"
#include "addr/ui.h"
#include "core/uiport.h"

namespace {
const int kIdWindowed = 0x1101;
const int kIdVSync = 0x1102;
const int kIdTrapMouse = 0x1104;
const int kIdMute = 0x1105;
const int kIdPerf = 0x1106;
const int kIdNet = 0x111A;
const int kIdAutosave = 0x1109;
const int kIdAutosaveCombo = 0x110a;
const int kIdAutosaveSlots = 0x1116;
const int kIdSaveOnExit = 0x1119;
const int kIdAlerts = 0x1113;
const int kIdOk = 0x1002;
const int kResolutionSlot = 0xd;
const int kIdApply = 0x1114;
const int kIdReset = 0x1115;
const int kIdControls = 0x111B;
const int kIdLanguage = 0x111C;
const int kIdMonitor = 0x111D;
const int kIdUpdate = 0x111E;
std::vector<std::string> g_langCodes;
const int kGameDefaults[13] = {0, 0, 0, 1, 1, 1, 1, 1, 14, 50, 50, 50, 50};
int g_brightOriginal = 100;
bool g_useDefaults = false;
const int kIdBrightCombo = 0x110c;
const int kIdMovieScroll = 0x1110;
const int kIdMovieMute = 0x1111;
const int kIdLargeAddress = 0x110d;

const int kAutosaveMinutes[] = {5, 10, 15, 20, 30, 45, 60};
const char *kAutosaveLabels[] = {"5 minutes", "10 minutes", "15 minutes", "20 minutes", "30 minutes", "45 minutes",
	"60 minutes"};
const int kAutosaveSlotValues[] = {1, 2, 3, 5, 10};
const char *kAutosaveSlotLabels[] = {"1 copy", "2 copies", "3 copies", "5 copies", "10 copies"};
const char *kUpdateLabels[] = {"Notify", "Automatic", "Never"};
const int kBrightMin = 80, kBrightMax = 130;

typedef int(__fastcall *ComboGetSelected_t)(void *combo);
typedef void(__fastcall *ComboSetSelected_t)(void *combo, void *edx, int index, int notify);
typedef void *(__fastcall *ItemListInsert_t)(void *list, void *edx, const char *text, void *image, int d1, int d2,
	unsigned pos, int redraw);
typedef int(__fastcall *ItemListRemove_t)(void *list, void *edx, unsigned from, int count, int redraw);
typedef char *(__cdecl *StrDupPool_t)(const char *);

const unsigned kMsgButtonClick = 0x12345604;

typedef void(__fastcall *PopulateControls_t)(void *dlg);
typedef unsigned(__fastcall *OnMessage_t)(void *dlg, void *edx, int *msg);
typedef void *(__fastcall *GetChild_t)(void *dlg, void *edx, int id, const char *cls);
typedef void(__fastcall *SetValue_t)(void *ctrl, void *edx, int value, int notify);
typedef int(__fastcall *GetId_t)(void *ctrl);

PopulateControls_t g_origPopulate = nullptr;
OnMessage_t g_origOnMessage = nullptr;

void *FindCheck(void *dlg, int id)
{
	void **vt = *reinterpret_cast<void ***>(dlg);
	auto getChild = reinterpret_cast<GetChild_t>(vt[0x7c / 4]);
	return getChild(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCUIButton));
}

void SetCheck(void *dlg, int id, bool value)
{
	void *ctrl = FindCheck(dlg, id);
	if (ctrl) {
		void **vt = *reinterpret_cast<void ***>(ctrl);
		reinterpret_cast<SetValue_t>(vt[0x70 / 4])(ctrl, nullptr, value ? 1 : 0, 1);
	}
}

bool GetCheck(void *dlg, int id, bool fallback)
{
	void *ctrl = FindCheck(dlg, id);
	return ctrl ? *reinterpret_cast<short *>(static_cast<char *>(ctrl) + 0x7e) == 1 : fallback;
}

void *FindScroll(void *dlg, int id)
{
	void **vt = *reinterpret_cast<void ***>(dlg);
	auto getChild = reinterpret_cast<GetChild_t>(vt[0x7c / 4]);
	return getChild(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCUIScroll));
}

void SetScroll(void *dlg, int id, int value)
{
	typedef void(__fastcall * SetRange_t)(void *, void *, int, int, int, int, int);
	void *scroll = FindScroll(dlg, id);
	if (!scroll) return;
	reinterpret_cast<SetRange_t>(addr::kScrollSetRange)(scroll, nullptr, 0, 100, 10, 1, 0);
	reinterpret_cast<SetValue_t>((*reinterpret_cast<void ***>(scroll))[0x70 / 4])(scroll, nullptr, value, 1);
}

void SetScrollRange(void *dlg, int id, int lo, int hi, int value)
{
	typedef void(__fastcall * SetRange_t)(void *, void *, int, int, int, int, int);
	void *scroll = FindScroll(dlg, id);
	if (!scroll) return;
	reinterpret_cast<SetRange_t>(addr::kScrollSetRange)(scroll, nullptr, lo, hi, 10, 5, 0);
	reinterpret_cast<SetValue_t>((*reinterpret_cast<void ***>(scroll))[0x70 / 4])(scroll, nullptr, value, 1);
}

int GetScroll(void *dlg, int id, int fallback)
{
	void *scroll = FindScroll(dlg, id);
	return scroll ? *reinterpret_cast<int *>(static_cast<char *>(scroll) + 0x60) : fallback;
}

void *FindCombo(void *dlg, int id)
{
	void **vt = *reinterpret_cast<void ***>(dlg);
	auto getChild = reinterpret_cast<GetChild_t>(vt[0x7c / 4]);
	return getChild(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCUIComboBox));
}

void FillCombo(void *dlg, int id, const std::vector<std::string> &items, int selected)
{
	void *combo = FindCombo(dlg, id);
	void *list = combo ? *reinterpret_cast<void **>(static_cast<char *>(combo) + 0x64) : nullptr;
	if (!list) {
		return;
	}
	char *l = static_cast<char *>(list);
	*reinterpret_cast<uintptr_t *>(l + 0x134) = addr::kFreePoolString;
	const unsigned count = *reinterpret_cast<unsigned *>(l + 0x58);
	if (count) {
		reinterpret_cast<ItemListRemove_t>(addr::kItemListRemove)(list, nullptr, 0, count, 0);
	}
	for (const std::string &item : items) {
		char *text = reinterpret_cast<StrDupPool_t>(addr::kStrDupPool)(item.c_str());
		reinterpret_cast<ItemListInsert_t>(addr::kItemListInsert)(list, nullptr, text, nullptr, 0, 0, 0xffffffffu, 1);
	}
	reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(combo, nullptr, selected, 0);
}

int GetComboIndex(void *dlg, int id)
{
	void *combo = FindCombo(dlg, id);
	return combo ? reinterpret_cast<ComboGetSelected_t>(addr::kComboGetSelected)(combo) : -1;
}

template <size_t N> int NearestIndex(const int (&values)[N], int value)
{
	int best = 0;
	for (size_t i = 0; i < N; i++) {
		if (abs(values[i] - value) < abs(values[best] - value)) best = static_cast<int>(i);
	}
	return best;
}

template <size_t N> std::vector<std::string> Items(const char *(&labels)[N])
{
	std::vector<std::string> items;
	for (size_t i = 0; i < N; i++) items.push_back(Tr(labels[i]));
	return items;
}

std::atomic<void *> g_openDialog{nullptr};
PopulateControls_t g_origDtor = nullptr;

void __fastcall HookDtor(void *dlg)
{
	void *expected = dlg;
	g_openDialog.compare_exchange_strong(expected, nullptr);
	g_origDtor(dlg);
}

void __fastcall HookPopulateControls(void *dlg)
{
	g_origPopulate(dlg);
	g_openDialog = dlg;
	static const Config kFactory;
	const Config &c = g_useDefaults ? kFactory : GetConfig();
	if (!g_useDefaults) g_brightOriginal = c.brightness;
	SetCheck(dlg, kIdWindowed, c.windowed);
	SetCheck(dlg, kIdVSync, c.vsync);
	SetCheck(dlg, kIdTrapMouse, c.trapMouse);
	SetCheck(dlg, kIdMute, c.muteInBackground);
	SetCheck(dlg, kIdPerf, c.showPerf);
	SetCheck(dlg, kIdNet, c.showNet);
	SetCheck(dlg, kIdAutosave, c.autosave);
	SetCheck(dlg, kIdAlerts, c.onlineAlerts);
	SetCheck(dlg, kIdSaveOnExit, c.autosaveOnExit);
	SetCheck(dlg, kIdLargeAddress, ModsExeIsLargeAddressAware());
	FillCombo(dlg, kIdAutosaveCombo, Items(kAutosaveLabels), NearestIndex(kAutosaveMinutes, c.autosaveMinutes));
	FillCombo(dlg, kIdAutosaveSlots, Items(kAutosaveSlotLabels), NearestIndex(kAutosaveSlotValues, c.autosaveSlots));
	FillCombo(dlg, kIdUpdate, Items(kUpdateLabels), UpdaterModeGet());
	{
		const std::vector<std::string> monitors = DisplayMonitorLabels();
		FillCombo(dlg, kIdMonitor, monitors, c.monitor < static_cast<int>(monitors.size()) ? c.monitor : 0);
	}
	SetScrollRange(dlg, kIdBrightCombo, kBrightMin, kBrightMax, c.brightness);
	SetScroll(dlg, kIdMovieScroll, c.movieVolume);
	SetCheck(dlg, kIdMovieMute, !c.movieMute);
	if (!g_useDefaults) {
		const std::string next = LangNext();
		std::vector<std::string> names;
		int selected = 0;
		g_langCodes.clear();
		for (const GameLanguage &l : LangAvailable()) {
			if (l.code == next) selected = static_cast<int>(g_langCodes.size());
			g_langCodes.push_back(l.code);
			names.push_back(l.name);
		}
		FillCombo(dlg, kIdLanguage, names, selected);
	}
	if (g_useDefaults) {
		struct { int id; int value; } kChecks[] = {{0x101b, 0}, {0x1007, 0}, {0x1008, 0}, {0x100b, 1}, {0x1010, 1}, {0x1011, 1}, {0x1015, 1}, {0x1016, 1}};
		for (const auto &k : kChecks) SetCheck(dlg, k.id, k.value != 0);
		struct { int id; int value; } kScrolls[] = {{0x101d, kGameDefaults[8]}, {0x101e, kGameDefaults[9]}, {0x100f, kGameDefaults[11]}, {0x1013, kGameDefaults[10]}, {0x1018, kGameDefaults[12]}};
		for (const auto &k : kScrolls) {
			void *scroll = FindScroll(dlg, k.id);
			if (!scroll) continue;
			reinterpret_cast<void(__fastcall *)(void *, void *, int, int, int, int, int)>(addr::kScrollSetRange)(scroll, nullptr, 1, 100, 10, 1, 0);
			reinterpret_cast<SetValue_t>((*reinterpret_cast<void ***>(scroll))[0x70 / 4])(scroll, nullptr, k.value, 1);
		}
	}
}

void ApplyModOptions(void *dlg)
{
	Config &c = MutableConfig();
	const bool wasWindowed = c.windowed;
	const int wasMonitor = c.monitor;
	c.windowed = GetCheck(dlg, kIdWindowed, c.windowed);
	c.vsync = GetCheck(dlg, kIdVSync, c.vsync);
	c.trapMouse = GetCheck(dlg, kIdTrapMouse, c.trapMouse);
	c.muteInBackground = GetCheck(dlg, kIdMute, c.muteInBackground);
	c.showPerf = GetCheck(dlg, kIdPerf, c.showPerf);
	c.showNet = GetCheck(dlg, kIdNet, c.showNet);
	c.autosave = GetCheck(dlg, kIdAutosave, c.autosave);
	c.onlineAlerts = GetCheck(dlg, kIdAlerts, c.onlineAlerts);
	c.autosaveOnExit = GetCheck(dlg, kIdSaveOnExit, c.autosaveOnExit);
	int sel = GetComboIndex(dlg, kIdAutosaveCombo);
	if (sel >= 0 && sel < static_cast<int>(sizeof(kAutosaveMinutes) / sizeof(kAutosaveMinutes[0]))) {
		c.autosaveMinutes = kAutosaveMinutes[sel];
	}
	sel = GetComboIndex(dlg, kIdAutosaveSlots);
	if (sel >= 0 && sel < static_cast<int>(sizeof(kAutosaveSlotValues) / sizeof(kAutosaveSlotValues[0]))) {
		c.autosaveSlots = kAutosaveSlotValues[sel];
	}
	sel = GetComboIndex(dlg, kIdUpdate);
	if (sel >= 0 && sel < 3) UpdaterModeSet(sel);
	sel = GetComboIndex(dlg, kIdMonitor);
	if (sel >= 0 && sel < static_cast<int>(DisplayMonitorLabels().size())) c.monitor = sel;
	c.brightness = (std::max)(kBrightMin, (std::min)(kBrightMax, GetScroll(dlg, kIdBrightCombo, c.brightness)));
	c.movieVolume = GetScroll(dlg, kIdMovieScroll, c.movieVolume);
	c.movieMute = !GetCheck(dlg, kIdMovieMute, !c.movieMute);
	SaveConfig();
	LogI("options", "mod options: window=%d vsync=%d brightness=%d trap=%d autosave=%d every %d min",
		c.windowed, c.vsync, c.brightness, c.trapMouse, c.autosave, c.autosaveMinutes);
	DisplayApplySettings(wasWindowed != c.windowed || wasMonitor != c.monitor);
	TweaksApplySmoothScroll();
	g_brightOriginal = c.brightness;
	sel = GetComboIndex(dlg, kIdLanguage);
	if (sel >= 0 && sel < static_cast<int>(g_langCodes.size()) && g_langCodes[sel] != LangNext()) {
		const std::string code = g_langCodes[sel];
		if (LangChoose(code)) {
			UiAskRestart(Tr("The game will restart to change the language to ") + LangDisplayName(code) + ".");
		}
	}
}

unsigned __fastcall HookOnMessage(void *dlg, void *edx, int *msg)
{
	const unsigned kind = static_cast<unsigned>(msg[0x10 / 4]);
	if (kind >= 0x23432301 && kind <= 0x23432304) {
		const int v = (std::max)(kBrightMin, (std::min)(kBrightMax, GetScroll(dlg, kIdBrightCombo, GetConfig().brightness)));
		if (v != GetConfig().brightness) MutableConfig().brightness = v;
	}
	if (static_cast<unsigned>(msg[0x10 / 4]) == kMsgButtonClick) {
		void *sender = reinterpret_cast<void *>(msg[0xc / 4]);
		int id = sender ? reinterpret_cast<GetId_t>((*reinterpret_cast<void ***>(sender))[0x1c / 4])(sender) : 0;
		if (id == 0x1003 || id == 0x1006) MutableConfig().brightness = g_brightOriginal;
		if (id == kIdApply) {
			reinterpret_cast<void(__fastcall *)(void *)>(addr::kReadControls)(dlg);
			const int *chosen = reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x60);
			void *ok = FindCheck(dlg, kIdOk);
			if (ok && chosen[kResolutionSlot] != *reinterpret_cast<int *>(addr::kCurrentResolutionIndex)) {
				msg[0xc / 4] = reinterpret_cast<int>(ok);
				id = kIdOk;
			}
		}
		if (id == kIdApply) {
			ApplyModOptions(dlg);
			reinterpret_cast<void(__fastcall *)(void *)>(addr::kReadControls)(dlg);
			reinterpret_cast<int(__cdecl *)(int *)>(addr::kApplyGameOptions)(reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x60));
			LogI("options", "applied without closing");
			return 0;
		}
		if (id == kIdControls) {
			UiOpenControls();
			return 0;
		}
		if (id == kIdReset) {
			int *block = reinterpret_cast<int *>(static_cast<char *>(dlg) + 0x60);
			for (int i = 0; i < 13; i++) block[i] = kGameDefaults[i];
			g_useDefaults = true;
			HookPopulateControls(dlg);
			g_useDefaults = false;
			LogI("options", "controls reset to the factory values");
			return 0;
		}
		if (id == kIdOk || id == 0x1005) {
			ApplyModOptions(dlg);
			const bool laaNow = ModsExeIsLargeAddressAware();
			const bool laaWanted = laaNow;
			const unsigned r = g_origOnMessage(dlg, edx, msg);
			if (laaWanted != laaNow) {
				MutableConfig().largeAddress = laaWanted;
				SaveConfig();
				UiAskRestart(laaWanted ? "The game will restart to use up to 4 GB of memory. A copy of the original executable is kept as gbr.exe.no4gb."
										  : "The game will restart to return to the original 2 GB memory limit.",
					laaWanted ? 1 : 0);
			}
			return r;
		}
	}
	return g_origOnMessage(dlg, edx, msg);
}

bool HookAt(uintptr_t addr, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr)) == MH_OK) {
		return true;
	}
	LogE("options", "could not hook %s", what);
	return false;
}

}

void *OptionsDialogOpen()
{
	return g_openDialog;
}

bool OptionsReadCheck(int id, bool fallback)
{
	void *dlg = g_openDialog;
	return dlg ? GetCheck(dlg, id, fallback) : fallback;
}

void OptionsClick(int id)
{
	void *dlg = g_openDialog;
	void *button = dlg ? FindCheck(dlg, id) : nullptr;
	if (!button) return;
	typedef void(__fastcall * SendNotify_t)(void *ctrl, void *edx, int target, unsigned msg, int id, int value, int extra);
	char *b = static_cast<char *>(button);
	reinterpret_cast<SendNotify_t>((*reinterpret_cast<void ***>(button))[0x6c / 4])(button, nullptr, *reinterpret_cast<int *>(b + 0x34), kMsgButtonClick,
		*reinterpret_cast<int *>(b + 0x30), *reinterpret_cast<short *>(b + 0x7e), 0);
}

void OptionsWriteCheck(int id, bool value)
{
	void *dlg = g_openDialog;
	if (!dlg) return;
	SetCheck(dlg, id, value);
	OptionsClick(id);
}

int OptionsReadScroll(int id, int fallback)
{
	void *dlg = g_openDialog;
	return dlg ? GetScroll(dlg, id, fallback) : fallback;
}

void OptionsWriteScroll(int id, int value)
{
	void *dlg = g_openDialog;
	void *scroll = dlg ? FindScroll(dlg, id) : nullptr;
	if (!scroll) return;
	if (id == kIdBrightCombo) {
		value = (std::max)(kBrightMin, (std::min)(kBrightMax, value));
		MutableConfig().brightness = value;
	}
	reinterpret_cast<SetValue_t>((*reinterpret_cast<void ***>(scroll))[0x70 / 4])(scroll, nullptr, value, 1);
	// Setting the value sends no message to the dialog, and reading the controls is what applies the sound at once.
	reinterpret_cast<void(__fastcall *)(void *)>(addr::kReadControls)(dlg);
}

bool OptionsControlEnabled(int id)
{
	void *dlg = g_openDialog;
	if (!dlg) return false;
	void *control = reinterpret_cast<GetChild_t>((*reinterpret_cast<void ***>(dlg))[0x7c / 4])(dlg, nullptr, id, *reinterpret_cast<const char **>(addr::kClassNameCWindow));
	if (!control) return true;
	return !reinterpret_cast<bool(__fastcall *)(void *, void *, unsigned)>(addr::kWindowHasFlags)(control, nullptr, 0x200);
}

int OptionsReadCombo(int id)
{
	void *dlg = g_openDialog;
	return dlg ? GetComboIndex(dlg, id) : -1;
}

std::vector<std::string> OptionsComboItems(int id)
{
	std::vector<std::string> out;
	void *dlg = g_openDialog;
	void *combo = dlg ? FindCombo(dlg, id) : nullptr;
	char *list = combo ? *reinterpret_cast<char **>(static_cast<char *>(combo) + 0x64) : nullptr;
	if (!list) return out;
	const unsigned count = *reinterpret_cast<unsigned *>(list + 0x58);
	const char *items = *reinterpret_cast<const char **>(list + 0x94);
	for (unsigned i = 0; items && i < count && i < 128; i++) {
		const char *text = *reinterpret_cast<const char *const *>(items + i * 0x14);
		out.push_back(text ? text : "");
	}
	return out;
}

void OptionsWriteCombo(int id, int index)
{
	void *dlg = g_openDialog;
	void *combo = dlg ? FindCombo(dlg, id) : nullptr;
	if (combo) reinterpret_cast<ComboSetSelected_t>(addr::kComboSetSelected)(combo, nullptr, index, 1);
}

void InstallOptionsHooks(bool knownExe)
{
	if (!knownExe) {
		return;
	}
	bool ok = HookAt(addr::kPopulateControls, reinterpret_cast<void *>(&HookPopulateControls),
				  reinterpret_cast<void **>(&g_origPopulate), "CVXUIOptions::PopulateControls") &&
		HookAt(addr::kOptionsOnMessage, reinterpret_cast<void *>(&HookOnMessage), reinterpret_cast<void **>(&g_origOnMessage),
			"CVXUIOptions::OnMessage") &&
		HookAt(addr::kOptionsDtor, reinterpret_cast<void *>(&HookDtor), reinterpret_cast<void **>(&g_origDtor), "CVXUIOptions destructor");
	LogAt(ok ? kLogInfo : kLogError, "options", "game menu %s", ok ? "connected to the mod's options" : "not connected");
}
