// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>

#include "ui/textedit.h"
#include "MinHook.h"
#include "core/log.h"

#include "ui/mpui_internal.h"

using namespace mpui_impl;

void DropKeyMove(WPARAM vk)
{
	int sel = -1;
	const int count = static_cast<int>(DropItems(g_s, sel).size());
	if (count <= 0) return;
	const int cur = g_s.tabAction == kGDropPick ? g_s.tabArg : sel;
	if (g_s.dropKind == kDropColor) {
		std::vector<int> free;
		for (const Hit &h : g_s.hits) {
			if (h.action == kGDropPick) free.push_back(h.arg);
		}
		if (free.empty()) return;
		std::sort(free.begin(), free.end());
		const int step = vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : vk == VK_UP ? -6 : vk == VK_DOWN ? 6 : 0;
		int to = vk == VK_HOME ? free.front() : vk == VK_END ? free.back() : cur;
		if (step && cur < 0) {
			to = free.front();
		}
		else if (step) {
			for (int c = cur + step; c >= 0 && c < 12; c += step) {
				if (std::find(free.begin(), free.end(), c) != free.end()) {
					to = c;
					break;
				}
				if (std::abs(step) == 6) break;
			}
		}
		g_s.tabAction = kGDropPick;
		g_s.tabArg = to;
		return;
	}
	if (vk == VK_LEFT || vk == VK_RIGHT) return;
	int to = vk == VK_HOME ? 0 : vk == VK_END ? count - 1 : cur < 0 ? 0 : cur + (vk == VK_UP ? -1 : 1);
	to = std::max(0, std::min(count - 1, to));
	g_s.tabAction = kGDropPick;
	g_s.tabArg = to;
	if (to < g_s.dropScroll) g_s.dropScroll = to;
	else if (to >= g_s.dropScroll + kDropRows) g_s.dropScroll = to - kDropRows + 1;
}

void DropFocusCombo()
{
	g_s.tabAction = kGDropOpen;
	g_s.tabArg = g_s.dropKind | (g_s.dropIndex << 4);
}

bool MpUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_gameModal > 0) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.open) return false;
	if (g_s.startingCover) return msg != WM_MOUSEMOVE && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST));
	switch (msg) {
	case WM_MOUSEMOVE:
		g_s.mouseX = static_cast<short>(LOWORD(lp));
		g_s.mouseY = static_cast<short>(HIWORD(lp));
		{
			const POINT at = {g_s.mouseX, g_s.mouseY};
			ListMove(g_s.pick, g_s.pickGeom, at);
			ListMove(g_s.opt, g_s.optGeom, at);
			ListMove(g_s.detail, g_s.detailGeom, at);
			ChatBarMove(at);
		}
		if (g_s.syncConfirm || g_s.view == kViewCreate) {
			const POINT at = {g_s.mouseX, g_s.mouseY};
			ListMove(g_s.syncList, g_s.syncGeom, at);
			ListMove(g_s.createModsList, g_s.createModsGeom, at);
			ListMove(g_s.createCivList, g_s.createCivGeom, at);
		}
		if (g_s.edit.dragging && g_s.mouseDown && g_s.focus != kNoField) {
			FieldGeom geom;
			if (FieldGet(ActionOfField(g_s.focus), geom)) g_s.edit.caret = EditIndexAt(geom, FocusedText(), g_s.mouseX);
		}
		if (g_s.resizeCol >= 0 && g_s.mouseDown) {
			const int c = g_s.resizeCol & 3;
			int others = 0;
			for (int i = 0; i < 4; i++) others += i == c ? 0 : g_s.colW[i];
			const int want = g_s.resizeStartW + static_cast<int>((g_s.mouseX - g_s.resizeStartX) / g_s.uiScale);
			g_s.colW[c] = std::max(50, std::min(want, 600 - 70 - others));
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseX = static_cast<short>(LOWORD(lp));
		g_s.mouseY = static_cast<short>(HIWORD(lp));
		POINT pt = {g_s.mouseX, g_s.mouseY};
		g_s.tabAction = -1;
		if (g_s.syncConfirm && ListDown(g_s.syncList, g_s.syncGeom, pt)) return true;
		if (!g_s.syncConfirm && g_s.view == kViewCreate && (ListDown(g_s.createModsList, g_s.createModsGeom, pt) || ListDown(g_s.createCivList, g_s.createCivGeom, pt))) return true;
		if (g_s.game.picker && !SaveMapPicking(g_s)) {
			if (ListDown(g_s.pick, g_s.pickGeom, pt)) return true;
		}
		else if (ListDown(g_s.opt, g_s.optGeom, pt) || ListDown(g_s.detail, g_s.detailGeom, pt) || ChatBarDown(pt)) {
			return true;
		}
		bool hit = false;
		for (auto it = g_s.hits.rbegin(); it != g_s.hits.rend(); ++it) {
			if (PtInRect(&it->r, pt)) {
				const Hit h = *it;
				g_s.pressAction = h.action;
				g_s.pressArg = h.arg;
				g_s.pressTime = GetTickCount();
				g_s.mouseDown = true;
				hit = true;
				DoAction(h.action, h.arg);
				if (msg == WM_LBUTTONDBLCLK && h.action == kSelect && g_s.ready) {
					const std::vector<int> vis = Visible(g_s);
					if (g_s.selected >= 0 && g_s.selected < static_cast<int>(vis.size())) {
						const OnlineLobby &g = g_s.lobbies[vis[g_s.selected]];
						if (!g.playing && g.players < g.maxPlayers) DoAction(kJoin, 0);
					}
				}
				WakeGameThread();
				break;
			}
		}
		if (!hit) g_s.focus = kNoField;
		else if (g_s.focus != kNoField) {
			FieldGeom geom;
			if (FieldGet(ActionOfField(g_s.focus), geom) && PtInRect(&geom.r, pt)) {
				std::string &text = FocusedText();
				g_s.edit.caret = EditIndexAt(geom, text, g_s.mouseX);
				if (!(GetKeyState(VK_SHIFT) & 0x8000)) g_s.edit.anchor = g_s.edit.caret;
				if (msg == WM_LBUTTONDBLCLK) EditSelectWord(text, g_s.edit);
				else g_s.edit.dragging = true;
			}
		}
		return true;
	}
	case WM_MOUSEWHEEL: {
		if (g_s.syncConfirm) {
			ListWheel(g_s.syncList, g_s.syncGeom, POINT{g_s.mouseX, g_s.mouseY}, GET_WHEEL_DELTA_WPARAM(wp));
			return true;
		}
		if (g_s.view == kViewCreate && !g_s.dropOpen) {
			const POINT at = {g_s.mouseX, g_s.mouseY};
			if (ListWheel(g_s.createModsList, g_s.createModsGeom, at, GET_WHEEL_DELTA_WPARAM(wp)) ||
				ListWheel(g_s.createCivList, g_s.createCivGeom, at, GET_WHEEL_DELTA_WPARAM(wp), 1)) {
				return true;
			}
		}
	}
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			const POINT pt = {g_s.mouseX, g_s.mouseY};
			if (!g_s.dropOpen && g_chat.rectValid && PtInRect(&g_chat.rect, pt)) {
				g_chat.scroll = std::max(0, g_chat.scroll + GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 40);
				return true;
			}
			if (!g_s.dropOpen && g_s.game.active && PtInRect(&g_optRect, pt) && g_s.optGeom.maxScroll > 0) {
				g_s.opt.scroll = std::max(0, std::min(g_s.optGeom.maxScroll, g_s.opt.scroll - GET_WHEEL_DELTA_WPARAM(wp) * 20 / WHEEL_DELTA));
				return true;
			}
			if (!g_s.dropOpen && !g_s.passPrompt && g_s.view == kViewList && g_detail.rectValid && PtInRect(&g_detail.rect, pt)) {
				g_s.detail.scroll = std::max(0, std::min(g_s.detailGeom.maxScroll, g_s.detail.scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 40));
				return true;
			}
		}
		if (g_s.dropOpen) {
			int sel = -1;
			const int count = static_cast<int>(DropItems(g_s, sel).size());
			g_s.dropScroll = ClampDropScroll(g_s.dropScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3, count);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s)) {
			const int count = static_cast<int>(g_s.game.pickerItems.size());
			g_s.pick.scroll = std::max(0, std::min(g_s.pick.scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3, count - PickRowsShown()));
		}
		return true;
	case WM_LBUTTONUP:
		g_s.edit.dragging = false;
		g_s.mouseDown = false;
		g_s.resizeCol = -1;
		ListUp(g_s.pick);
		ListUp(g_s.opt);
		ListUp(g_s.detail);
		ListUp(g_s.chat);
		ListUp(g_s.syncList);
		ListUp(g_s.createModsList);
		ListUp(g_s.createCivList);
		g_s.pressTime = GetTickCount();
		return true;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_KEYUP:
		return true;
	case WM_CHAR:
		if (g_s.focus != kNoField) FieldKey(msg, wp);
		return true;
	case WM_KEYDOWN:
		if (wp == VK_TAB) {
			TabStep((GetKeyState(VK_SHIFT) & 0x8000) != 0);
		}
		else if (g_s.passPrompt && wp == VK_ESCAPE) {
			DoAction(kPassCancel, 0);
		}
		else if (g_s.dropOpen && (wp == VK_UP || wp == VK_DOWN || wp == VK_LEFT || wp == VK_RIGHT || wp == VK_HOME || wp == VK_END)) {
			DropKeyMove(wp);
		}
		else if (g_s.focus != kNoField) {
			FieldKey(msg, wp);
		}
		else if (g_s.tabAction >= 0 && (wp == VK_RETURN || wp == VK_SPACE)) {
			const int action = g_s.tabAction, arg = g_s.tabArg;
			DoAction(action, arg);
			if (action == kGDropOpen && g_s.dropOpen) {
				int sel = -1;
				DropItems(g_s, sel);
				g_s.tabAction = kGDropPick;
				g_s.tabArg = std::max(0, sel);
			}
			else if (action == kGDropPick) {
				DropFocusCombo();
			}
			WakeGameThread();
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && g_s.pickDelete >= 0) {
			if (wp == VK_ESCAPE) DoAction(kPickDeleteNo, 0);
			else if (wp == VK_RETURN) DoAction(kPickDeleteYes, 0);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END)) {
			PickMove(wp);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
			DoAction(kFieldPickSearch, 0);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && wp == VK_RETURN) {
			DoAction(kPickOk, 0);
		}
		else if (wp == VK_ESCAPE && g_s.game.picker && !SaveMapPicking(g_s) && !g_s.dropOpen) {
			DoAction(kPickCancel, 0);
		}
		else if (wp == VK_ESCAPE && g_s.dropOpen) {
			g_s.dropOpen = false;
			DropFocusCombo();
		}
		else if (wp == VK_ESCAPE) {
			if (g_s.searching) DoAction(g_s.found ? kDecline : kCancelSearch, 0);
			else DoAction(kBack, 0);
		}
		else if (wp == VK_UP) {
			g_s.selected = std::max(0, g_s.selected - 1);
		}
		else if (wp == VK_DOWN) {
			g_s.selected = std::min(static_cast<int>(Visible(g_s).size()) - 1, g_s.selected + 1);
		}
		return true;
	}
	return false;
}

bool MpUiNeedsDraw()
{
	if (g_gameModal > 0) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.open;
}
