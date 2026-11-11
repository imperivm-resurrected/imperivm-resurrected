// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/chatui.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "i18n/tr.h"
#include "game/chat.h"
#include "game/chatlog.h"
#include "game/cheats.h"
#include "game/controls.h"
#include "game/zoomview.h"
#include "ui/battlesui.h"
#include "ui/chatui_view.h"
#include "ui/keycaps.h"
#include "ui/conquestui.h"
#include "ui/controlsui.h"
#include "ui/gamemenuui.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/optionsui.h"
#include "ui/profileui.h"
#include "ui/savesui.h"
#include "ui/textedit.h"

using namespace ui;
using namespace chatui;

namespace {

// Right of the game's own columns at the left of the map: the notifications (from x 40) and the quick icons.
const int kLeftInset = 264, kBottomMargin = 10;
const DWORD kReadEveryMs = 200;

struct State {
	bool open = false;
	std::string text;
	EditState edit;
	std::vector<ChatTarget> targets;
	int target = 0;
	bool withLocation = false;
	int scroll = 0;
	ChatRecall recall;
	ChatMatch match;
	DWORD matchRead = 0;
	std::vector<ChatEntry> lines;
	uint32_t serial = 0;
	Painted painted;
	// The ping menu, open while the button that started it is down.
	bool menu = false;
	POINT menuAt = {};
	int menuHover = -1;
	DWORD menuSince = 0;
	const char *menuHow = "";
	// Dragging the panel by its strip: where the mouse holds it, from the bottom-left corner.
	bool dragging = false;
	POINT grab = {};
	ChatAnchor live;
	DWORD lastGripDown = 0;
	POINT lastGripAt = {};
	// The place last drawn, to save where the drag ended.
	ChatAnchor drawn;
	// The recipients last chosen on the chip, for the rest of the match: the bar opens with them and pings go to them.
	ChatTarget chosen;
	bool chosenSet = false;
	POINT menuCenter = {};
	// The help just shown starts at the top of the panel until the player scrolls.
	uint32_t helpSerial = 0;
	ScrollDrag bar;
};

std::mutex g_lock;
State g_s;
int g_mouseX = -10000, g_mouseY = -10000;
bool g_haveMouse = false;
// Window thread only.
WPARAM g_swallowVk = 0, g_openVk = 0, g_lastDownVk = 0;
DWORD g_lastDownAt = 0;
bool g_swallowLeftUp = false, g_swallowRightUp = false;
int g_frameW = 1280, g_frameH = 768;
// The menu shows once the button has been held this long, or as soon as the mouse leaves the dead zone; a quick click sends the
// default type without it ever appearing.
const DWORD kMenuDelayMs = 180;
const char *const kTypeNames[kPingTypes] = {"here", "attack", "danger"};

bool OtherScreenOpen()
{
	return GameMenuUiNeedsDraw() || OptionsUiNeedsDraw() || ModUiIsOpen() || MpUiNeedsDraw() || ControlsUiNeedsDraw() || SavesUiNeedsDraw() ||
		ProfileUiNeedsDraw() || ConquestUiNeedsDraw() || BattlesUiNeedsDraw() || CheatsPanelOpen();
}

std::vector<ChatTarget> TargetsOf(const ChatMatch &m)
{
	std::vector<int> others;
	for (const ChatPlayer &p : m.others) others.push_back(p.index);
	return ChatTargets(m.hasAllies, others);
}

const ChatPlayer *PlayerOf(const ChatMatch &m, int index)
{
	for (const ChatPlayer &p : m.others) {
		if (p.index == index) return &p;
	}
	return nullptr;
}

COLORREF ColorOf(uint32_t rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

void CloseLocked(State &s)
{
	s.open = false;
	s.text.clear();
	s.edit = EditState();
	s.withLocation = false;
	s.scroll = 0;
	s.recall.Reset();
	s.painted = Painted();
	s.helpSerial = 0;
	s.bar = ScrollDrag();
}

void Choose(State &s)
{
	if (s.target < 0 || s.target >= static_cast<int>(s.targets.size())) return;
	s.chosen = s.targets[s.target];
	s.chosenSet = true;
}

// Where pings go: the chip's last choice while it still exists, else the allies if there are any, else everyone.
ChatTarget PingTarget()
{
	std::lock_guard<std::mutex> lock(g_lock);
	const std::vector<ChatTarget> targets = TargetsOf(g_s.match);
	if (g_s.chosenSet && ChatFindTarget(targets, g_s.chosen) >= 0) return g_s.chosen;
	return {g_s.match.hasAllies ? kTargetAllies : kTargetAll, -1};
}

std::string KeyText(int id)
{
	const ControlKey k = ControlsModKey(id);
	return k.vk ? ControlsKeyLabel(k) : std::string();
}

void HelpEntry(int kind, const std::string &text, const std::string &key = "", std::vector<ControlKey> keys = {}, const std::string &hint = "",
	bool click = false)
{
	ChatEntry e;
	e.localKind = kind;
	e.text = text;
	e.key = key;
	e.keyHint = hint;
	e.click = click;
	for (const ControlKey &k : keys) {
		if (k.vk) e.keys.push_back(k.vk | static_cast<uint32_t>(k.mods) << 16);
	}
	ChatAddLocalEntry(std::move(e));
}

void ShowHelp()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.helpSerial = ChatLogSerial() + 1;
	}
	HelpEntry(kLocalHeading, Tr("Chat commands"));
	HelpEntry(kLocalSection, Tr("Recipients"));
	HelpEntry(kLocalRow, Tr("everyone"), Tr("/all \xc2\xb7 /al"));
	HelpEntry(kLocalRow, Tr("your allies"), Tr("/allies \xc2\xb7 /a"));
	HelpEntry(kLocalRow, Tr("one player"), "/w \xc2\xb7 /p", {}, Tr("\xe2\x80\xb9name\xe2\x80\xba"));
	HelpEntry(kLocalRow, Tr("a line that starts with /"), "//");
	HelpEntry(kLocalSection, Tr("Keys"));
	HelpEntry(kLocalRow, Tr("who reads it"), "", {{VK_TAB, 0}});
	HelpEntry(kLocalRow, Tr("what you sent"), "", {{VK_UP, 0}, {VK_DOWN, 0}});
	HelpEntry(kLocalRow, Tr("send with your camera's place"), "", {{VK_RETURN, kKeyCtrl}});
	HelpEntry(kLocalRow, Tr("close"), "", {{VK_ESCAPE, 0}});
	HelpEntry(kLocalSection, Tr("Marks"));
	const unsigned clickVk = ControlsPingClickVk();
	if (clickVk) HelpEntry(kLocalRow, Tr("hold and point: on my way, attack, danger"), "", {{static_cast<unsigned short>(clickVk), 0}}, "", true);
	const struct {
		int id;
		const char *what;
	} keys[] = {{kModKeyPing, "on my way"}, {kModKeyPingAttack, "attack here"}, {kModKeyPingDanger, "danger"}};
	for (const auto &k : keys) {
		const ControlKey key = ControlsModKey(k.id);
		if (key.vk) HelpEntry(kLocalRow, Tr(k.what), "", {key});
	}
	HelpEntry(kLocalRow, Tr("they go to whoever the chip shows"));
}

// What a /w names, among the other players. Writes the local error and returns false when it does not settle on one.
bool Whisper(const ChatSlash &sl, const ChatMatch &m, ChatTarget &target)
{
	if (sl.name.empty()) {
		ChatAddLocal(Tr("Write a name after /w."));
		return false;
	}
	std::vector<std::string> names;
	for (const ChatPlayer &p : m.others) names.push_back(p.name);
	const int found = ChatMatchName(sl.name, names);
	if (found == -1) {
		ChatAddLocal(TrFmt("No player is called %s.", {sl.name}));
		return false;
	}
	if (found == -2) {
		ChatAddLocal(TrFmt("Several players start with %s: write more of the name.", {sl.name}));
		return false;
	}
	target = {kTargetPlayer, m.others[found].index};
	return true;
}

bool OnOpen(bool withLocation)
{
	if (!ChatFloating() || OtherScreenOpen()) return false;
	const ChatMatch m = ChatReadMatch();
	if (!m.playing) return false;
	const bool everyone = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	int count = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		CloseLocked(g_s);
		g_s.open = true;
		g_s.match = m;
		g_s.matchRead = GetTickCount();
		g_s.targets = TargetsOf(m);
		g_s.target = ChatDefaultTarget(g_s.targets, everyone);
		const int remembered = g_s.chosenSet ? ChatFindTarget(g_s.targets, g_s.chosen) : -1;
		if (!everyone && remembered >= 0) g_s.target = remembered;
		Choose(g_s);
		g_s.withLocation = withLocation;
		count = static_cast<int>(g_s.targets.size());
	}
	// The key that opened the bar may still give its character: it is not the first letter of the message.
	g_openVk = GetTickCount() - g_lastDownAt < 1000 && (GetKeyState(static_cast<int>(g_lastDownVk)) & 0x8000) ? g_lastDownVk : 0;
	LogD("ui", "chat: bar opened (%d targets%s%s)", count, everyone ? ", to everyone" : "", withLocation ? ", with the location" : "");
	return true;
}

void Send(bool withLocation)
{
	std::string text;
	ChatTarget target;
	bool location = false;
	ChatMatch m;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		text = ChatCleanText(g_s.text);
		target = g_s.target >= 0 && g_s.target < static_cast<int>(g_s.targets.size()) ? g_s.targets[g_s.target] : ChatTarget();
		location = withLocation || g_s.withLocation;
		m = g_s.match;
	}
	// The bar's own commands: never sent, and the bar stays open for the answer.
	const ChatSlash sl = ChatParseSlash(text);
	bool keep = false, clear = false;
	switch (sl.kind) {
	case kSlashHelp:
		ShowHelp();
		keep = clear = true;
		break;
	case kSlashTarget:
		if (sl.rest.empty()) {
			ChatAddLocal(Tr("Write the message after the command."));
			keep = true;
		}
		target = {sl.target, -1};
		text = sl.rest;
		break;
	case kSlashWhisper:
		if (!Whisper(sl, m, target)) {
			keep = true;
		}
		else if (sl.rest.empty()) {
			ChatAddLocal(Tr("Write the message after the name."));
			keep = true;
		}
		text = sl.rest;
		break;
	case kSlashOther:
		ChatAddLocal(TrFmt("Unknown command: /%s \xc2\xb7 %s", {sl.verb, Tr("/help")}));
		keep = true;
		break;
	default:
		break;
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.recall.Push(ChatCleanText(g_s.text));
		if (keep) {
			if (clear) {
				g_s.text.clear();
				g_s.edit = EditState();
			}
			LogD("ui", "chat: command handled here (%s)", sl.kind == kSlashHelp ? "help" : sl.kind == kSlashOther ? "unknown" : "incomplete");
			return;
		}
		CloseLocked(g_s);
	}
	// The game's dialog can call back into the mod: no lock while sending.
	ChatSend(text, target, location);
}

void Close()
{
	std::lock_guard<std::mutex> lock(g_lock);
	CloseLocked(g_s);
}

int HitAt(const std::vector<Hit> &hits, POINT at)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (PtInRect(&it->r, at)) return it->action;
	}
	return kActNone;
}

bool HandleKeyDown(WPARAM wp)
{
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	g_openVk = 0;
	switch (wp) {
	case VK_RETURN:
		g_swallowVk = VK_RETURN;
		Send(ctrl);
		return true;
	case VK_ESCAPE:
		// The game opens its menu when Esc comes up: that release is the bar's too.
		g_swallowVk = VK_ESCAPE;
		Close();
		return true;
	default:
		break;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	State &s = g_s;
	switch (wp) {
	case VK_TAB:
		s.target = ChatCycle(s.target, static_cast<int>(s.targets.size()), shift);
		Choose(s);
		return true;
	case VK_UP:
		if (s.recall.Older(s.text)) EditToEnd(s.text, s.edit);
		return true;
	case VK_DOWN:
		if (s.recall.Newer(s.text)) EditToEnd(s.text, s.edit);
		return true;
	case VK_PRIOR:
		if (s.helpSerial) s.scroll = s.painted.scroll;
		s.helpSerial = 0;
		s.scroll = std::min(s.painted.maxScroll, s.scroll + s.painted.pageRows);
		return true;
	case VK_NEXT:
		if (s.helpSerial) s.scroll = s.painted.scroll;
		s.helpSerial = 0;
		s.scroll = std::max(0, s.scroll - s.painted.pageRows);
		return true;
	default:
		EditKey(s.text, s.edit, wp, kChatMaxText);
		return true;
	}
}

bool HandleMouse(UINT msg, WPARAM wp, POINT at)
{
	std::vector<Hit> hits;
	RECT panel;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		hits = g_s.painted.hits;
		panel = g_s.painted.panel;
	}
	const bool inside = PtInRect(&panel, at) != 0;
	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		if (!inside) return false;
		g_swallowLeftUp = true;
		const int action = HitAt(hits, at);
		if (action == kActPin) {
			Send(true);
			return true;
		}
		if (action == kActGrip) {
			const DWORD now = GetTickCount();
			bool reset = false;
			{
				std::lock_guard<std::mutex> lock(g_lock);
				reset = now - g_s.lastGripDown <= GetDoubleClickTime() && std::abs(at.x - g_s.lastGripAt.x) <= 4 && std::abs(at.y - g_s.lastGripAt.y) <= 4;
				g_s.lastGripDown = reset ? 0 : now;
				g_s.lastGripAt = at;
				if (!reset) {
					g_s.dragging = true;
					g_s.grab = {at.x - g_s.drawn.left, at.y - g_s.drawn.bottom};
					g_s.live = g_s.drawn;
				}
			}
			// The game's window class may not send double clicks, so the strip counts them itself.
			if (reset) ChatUiResetPosition();
			return true;
		}
		std::lock_guard<std::mutex> lock(g_lock);
		if (action == kActChip) {
			g_s.target = ChatCycle(g_s.target, static_cast<int>(g_s.targets.size()), false);
			Choose(g_s);
		}
		int field = 0;
		FieldGeom geo;
		if (action == kActField && FieldAt(at, field, geo)) {
			const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			g_s.edit.caret = EditIndexAt(geo, g_s.text, at.x);
			if (!shift) g_s.edit.anchor = g_s.edit.caret;
			if (msg == WM_LBUTTONDBLCLK) EditSelectWord(g_s.text, g_s.edit);
		}
		return true;
	}
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK: {
		if (!inside) return false;
		g_swallowRightUp = true;
		std::lock_guard<std::mutex> lock(g_lock);
		if (HitAt(hits, at) == kActChip) {
			g_s.target = ChatCycle(g_s.target, static_cast<int>(g_s.targets.size()), true);
			Choose(g_s);
		}
		return true;
	}
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MBUTTONDBLCLK:
		return inside;
	case WM_MOUSEWHEEL: {
		if (!inside) return false;
		const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.helpSerial) g_s.scroll = g_s.painted.scroll;
		g_s.helpSerial = 0;
		g_s.scroll = std::max(0, std::min(g_s.painted.maxScroll, g_s.scroll + steps * 3));
		return true;
	}
	default:
		return false;
	}
}

ChatRateLimit g_pingLimit(kPingBurst, kPingWindowMs);
DWORD g_lastHint = 0;

void Hint(const char *text)
{
	const DWORD now = GetTickCount();
	if (now - g_lastHint < 3000) return;
	g_lastHint = now;
	UiToast(Tr(text));
}

bool MayPing()
{
	const ChatMatch m = ChatReadMatch();
	return m.playing && m.alive;
}

// False when there is no match to ping in, so the key or the click goes on to the game.
bool CanPing(const char *how)
{
	if (!ChatFloating()) return false;
	if (OtherScreenOpen()) {
		LogD("ui", "chat: ping (%s) ignored: another screen is open", how);
		return false;
	}
	const ChatMatch m = ChatReadMatch();
	if (!m.playing) {
		LogD("ui", "chat: ping (%s) ignored: no match on screen", how);
		return false;
	}
	// Out of the match there is nobody to coordinate with, and the stopped game clock would keep the marks for good.
	if (!m.alive) {
		LogI("ui", "chat: ping (%s) ignored: you are out of the match", how);
		Hint("Marks are only for players still in the match.");
		return true;
	}
	return true;
}

void Ping(int x, int y, bool haveMouse, const char *how, int type)
{
	if (!MayPing()) return;
	const DWORD now = GetTickCount();
	if (!g_pingLimit.Allow(now)) {
		LogI("ui", "chat: ping (%s) refused: %d in %u s already, %lu ms to wait", how, kPingBurst, static_cast<unsigned>(kPingWindowMs / 1000),
			static_cast<unsigned long>(g_pingLimit.WaitMs(now)));
		Hint("Too many marks: wait a moment.");
		return;
	}
	ChatPing(x, y, haveMouse, how, type, PingTarget());
}

bool TryPingKey(WPARAM wp, LPARAM lp)
{
	const int keys[kPingTypes] = {kModKeyPing, kModKeyPingAttack, kModKeyPingDanger};
	int type = -1;
	for (int t = 0; t < kPingTypes && type < 0; t++) {
		if (ControlsModKeyPressed(keys[t], static_cast<unsigned>(wp))) type = t;
	}
	if (type < 0) return false;
	if (lp & (1 << 30)) return g_swallowVk == wp;
	LogD("ui", "chat: %s ping key %s received (mouse %s at %d,%d)", kTypeNames[type], ControlsKeyLabel(ControlsModKey(keys[type])).c_str(),
		g_haveMouse ? "seen" : "not seen yet", g_mouseX, g_mouseY);
	if (!CanPing("key")) return false;
	g_swallowVk = wp;
	Ping(g_mouseX, g_mouseY, g_haveMouse, "key", type);
	return true;
}

const char *ClickName(unsigned vk) { return vk == VK_CONTROL ? "Ctrl + click" : vk == VK_SHIFT ? "Shift + click" : "Alt + click"; }

// The modifier chosen in Controls + left click, as in other strategy games: the click marks the point and does not reach the
// game, so nothing is selected. On the battlefield the game selects with Ctrl and Shift, so there only Alt marks.
bool TryPingClick(POINT at)
{
	const unsigned vk = ControlsPingClickVk();
	if (!vk || !(GetKeyState(static_cast<int>(vk)) & 0x8000)) return false;
	const bool zoomMap = ZoomViewContains(at.x, at.y);
	if (!zoomMap && vk != VK_MENU) return false;
	// The same key as the box selection: the zoom map tells a click from a drag and calls back for the click.
	if (zoomMap && ZoomViewBoxUses(vk)) return false;
	LogD("ui", "chat: %s at %d,%d (%s): ping menu", ClickName(vk), static_cast<int>(at.x), static_cast<int>(at.y), zoomMap ? "zoom map" : "map view");
	// Only the map itself takes marks: not the bars, the game's windows or the mod's panels over it.
	int area[4] = {};
	const char *why = nullptr, *where = "not on the map";
	if (ModUiCoversPoint(at.x, at.y)) why = "over the cheats panel";
	else if (!ChatPointOnMap(at.x, at.y, area, where)) why = where;
	if (why) {
		LogD("ui", "chat: %s at %d,%d is not a mark: %s", ClickName(vk), static_cast<int>(at.x), static_cast<int>(at.y), why);
		return false;
	}
	if (!CanPing(ClickName(vk))) return false;
	if (!MayPing()) {
		g_swallowLeftUp = true;
		return true;
	}
	const POINT center = PingMenuPlace(at, area, g_frameW, g_frameH);
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.menu = true;
	g_s.menuAt = at;
	g_s.menuCenter = center;
	g_s.menuHover = -1;
	g_s.menuSince = GetTickCount();
	g_s.menuHow = ClickName(vk);
	return true;
}

// While the menu is open the mouse belongs to it: the button coming up sends the type pointed at, Esc or the right button cancel.
bool HandleMenu(UINT msg, WPARAM wp, LPARAM lp)
{
	POINT at = {};
	const char *how = "";
	int hover = -1;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.menu) return false;
		at = g_s.menuAt;
		how = g_s.menuHow;
		if (msg == WM_MOUSEMOVE) {
			const POINT c = g_s.menuCenter;
			g_s.menuHover = ChatPingMenuPick(static_cast<short>(LOWORD(lp)) - c.x, static_cast<short>(HIWORD(lp)) - c.y, PingMenuDeadZone(g_frameW, g_frameH));
			return false;
		}
		hover = g_s.menuHover;
		const bool cancel = (msg == WM_KEYDOWN && wp == VK_ESCAPE) || msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK;
		if (cancel) {
			g_s.menu = false;
			if (msg == WM_KEYDOWN) g_swallowVk = VK_ESCAPE;
			else g_swallowRightUp = true;
			LogD("ui", "chat: ping menu cancelled");
			return true;
		}
		// A press with the menu open means its release was lost outside the window: the menu goes, the click is a normal one.
		if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
			g_s.menu = false;
			LogD("ui", "chat: ping menu dropped (its release was not seen)");
			return false;
		}
		if (msg != WM_LBUTTONUP) return false;
		g_s.menu = false;
	}
	const int type = hover >= 0 ? hover : kPingHere;
	LogD("ui", "chat: ping menu released on %s%s", kTypeNames[type], hover < 0 ? " (no type pointed at: the default)" : "");
	Ping(at.x, at.y, true, how, type);
	return true;
}

bool OnZoomClick(int x, int y)
{
	const unsigned vk = ControlsPingClickVk();
	if (!vk || !ZoomViewBoxUses(vk)) return false;
	LogD("ui", "chat: %s at %d,%d (zoom map, no drag)", ClickName(vk), x, y);
	if (!CanPing(ClickName(vk))) return false;
	Ping(x, y, true, ClickName(vk), kPingHere);
	return true;
}

// The default place is above the command bar, right of the game's notification column; a saved place is a share of the screen.
ChatAnchor PlaceOf(const State &s, int w, int h, int &maxWidth)
{
	const ChatMatch &m = s.match;
	const int viewLeft = m.haveView ? std::max(0, m.viewLeft) : 0;
	const int barTop = m.haveBar ? m.barTop : m.haveView ? std::min(h, m.viewBottom) : h - 54;
	ChatAnchor a;
	a.left = viewLeft + kLeftInset;
	a.bottom = barTop - kBottomMargin;
	maxWidth = (m.haveView ? std::min(w, m.viewRight) : w) - a.left - 16;
	const int width = ChatWidth(w, h, maxWidth);
	const Config &c = GetConfig();
	if (s.dragging) a = s.live;
	else if (c.chatX >= 0 && c.chatY >= 0) a = {ChatFromPermille(c.chatX, w), ChatFromPermille(c.chatY, h)};
	else return a;
	maxWidth = width;
	return ChatClampAnchor(a, w, width, std::min(h, static_cast<int>(160 * Scale(w, h))), barTop - kBottomMargin);
}

bool HandleDrag(UINT msg, WPARAM wp, LPARAM lp)
{
	ChatAnchor end;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dragging) return false;
		const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		// A move with the button already up means the release happened outside the window.
		const bool released = msg == WM_LBUTTONUP || (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON));
		if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP) g_s.live = {at.x - g_s.grab.x, at.y - g_s.grab.y};
		if (!released) return msg == WM_MOUSEMOVE ? false : msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
		int maxWidth = 0;
		end = PlaceOf(g_s, g_frameW, g_frameH, maxWidth);
		g_s.dragging = false;
	}
	Config &c = MutableConfig();
	c.chatX = ChatToPermille(end.left, g_frameW);
	c.chatY = ChatToPermille(end.bottom, g_frameH);
	SaveConfig();
	LogI("ui", "chat: position saved at %d,%d of %dx%d (%d,%d thousandths)", end.left, end.bottom, g_frameW, g_frameH, c.chatX, c.chatY);
	if (msg == WM_LBUTTONUP) g_swallowLeftUp = false;
	return msg == WM_LBUTTONUP;
}

Line LineOf(const ChatEntry &e, int alpha)
{
	Line l;
	l.clock = ChatClock(e.clockMs);
	l.scope = e.scope;
	l.name = e.name;
	l.toName = e.toName;
	l.text = e.text;
	l.color = ColorOf(e.rgb);
	l.location = e.location;
	l.ping = e.ping;
	l.local = e.local;
	l.localKind = e.localKind;
	l.key = e.key;
	l.keyHint = e.keyHint;
	l.click = e.click;
	l.error = e.error;
	for (const uint32_t k : e.keys) l.keys.push_back({static_cast<unsigned short>(k & 0xffff), static_cast<unsigned char>(k >> 16)});
	l.alpha = alpha;
	return l;
}

}

void ChatUiInstall()
{
	ChatSetOpenHandler(&OnOpen);
	ZoomViewSetClickHook(&OnZoomClick);
}

void ChatUiGameTick()
{
	ChatGameTick();
	if (!ChatFloating()) return;
	const DWORD now = GetTickCount();
	bool read = false, open = false;
	uint32_t serial = 0;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		open = g_s.open;
		read = open || now - g_s.matchRead >= kReadEveryMs;
		serial = g_s.serial;
	}
	const uint32_t fresh = ChatLogSerial();
	std::vector<ChatEntry> lines;
	const bool newLines = fresh != serial;
	if (newLines) lines = ChatLogLines();
	ChatMatch m;
	if (read) m = ChatReadMatch();
	const bool other = open && OtherScreenOpen();
	std::lock_guard<std::mutex> lock(g_lock);
	if (newLines) {
		g_s.lines = std::move(lines);
		g_s.serial = fresh;
	}
	if (!read) return;
	const ChatTarget current = g_s.target >= 0 && g_s.target < static_cast<int>(g_s.targets.size()) ? g_s.targets[g_s.target] : ChatTarget();
	g_s.match = m;
	g_s.matchRead = now;
	if (!m.onScreen) g_s.chosenSet = false;
	if (g_s.open && (!m.playing || other)) {
		LogD("ui", "chat: bar closed (%s)", other ? "another screen opened" : "the match is no longer on screen");
		CloseLocked(g_s);
		return;
	}
	if (!g_s.open) return;
	const std::vector<ChatTarget> targets = TargetsOf(m);
	if (targets.size() == g_s.targets.size() && std::equal(targets.begin(), targets.end(), g_s.targets.begin())) return;
	// Someone left or an alliance changed: the same recipient stays chosen if it still exists.
	g_s.targets = targets;
	const int again = ChatFindTarget(targets, current);
	g_s.target = again >= 0 ? again : ChatDefaultTarget(targets, false);
}

bool ChatUiNeedsDraw()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.match.onScreen) return false;
		if (!g_s.open && !g_s.menu && (g_s.lines.empty() || GetTickCount() - g_s.lines.back().arrivedMs >= kChatShowMs)) return false;
	}
	return !OtherScreenOpen();
}

void ChatUiDraw(uint32_t *buf, int w, int h)
{
	View v;
	bool menu = false;
	PingMenuView menuView;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const State &s = g_s;
		if (!s.match.onScreen) return;
		g_frameW = w;
		g_frameH = h;
		const DWORD now = GetTickCount();
		menu = s.menu && (s.menuHover >= 0 || now - s.menuSince >= kMenuDelayMs);
		menuView.x = s.menuCenter.x;
		menuView.y = s.menuCenter.y;
		menuView.pressX = s.menuAt.x;
		menuView.pressY = s.menuAt.y;
		menuView.hover = s.menuHover;
		v.typing = s.open;
		if (s.open) {
			const size_t from = s.lines.size() > kChatHistoryLines ? s.lines.size() - kChatHistoryLines : 0;
			for (size_t i = from; i < s.lines.size(); i++) {
				if (s.helpSerial && s.lines[i].serial == s.helpSerial) v.topLine = static_cast<int>(v.lines.size());
				v.lines.push_back(LineOf(s.lines[i], 255));
			}
		}
		else {
			for (const ChatEntry &e : s.lines) {
				const int alpha = ChatLineAlpha(now - e.arrivedMs);
				if (alpha > 0) v.lines.push_back(LineOf(e, alpha));
			}
		}
		v.scroll = s.scroll;
		ChatTarget shownTarget = s.target >= 0 && s.target < static_cast<int>(s.targets.size()) ? s.targets[s.target] : ChatTarget();
		bool haveTarget = s.target >= 0 && s.target < static_cast<int>(s.targets.size());
		// A target command being typed shows its recipients on the chip at once.
		const ChatSlash sl = s.open ? ChatParseSlash(s.text) : ChatSlash();
		if (sl.kind == kSlashTarget && sl.complete) {
			shownTarget = {sl.target, -1};
			haveTarget = true;
		}
		else if (sl.kind == kSlashWhisper && !sl.name.empty()) {
			std::vector<std::string> names;
			for (const ChatPlayer &p : s.match.others) names.push_back(p.name);
			const int found = ChatMatchName(sl.name, names);
			if (found >= 0) {
				shownTarget = {kTargetPlayer, s.match.others[found].index};
				haveTarget = true;
			}
		}
		if (haveTarget) {
			const ChatTarget &t = shownTarget;
			v.targetScope = t.kind == kTargetAllies ? kChatAllies : t.kind == kTargetAll ? kChatAll : kChatPrivate;
			if (const ChatPlayer *p = t.kind == kTargetPlayer ? PlayerOf(s.match, t.player) : nullptr) {
				v.targetName = p->name;
				v.targetColor = ColorOf(p->rgb);
			}
		}
		v.text = s.text;
		v.edit = s.edit;
		v.withLocation = s.withLocation;
		const ChatAnchor at = PlaceOf(s, w, h, v.maxWidth);
		v.left = at.left;
		v.bottom = at.bottom;
		v.dragging = s.dragging;
		v.bar = s.bar;
		g_s.drawn = at;
	}
	v.mouseX = g_mouseX;
	v.mouseY = g_mouseY;
	if (v.typing || !v.lines.empty()) {
		Painted painted;
		PaintChat(buf, w, h, v, painted);
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.open == v.typing) {
			g_s.painted = std::move(painted);
			g_s.scroll = std::min(g_s.scroll, g_s.painted.maxScroll);
		}
	}
	if (menu) PaintPingMenu(buf, w, h, menuView);
}

void ChatUiResetPosition()
{
	Config &c = MutableConfig();
	if (c.chatX < 0) return;
	c.chatX = c.chatY = -1;
	SaveConfig();
	LogI("ui", "chat: position reset to the default place");
}

bool ChatUiPositionMoved() { return GetConfig().chatX >= 0; }

// The history's scroll bar: a press on it, and the drag that follows until the button comes up.
bool HandleHistoryBar(UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg != WM_LBUTTONDOWN && msg != WM_LBUTTONDBLCLK && msg != WM_MOUSEMOVE && msg != WM_LBUTTONUP) return false;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.open && g_s.bar.id < 0) return false;
	ScrollTo to;
	const bool took = ScrollbarMouse(g_s.bar, g_s.painted.hits, msg, wp, at, to);
	if (to.id == kBarHistory) {
		g_s.helpSerial = 0;
		g_s.scroll = std::max(0, std::min(g_s.painted.maxScroll, g_s.painted.maxScroll - to.pos));
	}
	if (!took || msg == WM_MOUSEMOVE) return false;
	g_swallowLeftUp = msg != WM_LBUTTONUP;
	return true;
}

bool ChatUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (HandleMenu(msg, wp, lp)) return true;
	if (HandleDrag(msg, wp, lp)) return true;
	if (HandleHistoryBar(msg, wp, lp)) return true;
	if (msg == WM_MOUSEMOVE) {
		g_mouseX = static_cast<short>(LOWORD(lp));
		g_mouseY = static_cast<short>(HIWORD(lp));
		g_haveMouse = true;
		return false;
	}
	if (msg == WM_MOUSELEAVE) g_haveMouse = false;
	if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
		g_lastDownVk = wp;
		g_lastDownAt = GetTickCount();
	}
	if (g_swallowVk && (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && wp == g_swallowVk) {
		if (msg == WM_KEYUP || msg == WM_SYSKEYUP) g_swallowVk = 0;
		return true;
	}
	if (g_swallowVk == VK_RETURN && msg == WM_CHAR && wp == '\r') return true;
	if (msg == WM_LBUTTONUP && g_swallowLeftUp) {
		g_swallowLeftUp = false;
		return true;
	}
	if (msg == WM_RBUTTONUP && g_swallowRightUp) {
		g_swallowRightUp = false;
		return true;
	}
	bool open = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		open = g_s.open;
	}
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
		const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		RECT panel = {};
		if (open) {
			std::lock_guard<std::mutex> lock(g_lock);
			panel = g_s.painted.panel;
		}
		if (!PtInRect(&panel, at) && TryPingClick(at)) return true;
	}
	if (!open) return (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && TryPingKey(wp, lp);
	// Held down, the key that opened the bar repeats: Enter would send the empty bar at once.
	if (msg == WM_KEYDOWN && wp == g_openVk && (lp & (1 << 30))) return true;
	switch (msg) {
	case WM_KEYDOWN: return HandleKeyDown(wp);
	case WM_KEYUP:
		if (wp == g_openVk) g_openVk = 0;
		return true;
	case WM_CHAR: {
		if (ControlsEatChar(msg, static_cast<unsigned>(wp))) return true;
		if (g_openVk && (GetKeyState(static_cast<int>(g_openVk)) & 0x8000)) return true;
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.recall.Reset();
		EditChar(g_s.text, g_s.edit, wp, kChatMaxText);
		return true;
	}
	case WM_DEADCHAR:
		return true;
	default:
		break;
	}
	if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
		const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		// The wheel comes in screen coordinates: the last position the mouse moved to says where it is.
		const POINT where = msg == WM_MOUSEWHEEL ? POINT{g_mouseX, g_mouseY} : at;
		return HandleMouse(msg, wp, where);
	}
	return false;
}
