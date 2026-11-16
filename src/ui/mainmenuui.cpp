// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/mainmenuui.h"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "game/gamemenu.h"
#include "game/gametext.h"
#include "i18n/tr.h"
#include "net/leagueclient.h"
#include "ui/battlesui.h"
#include "ui/conquestui.h"
#include "ui/controlsui.h"
#include "ui/creditsui.h"
#include "ui/mainmenuui_view.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/optionsui.h"
#include "ui/profileui.h"
#include "ui/savesui.h"
#include "ui/support.h"
#include "ui/updateui.h"

using namespace ui;
using namespace mainmenuui;

namespace {

// The buttons of the game's mainmenu.ini: their id, their English text and the section that gives the context of its translation.
struct Spec {
	int id;
	const char *english, *section;
};

const Spec kMainSpecs[] = {
	{0x1003, "Multiplayer", "MultiPlayer"},
	{0x1011, "Change player", "ChangePlayer"},
	{0x1008, "Editor", "Editor"},
	{0x1041, "Additional content", "UpdateBtn"},
	{0x1005, "Options", "Options"},
	{0x1006, "Credits", "Credits"},
	{0x1007, "Quit", "Quit"},
};

const Spec kSingleSpecs[] = {
	{0x1002, "Tutorial", "Tutorial"},
	{0x1009, "Great Battles", "Adventure"},
	{0x1060, "Conquest", "Conquest"},
	{0x1010, "Single player", "SinglePlayer"},
	{0x1004, "Load game", "LoadGame"},
};

const int kIdAdditionalContent = 0x1041, kIdMultiplayer = 0x1003;
// A screen asked for a group's submenu to be open when the menu comes back (the multiplayer screen's Back).
std::atomic<int> g_groupWanted{1};
const DWORD kLabelsEveryMs = 1000;

struct State {
	void *dlg = nullptr;
	// The buttons the mod hid, told apart from those the game hides itself.
	std::set<int> hidden;
	bool shown = false;
	View view;
	std::vector<Hit> hits;
	std::vector<std::pair<int, std::string>> labels;
	DWORD labelsAt = 0;
};

std::mutex g_lock;
State g_s;
// Only the game thread touches these.
bool g_failed = false, g_swallowUp = false, g_enterArmed = false, g_escArmed = false;

void Fail(void *dlg, const char *why)
{
	g_failed = true;
	LogW("ui", "main menu: %s; the game's own buttons stay", why);
	std::set<int> hidden;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		hidden.swap(g_s.hidden);
		g_s.shown = false;
		g_s.hits.clear();
	}
	if (!dlg) return;
	for (int id : hidden) NativeButtonSetVisible(dlg, id, true);
}

// Screens of the mod or the game that take the input while the main menu stays under them.
bool OtherScreenOnTop()
{
	return !MpUiMainMenuVisible() || MpUiIsOpen() || ModUiIsOpen() || SavesUiIsOpen() || ControlsUiNeedsDraw() || OptionsUiNeedsDraw() || ProfileUiNeedsDraw() ||
		BattlesUiNeedsDraw() || ConquestUiNeedsDraw() || CreditsUiNeedsDraw() || SupportPanelOpen() || UpdateUiPanelOpen();
}

void *ShownDialog()
{
	void *dlg = MpUiMainMenu();
	return dlg && NativeWindowVisible(dlg) ? dlg : nullptr;
}

std::string LabelOf(const Spec &spec, const std::vector<std::pair<int, std::string>> &labels)
{
	for (const auto &label : labels) {
		if (label.first == spec.id) return label.second;
	}
	return spec.english;
}

// Hides the game's buttons that are still shown and returns the rows the menu draws: a button the game itself hid has none.
template <size_t N>
std::vector<Item> TakeButtons(void *dlg, const Spec (&specs)[N], std::set<int> &hidden, const std::vector<std::pair<int, std::string>> &labels)
{
	std::vector<Item> items;
	for (const Spec &spec : specs) {
		const NativeButtonState state = NativeButton(dlg, spec.id);
		if (!state.exists) continue;
		if (state.visible) {
			NativeButtonSetVisible(dlg, spec.id, false);
			hidden.insert(spec.id);
		}
		else if (!hidden.count(spec.id)) {
			continue;
		}
		Item item;
		item.id = spec.id;
		item.label = LabelOf(spec, labels);
		item.enabled = state.enabled;
		items.push_back(item);
	}
	return items;
}

// The texts the game gives its buttons, with the same keys it translates them by (and the mod renames some of them).
template <size_t N>
void AddLabels(const Spec (&specs)[N], std::vector<std::pair<int, std::string>> &labels)
{
	for (const Spec &spec : specs) labels.emplace_back(spec.id, GameText(spec.english, std::string("/Menu/mainmenu.ini:") + spec.section + ":Text"));
}

std::vector<std::pair<int, std::string>> ReadLabels()
{
	std::vector<std::pair<int, std::string>> labels;
	AddLabels(kMainSpecs, labels);
	AddLabels(kSingleSpecs, labels);
	return labels;
}

int FirstEnabled(const std::vector<Item> &items, int from, int step)
{
	const int count = static_cast<int>(items.size());
	for (int i = from; i >= 0 && i < count; i += step) {
		if (items[i].enabled) return i;
	}
	return -1;
}

int GroupIndex(const View &v)
{
	for (size_t i = 0; i < v.main.size(); i++) {
		if (v.main[i].id == v.openGroup) return static_cast<int>(i);
	}
	return -1;
}

// Runs on the game thread, which owns the game's dialog: hides its buttons and reads what the menu shows. While the game shows the
// dialog only the buttons are hidden: the other screens are not asked anything from inside the game's call.
void Sync(void *dlg, bool onShow)
{
	if (g_failed) return;
	if (!LoadBrandArt()) {
		Fail(dlg, "the brand art in imperivm-resurrected\\brand is missing or unreadable");
		return;
	}
	std::set<int> hidden;
	std::vector<std::pair<int, std::string>> labels;
	bool readLabels = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (dlg != g_s.dlg) {
			g_s.dlg = dlg;
			g_s.hidden.clear();
			g_s.labels.clear();
			g_s.view = View();
		}
		hidden = g_s.hidden;
		labels = g_s.labels;
		readLabels = labels.empty() || GetTickCount() - g_s.labelsAt >= kLabelsEveryMs;
	}
	if (readLabels) labels = ReadLabels();
	std::vector<Item> sub = TakeButtons(dlg, kSingleSpecs, hidden, labels);
	std::vector<Item> main = TakeButtons(dlg, kMainSpecs, hidden, labels);
	if (main.empty() && sub.empty()) {
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.hidden = hidden;
		}
		Fail(dlg, "none of the game's buttons were found in its dialog");
		return;
	}
	if (onShow) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.hidden = hidden;
		if (readLabels) {
			g_s.labels = labels;
			g_s.labelsAt = GetTickCount();
		}
		return;
	}
	if (!sub.empty()) {
		Item group;
		group.id = kGroupSingle;
		group.label = Tr("Single-player");
		main.insert(main.begin(), group);
	}
	// Multiplayer opens its own submenu: the rooms, the ranked mode and the tournaments.
	std::vector<Item> multi;
	for (Item &item : main) {
		if (item.id != kIdMultiplayer) continue;
		item.id = kGroupMulti;
		const struct {
			int id;
			const char *label;
		} rows[] = {{kMultiRooms, "Rooms"}, {kMultiRanked, "Ranked"}, {kMultiTournaments, "Tournaments"}};
		for (const auto &row : rows) {
			Item sub2;
			sub2.id = row.id;
			sub2.label = Tr(row.label);
			sub2.enabled = item.enabled;
			multi.push_back(sub2);
		}
	}
	const int updates = ModUiUpdatesAvailable();
	for (Item &item : main) {
		if (item.id == kIdAdditionalContent) item.badge = updates;
	}
	const bool interactive = !OtherScreenOnTop();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hidden = hidden;
	if (readLabels) {
		g_s.labels = labels;
		g_s.labelsAt = GetTickCount();
	}
	View &v = g_s.view;
	v.main = std::move(main);
	v.subSingle = std::move(sub);
	v.subMulti = std::move(multi);
	if (v.openGroup == kGroupMulti && v.subMulti.empty()) v.openGroup = kGroupSingle;
	v.sub = v.openGroup == kGroupMulti ? v.subMulti : v.subSingle;
	v.groupCaption = Tr(v.openGroup == kGroupMulti ? "Multiplayer" : "Single-player");
	v.tagline = Tr("History never ends.");
	if (v.sub.empty()) v.subOpen = false;
	if (v.focusMain >= static_cast<int>(v.main.size())) v.focusMain = -1;
	if (v.focusSub >= static_cast<int>(v.sub.size())) v.focusSub = -1;
	if (!interactive && v.interactive) {
		v.subOpen = false;
		v.focusSub = -1;
		v.keyboard = false;
	}
	v.interactive = interactive;
	// Back from the multiplayer screen leaves its submenu open, as the single player's screens do with theirs.
	if (interactive) {
		const int wanted = g_groupWanted.exchange(1);
		if (wanted != 1) {
			v.openGroup = wanted;
			v.sub = wanted == kGroupMulti ? v.subMulti : v.subSingle;
			v.groupCaption = Tr(wanted == kGroupMulti ? "Multiplayer" : "Single-player");
			if (!v.sub.empty()) {
				v.subOpen = true;
				v.subChangedAt = GetTickCount();
				v.focusMain = GroupIndex(v);
				v.focusSub = -1;
			}
		}
	}
	g_s.shown = true;
}

void Click(int id)
{
	void *dlg = ShownDialog();
	if (!dlg || !id) return;
	// A row of the multiplayer group: the game's Multiplayer button, and the screen opens on that section.
	if (id == kMultiRooms || id == kMultiRanked || id == kMultiTournaments) {
		MpUiOpenSection(id == kMultiRanked ? MpSection::Ranked : id == kMultiTournaments ? MpSection::Tournaments : MpSection::Rooms);
		id = kIdMultiplayer;
	}
	// The game's dialog can close itself and call back into the mod: no lock here.
	NativeButtonClick(dlg, id);
}

void OpenGroup(View &v, int group, bool focusFirst)
{
	if (!v.subOpen || v.openGroup != group) v.subChangedAt = GetTickCount();
	v.openGroup = group;
	v.sub = group == kGroupMulti ? v.subMulti : v.subSingle;
	v.groupCaption = Tr(group == kGroupMulti ? "Multiplayer" : "Single-player");
	v.subOpen = true;
	v.focusMain = GroupIndex(v);
	v.focusSub = focusFirst ? FirstEnabled(v.sub, 0, 1) : -1;
}

void CloseGroup(View &v)
{
	if (v.subOpen) v.subChangedAt = GetTickCount();
	v.subOpen = false;
	v.focusSub = -1;
}

// Returns the game button to click, or 0.
int Activate(View &v)
{
	if (v.subOpen && v.focusSub >= 0 && v.focusSub < static_cast<int>(v.sub.size())) {
		const Item &item = v.sub[v.focusSub];
		if (!item.enabled) return 0;
		CloseGroup(v);
		return item.id;
	}
	if (v.focusMain < 0 || v.focusMain >= static_cast<int>(v.main.size())) return 0;
	const Item &item = v.main[v.focusMain];
	if (!item.enabled) return 0;
	if (IsGroup(item.id)) {
		OpenGroup(v, item.id, true);
		return 0;
	}
	return item.id;
}

void MoveFocus(View &v, int step)
{
	v.keyboard = true;
	const bool inSub = v.subOpen && v.focusSub >= 0;
	const std::vector<Item> &items = inSub ? v.sub : v.main;
	int &focus = inSub ? v.focusSub : v.focusMain;
	const int count = static_cast<int>(items.size());
	if (!count) return;
	int next = focus < 0 ? FirstEnabled(items, step > 0 ? 0 : count - 1, step) : FirstEnabled(items, focus + step, step);
	if (next < 0) next = FirstEnabled(items, step > 0 ? 0 : count - 1, step);
	if (next >= 0) focus = next;
}

bool HandleKey(UINT msg, WPARAM wp, int &clickId)
{
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	if (msg == WM_KEYDOWN) {
		switch (wp) {
		case VK_UP:
		case VK_DOWN:
			MoveFocus(v, wp == VK_DOWN ? 1 : -1);
			return true;
		case VK_RIGHT:
			if (v.subOpen || v.focusMain < 0 || v.focusMain >= static_cast<int>(v.main.size()) || !IsGroup(v.main[v.focusMain].id)) return false;
			v.keyboard = true;
			OpenGroup(v, v.main[v.focusMain].id, true);
			return true;
		case VK_LEFT:
			if (!v.subOpen) return false;
			v.keyboard = true;
			CloseGroup(v);
			return true;
		case VK_RETURN:
			if (!v.keyboard || (v.focusMain < 0 && v.focusSub < 0)) return false;
			g_enterArmed = true;
			return true;
		case VK_ESCAPE:
			if (!v.subOpen) return false;
			g_escArmed = true;
			return true;
		default: return false;
		}
	}
	// On the release, as the game does: acting on the press would leave the release to the screen it opens.
	if (wp == VK_RETURN && g_enterArmed) {
		g_enterArmed = false;
		clickId = Activate(v);
		return true;
	}
	if (wp == VK_ESCAPE && g_escArmed) {
		g_escArmed = false;
		CloseGroup(v);
		return true;
	}
	return false;
}

bool HandleClick(UINT msg, POINT at, int &clickId)
{
	std::lock_guard<std::mutex> lock(g_lock);
	View &v = g_s.view;
	v.keyboard = false;
	const Hit *hit = TopHitAt(g_s.hits, at);
	if (!hit) {
		if (!v.subOpen) return false;
		CloseGroup(v);
		return true;
	}
	if (hit->action == kActSub && hit->arg >= 0 && hit->arg < static_cast<int>(v.sub.size())) {
		clickId = v.sub[hit->arg].enabled ? v.sub[hit->arg].id : 0;
		CloseGroup(v);
		return true;
	}
	if (hit->action != kActMain || hit->arg < 0 || hit->arg >= static_cast<int>(v.main.size())) return false;
	const Item &item = v.main[hit->arg];
	v.focusMain = hit->arg;
	if (!IsGroup(item.id)) {
		CloseGroup(v);
		clickId = item.enabled ? item.id : 0;
	}
	// A double click is two presses: the second one keeps the submenu open.
	else if (v.subOpen && v.openGroup == item.id && msg == WM_LBUTTONDOWN) CloseGroup(v);
	else if (item.enabled) OpenGroup(v, item.id, false);
	return true;
}

}

void MainMenuUiGameTick()
{
	void *dlg = ShownDialog();
	if (!dlg) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.shown = false;
		g_s.hits.clear();
		return;
	}
	Sync(dlg, false);
}

void MainMenuUiShown(void *dlg)
{
	// Before the dialog's first frame, so the game's buttons never show.
	if (dlg) Sync(dlg, true);
	// The league account's standing, frame and title read ahead, so Statistics opens with them in place (at most once a minute).
	LeagueMeRefresh();
}

void MainMenuUiDestroyed(void *dlg)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (dlg != g_s.dlg) return;
	g_s = State();
}

void MainMenuUiOpenMultiplayerGroup()
{
	g_groupWanted = kGroupMulti;
}

bool MainMenuUiNeedsDraw()
{
	bool waiting = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		// Under another screen only the background shows: the room and other windows don't cover the whole menu.
		if (g_s.shown && g_s.view.interactive) return true;
		waiting = g_s.shown;
	}
	// Whether the menu is back on top is known on the game thread, at the game's next frame; with the menu still the game draws none
	// until the mouse moves, so the menu stayed blank after a screen closed. The game thread is woken to look again.
	static DWORD wokeAt = 0;
	const DWORD now = GetTickCount();
	if (waiting && now - wokeAt >= 200) {
		wokeAt = now;
		MpUiWakeGameThread();
	}
	return false;
}

void MainMenuUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.shown) return;
		view = g_s.view;
	}
	std::vector<Hit> hits;
	Paint(buf, w, h, view, hits);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.shown) g_s.hits = std::move(hits);
}

bool MainMenuUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONUP && g_swallowUp) {
		g_swallowUp = false;
		return true;
	}
	bool shown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		shown = g_s.shown;
	}
	if (!shown || g_failed || OtherScreenOnTop()) {
		// The menu went away with the key still down: its release is not for what replaced it.
		if (msg == WM_KEYUP && ((wp == VK_RETURN && g_enterArmed) || (wp == VK_ESCAPE && g_escArmed))) {
			g_enterArmed = g_escArmed = false;
			return true;
		}
		return false;
	}
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	int clickId = 0;
	bool consumed = false;
	switch (msg) {
	case WM_MOUSEMOVE: {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.mouseX = at.x;
		g_s.view.mouseY = at.y;
		g_s.view.keyboard = false;
		break;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		consumed = HandleClick(msg, at, clickId);
		g_swallowUp = consumed;
		break;
	case WM_KEYDOWN:
	case WM_KEYUP:
		consumed = HandleKey(msg, wp, clickId);
		break;
	default:
		break;
	}
	if (clickId) Click(clickId);
	return consumed;
}
