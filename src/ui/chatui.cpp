// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/chatui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/chat.h"
#include "game/chatlog.h"
#include "game/cheats.h"
#include "game/controls.h"
#include "ui/battlesui.h"
#include "ui/chatui_view.h"
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
const DWORD kReadEveryMs = 200, kPingCooldownMs = 700;

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
};

std::mutex g_lock;
State g_s;
int g_mouseX = -10000, g_mouseY = -10000;
bool g_haveMouse = false;
// Window thread only.
WPARAM g_swallowVk = 0, g_openVk = 0, g_lastDownVk = 0;
DWORD g_lastDownAt = 0, g_lastPing = 0;
bool g_swallowLeftUp = false, g_swallowRightUp = false;

bool OtherScreenOpen()
{
	return GameMenuUiNeedsDraw() || OptionsUiNeedsDraw() || ModUiIsOpen() || MpUiNeedsDraw() || ControlsUiNeedsDraw() || SavesUiNeedsDraw() ||
		ProfileUiNeedsDraw() || ConquestUiNeedsDraw() || BattlesUiNeedsDraw() || CheatsPanelOpen();
}

std::vector<ChatTarget> TargetsOf(const ChatMatch &m)
{
	std::vector<int> others;
	for (const ChatPlayer &p : m.humans) others.push_back(p.index);
	return ChatTargets(m.hasAllies, others);
}

const ChatPlayer *PlayerOf(const ChatMatch &m, int index)
{
	for (const ChatPlayer &p : m.humans) {
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
	{
		std::lock_guard<std::mutex> lock(g_lock);
		text = g_s.text;
		target = g_s.target >= 0 && g_s.target < static_cast<int>(g_s.targets.size()) ? g_s.targets[g_s.target] : ChatTarget();
		location = withLocation || g_s.withLocation;
		g_s.recall.Push(ChatCleanText(text));
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
		return true;
	case VK_UP:
		if (s.recall.Older(s.text)) EditToEnd(s.text, s.edit);
		return true;
	case VK_DOWN:
		if (s.recall.Newer(s.text)) EditToEnd(s.text, s.edit);
		return true;
	case VK_PRIOR:
		s.scroll = std::min(s.painted.maxScroll, s.scroll + s.painted.pageRows);
		return true;
	case VK_NEXT:
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
		std::lock_guard<std::mutex> lock(g_lock);
		if (action == kActChip) g_s.target = ChatCycle(g_s.target, static_cast<int>(g_s.targets.size()), false);
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
		if (HitAt(hits, at) == kActChip) g_s.target = ChatCycle(g_s.target, static_cast<int>(g_s.targets.size()), true);
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
		g_s.scroll = std::max(0, std::min(g_s.painted.maxScroll, g_s.scroll + steps * 3));
		return true;
	}
	default:
		return false;
	}
}

bool TryPing(WPARAM wp, LPARAM lp)
{
	if (!ControlsModKeyPressed(kModKeyPing, static_cast<unsigned>(wp))) return false;
	if (!ChatFloating() || OtherScreenOpen()) return false;
	const ChatMatch m = ChatReadMatch();
	if (!m.playing || !m.severalHumans) return false;
	g_swallowVk = wp;
	const DWORD now = GetTickCount();
	if ((lp & (1 << 30)) || now - g_lastPing < kPingCooldownMs) return true;
	g_lastPing = now;
	ChatPing(g_mouseX, g_mouseY, g_haveMouse);
	return true;
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
	l.alpha = alpha;
	return l;
}

}

void ChatUiInstall() { ChatSetOpenHandler(&OnOpen); }

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
		if (!g_s.open && (g_s.lines.empty() || GetTickCount() - g_s.lines.back().arrivedMs >= kChatShowMs)) return false;
	}
	return !OtherScreenOpen();
}

void ChatUiDraw(uint32_t *buf, int w, int h)
{
	View v;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const State &s = g_s;
		if (!s.match.onScreen) return;
		const DWORD now = GetTickCount();
		v.typing = s.open;
		if (s.open) {
			const size_t from = s.lines.size() > kChatHistoryLines ? s.lines.size() - kChatHistoryLines : 0;
			for (size_t i = from; i < s.lines.size(); i++) v.lines.push_back(LineOf(s.lines[i], 255));
		}
		else {
			for (const ChatEntry &e : s.lines) {
				const int alpha = ChatLineAlpha(now - e.arrivedMs);
				if (alpha > 0) v.lines.push_back(LineOf(e, alpha));
			}
		}
		v.scroll = s.scroll;
		if (s.target >= 0 && s.target < static_cast<int>(s.targets.size())) {
			const ChatTarget &t = s.targets[s.target];
			v.targetScope = t.kind == kTargetAllies ? kChatAllies : t.kind == kTargetAll ? kChatAll : kChatPrivate;
			if (const ChatPlayer *p = t.kind == kTargetPlayer ? PlayerOf(s.match, t.player) : nullptr) {
				v.targetName = p->name;
				v.targetColor = ColorOf(p->rgb);
			}
		}
		v.text = s.text;
		v.edit = s.edit;
		v.withLocation = s.withLocation;
		const ChatMatch &m = s.match;
		const int viewLeft = m.haveView ? std::max(0, m.viewLeft) : 0;
		v.left = viewLeft + kLeftInset;
		v.bottom = (m.haveBar ? m.barTop : m.haveView ? std::min(h, m.viewBottom) : h - 54) - kBottomMargin;
		v.maxWidth = (m.haveView ? std::min(w, m.viewRight) : w) - v.left - 16;
	}
	v.mouseX = g_mouseX;
	v.mouseY = g_mouseY;
	if (!v.typing && v.lines.empty()) return;
	Painted painted;
	PaintChat(buf, w, h, v, painted);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.open == v.typing) {
		g_s.painted = std::move(painted);
		g_s.scroll = std::min(g_s.scroll, g_s.painted.maxScroll);
	}
}

bool ChatUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
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
	if (!open) return (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && TryPing(wp, lp);
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
