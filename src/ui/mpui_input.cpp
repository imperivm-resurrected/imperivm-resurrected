// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <mutex>

#include "ui/colorwheel.h"
#include "ui/textedit.h"
#include "MinHook.h"
#include "core/log.h"

#include "ui/mpui_internal.h"
#include "ui/mpui_tourney.h"

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
	g_s.tabArg = DropArg(g_s.dropKind, g_s.dropIndex);
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
			ScrollTo to;
			if (ScrollbarMouse(g_s.dropBar, g_s.hits, msg, wp, at, to)) {
				if (to.id == kDropMenuBar) g_s.dropScroll = to.pos;
				else if (!g_s.scrolls.Apply(g_s.paneAreas, to)) TourneyScrollTo(to.id, to.pos);
			}
			if (g_s.view == kViewTournaments) TourneyPanMove(at, wp);
		}
		if (g_s.syncConfirm || g_s.view == kViewCreate) {
			const POINT at = {g_s.mouseX, g_s.mouseY};
			ListMove(g_s.syncList, g_s.syncGeom, at);
			ListMove(g_s.createModsList, g_s.createModsGeom, at);
		}
		if (g_s.edit.dragging && g_s.mouseDown && g_s.focus != kNoField) {
			FieldGeom geom;
			if (FieldGet(ActionOfField(g_s.focus), geom)) g_s.edit.caret = EditIndexAt(geom, FocusedText(), g_s.mouseX);
		}
		if (g_s.speedDrag && g_s.mouseDown) g_s.speedTry = SpeedAt(g_s.speedRect, g_s.mouseX);
		if (g_s.colorDrag && g_s.mouseDown) {
			g_s.colorTry = colorwheel::Pick(g_s.colorDrag == kGColorValue, g_s.colorRect, g_s.mouseX, g_s.mouseY, g_s.colorTry);
		}
		if (g_s.resizeCol >= 0 && g_s.mouseDown) {
			const int c = g_s.resizeCol & 3;
			int others = 0;
			for (int i = 0; i < 4; i++) others += i == c ? 0 : g_s.colW[i];
			const float units = g_s.uiScale * LobbyTableWidth(g_s.viewW, g_s.viewH) / kColumnBaseW;
			const int want = g_s.resizeStartW + static_cast<int>((g_s.mouseX - g_s.resizeStartX) / units);
			g_s.colW[c] = std::max(50, std::min(want, kColumnBaseW - 70 - others));
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseX = static_cast<short>(LOWORD(lp));
		g_s.mouseY = static_cast<short>(HIWORD(lp));
		POINT pt = {g_s.mouseX, g_s.mouseY};
		g_s.tabAction = -1;
		{
			ScrollTo to;
			if (ScrollbarMouse(g_s.dropBar, g_s.hits, msg, wp, pt, to)) {
				if (to.id == kDropMenuBar) g_s.dropScroll = to.pos;
				else if (!g_s.scrolls.Apply(g_s.paneAreas, to)) TourneyScrollTo(to.id, to.pos);
				return true;
			}
		}
		if (g_s.view == kViewTournaments && !g_s.dropOpen && !g_s.room.id && TourneyPanDown(pt, g_s.hits)) return true;
		if (g_s.syncConfirm && ListDown(g_s.syncList, g_s.syncGeom, pt)) return true;
		// The Content dropdown opens over the mods list: its items take the click, not the list's bar under them.
		if (!g_s.syncConfirm && g_s.view == kViewCreate && !g_s.dropOpen && ListDown(g_s.createModsList, g_s.createModsGeom, pt)) return true;
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
				if (h.action == kGColorWheel || h.action == kGColorValue) {
					g_s.colorDrag = h.action;
					g_s.colorRect = h.r;
					g_s.colorTry = colorwheel::Pick(h.action == kGColorValue, h.r, pt.x, pt.y, g_s.colorTry);
					break;
				}
				DoAction(h.action, h.arg);
				if (h.action == kGSpeedSlider && g_s.speedDrag) {
					g_s.speedRect = h.r;
					g_s.speedTry = SpeedAt(h.r, pt.x);
					break;
				}
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
		if (LinkOfferShown(g_s)) return true;
		if (g_s.setupDialog == kSetupOpenLoad) {
			const int most = std::max(0, static_cast<int>(g_s.setups.size()) - kSetupRows);
			g_s.setupScroll = std::max(0, std::min(most, g_s.setupScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA));
			return true;
		}
		if (g_s.setupDialog) return true;
		if (g_s.syncConfirm) {
			ListWheel(g_s.syncList, g_s.syncGeom, POINT{g_s.mouseX, g_s.mouseY}, GET_WHEEL_DELTA_WPARAM(wp));
			return true;
		}
		if (g_s.view == kViewCreate && !g_s.dropOpen && ListWheel(g_s.createModsList, g_s.createModsGeom, POINT{g_s.mouseX, g_s.mouseY}, GET_WHEEL_DELTA_WPARAM(wp))) return true;
		if (g_s.view == kViewTournaments && !g_s.dropOpen && !g_s.room.id && TourneyWheel(GET_WHEEL_DELTA_WPARAM(wp), (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0, (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0)) return true;
	}
		{
			std::lock_guard<std::mutex> lock(g_chat.mx);
			const POINT pt = {g_s.mouseX, g_s.mouseY};
			if (!g_s.dropOpen && g_s.scrolls.Wheel(g_s.paneAreas, pt, GET_WHEEL_DELTA_WPARAM(wp))) return true;
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
			// The rows the search left, as the picker paints them.
			const int count = static_cast<int>(PickView(g_s).size());
			const int top = std::max(0, std::min(g_s.pick.scroll, count - PickRowsShown()));
			g_s.pick.scroll = std::max(0, std::min(top - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3, count - PickRowsShown()));
		}
		return true;
	case WM_LBUTTONUP:
		g_s.edit.dragging = false;
		g_s.mouseDown = false;
		g_s.colorDrag = 0;
		// The speed is given to the room when the slider is let go; at the game's normal speed the room leaves it to the game.
		if (g_s.speedDrag) {
			g_s.speedDrag = false;
			g_s.speedHeld = GetTickCount();
			const int pace = SpeedPace(g_s.speedTry);
			const int value = SpeedPercent(pace) == SpeedPercent(g_s.game.normalSpeed) ? -1 : pace;
			g_s.game.gameSpeed = value;
			GameRoomSetSpeed(value);
		}
		g_s.resizeCol = -1;
		g_s.dropBar = ScrollDrag();
		TourneyPanUp({static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))}, g_s.hits);
		ListUp(g_s.pick);
		ListUp(g_s.opt);
		ListUp(g_s.detail);
		ListUp(g_s.chat);
		ListUp(g_s.syncList);
		ListUp(g_s.createModsList);
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
		// The button the keyboard was on may have left the screen (the cup published, a dialog closed) or be disabled now: Enter must not
		// run it again from memory.
		if (g_s.tabAction >= 0 && (wp == VK_RETURN || wp == VK_SPACE)) {
			bool shown = false;
			for (const Hit &h : g_s.hits) shown = shown || (h.action == g_s.tabAction && h.arg == g_s.tabArg);
			if (!shown) g_s.tabAction = -1;
		}
		if (wp == VK_TAB) {
			TabStep((GetKeyState(VK_SHIFT) & 0x8000) != 0);
		}
		else if (g_s.passPrompt && wp == VK_ESCAPE) {
			DoAction(kPassCancel, 0);
		}
		else if (g_s.setupDialog && (wp == VK_ESCAPE || (wp == VK_RETURN && g_s.tabAction < 0 && g_s.setupDialog == kSetupOpenSave))) {
			DoAction(wp == VK_ESCAPE ? kSetupCancel : kSetupSave, 0);
		}
		else if (LinkOfferShown(g_s) && (wp == VK_ESCAPE || (wp == VK_RETURN && g_s.tabAction < 0))) {
			DoAction(wp == VK_ESCAPE ? kLinkOfferNo : kLinkOfferYes, 0);
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
		else if (g_s.game.picker && !SaveMapPicking(g_s) && !TourneyRoomHiddenHeld(g_s) && !RankedRoomHidden(g_s) && g_s.pickDelete >= 0) {
			if (wp == VK_ESCAPE) DoAction(kPickDeleteNo, 0);
			else if (wp == VK_RETURN) DoAction(kPickDeleteYes, 0);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && !TourneyRoomHiddenHeld(g_s) && !RankedRoomHidden(g_s) && (wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT || wp == VK_HOME || wp == VK_END)) {
			PickMove(wp);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && !TourneyRoomHiddenHeld(g_s) && !RankedRoomHidden(g_s) && wp == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
			DoAction(kFieldPickSearch, 0);
		}
		else if (g_s.game.picker && !SaveMapPicking(g_s) && !TourneyRoomHiddenHeld(g_s) && !RankedRoomHidden(g_s) && wp == VK_RETURN) {
			DoAction(kPickOk, 0);
		}
		else if (wp == VK_ESCAPE && g_s.game.picker && !SaveMapPicking(g_s) && !TourneyRoomHiddenHeld(g_s) && !RankedRoomHidden(g_s) && !g_s.dropOpen) {
			DoAction(kPickCancel, 0);
		}
		else if (wp == VK_ESCAPE && g_s.dropOpen) {
			g_s.dropOpen = false;
			DropFocusCombo();
		}
		else if (wp == VK_ESCAPE) {
			// A match found is answered with its buttons: Escape never declines it, it only closes the panel's colour menu.
			if (g_s.searching) {
				LeagueSideMenuClose();
			}
			else if (g_s.view == kViewRanked && g_s.ranked.disputeMatch) RankedAction(kRkDisputeCancel);
			else if (g_s.view == kViewRanked && g_s.ranked.contentOpen) RankedAction(kRkContentClose);
			else if (g_s.view == kViewRanked && g_s.ranked.inviteOpen) RankedAction(kRkDuoInviteClose);
			else if (g_s.ranked.resultOpen) RankedAction(kRkResultClose);
			else if (g_s.ranked.ceremonyOpen) RankedAction(kRkCeremonyLater);
			else if (g_s.view == kViewRanked && (g_s.ranked.sub == kRankedBoard || g_s.ranked.sub == kRankedHistory || g_s.ranked.sub == kRankedGuide)) RankedAction(kRkHub);
			else DoAction(kBack, 0);
		}
		else if (wp == VK_UP || wp == VK_DOWN) {
			SelectRoom(g_s.selected + (wp == VK_UP ? -1 : 1));
			g_s.lobbyReveal = true;
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
