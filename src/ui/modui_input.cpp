// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <mutex>
#include "ui/mpui.h"
#include "net/spectator.h"
#include "game/cheats.h"
#include "ui/savesui.h"
#include "net/discord.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
namespace {
bool TabSkip(int a)
{
	return a == kNone || a == kDropClose || a == kToggle || a == kRichLink || a == kRowClick || a == kMark ||
		a == kIoFilterClose;
}

void TabStep(bool back)
{
	FocusItem cur = {g_s.tabAction, g_s.tabArg};
	if (g_s.focus != kNoField) cur = {ActionOfField(g_s.focus), 0};
	const FocusItem next = FocusStep(FocusOrder(g_s.hits, TabSkip), cur, back);
	if (next.action < 0) return;
	g_s.focus = kNoField;
	g_s.tabAction = next.action;
	g_s.tabArg = next.arg;
	if (FieldOfAction(next.action) != kNoField) {
		DoAction(next.action, next.arg);
		g_s.tabAction = next.action;
		g_s.tabArg = 0;
	}
}

void FieldKey(UINT msg, WPARAM wp)
{
	std::string &text = FieldRef(g_s.focus);
	const size_t limit = g_s.focus == kNameField ? 60 : g_s.focus == kSearchField || g_s.focus == kIoSearchField ? 80 : 2000;
	bool changed = false;
	if (msg == WM_CHAR) {
		changed = EditChar(text, g_s.edit, wp, limit);
	}
	else if (wp == VK_RETURN) {
		if (g_s.focus == kNameField) DoAction(kNameOk, 0);
		else if (g_s.focus == kIoSearchField) {
			g_s.ioSearchDue = 0;
			g_s.ioPage = 0;
			IoSearch();
		}
		else g_s.focus = kNoField;
		return;
	}
	else if (wp == VK_ESCAPE) {
		if (g_s.focus == kNameField) DoAction(kNameCancel, 0);
		else if (g_s.focus == kIoSearchField && !g_s.ioSearch.empty()) {
			g_s.ioSearch.clear();
			g_s.edit = EditState();
			g_s.ioSearchDue = 0;
			g_s.ioPage = 0;
			IoSearch();
		}
		else if (g_s.focus == kSearchField && !g_s.searchText.empty()) {
			g_s.searchText.clear();
			g_s.edit = EditState();
		}
		else g_s.focus = kNoField;
		return;
	}
	else {
		changed = EditKey(text, g_s.edit, wp, limit) == 2;
	}
	if (changed && g_s.focus == kSearchField) g_s.list.scroll = 0;
	if (changed && g_s.focus == kIoSearchField) g_s.ioSearchDue = (GetTickCount() + 250) | 1;
}
}
}

bool ModUiHandleCornerMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	const bool keyMsg = msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR;
	const bool otherUi = keyMsg && (SavesUiIsOpen() || MpUiIsOpen());
	std::lock_guard<std::mutex> lock(g_lock);
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) CheatsPanelConsumeKeyOpen();
	if (keyMsg && g_ch.shown && CheatsPanelOpen() && !g_s.open && !otherUi && ChHandleKey(msg, wp, lp)) return true;
	if (msg == WM_MOUSEWHEEL && g_ch.shown && g_ch.panel.right > g_ch.panel.left) {
		const POINT hp = {g_hover.x, g_hover.y};
		if (PtInRect(&g_ch.panel, hp)) {
			g_ch.scroll = std::max(0.0f, std::min(g_ch.maxScroll, g_ch.scroll - static_cast<short>(HIWORD(wp)) / 120.0f * 48.0f));
			return true;
		}
	}
	if (msg == WM_MOUSEMOVE) {
		g_hover.x = static_cast<short>(LOWORD(lp));
		g_hover.y = static_cast<short>(HIWORD(lp));
		if (g_spec.drag && g_spec.barShown) {
			const RECT &t = g_spec.btn[5];
			g_spec.dragFrac = std::max(0.0, std::min(1.0, (g_hover.x - t.left) / static_cast<double>(std::max<LONG>(1, t.right - t.left))));
		}
		if (g_ch.drag && !(wp & MK_LBUTTON)) g_ch.drag = false;
		if (g_ch.drag && g_ch.shown) CheatsScrollToMouse(g_hover.y);
		if (g_ch.es.dragging && ChEditing() && g_ch.geomOk[ChSlot()]) g_ch.es.caret = EditIndexAt(g_ch.geom[ChSlot()], ChText(), g_hover.x);
		if (g_ch.move && g_ch.shown) {
			if (!g_ch.moved && std::abs(g_hover.y - g_ch.moveY0) > 4) g_ch.moved = true;
			if (g_ch.moved) g_ch.pos = std::max(0.0f, std::min(1.0f, static_cast<float>(g_hover.y - g_ch.grab) / static_cast<float>(std::max(1, g_ch.winH))));
		}
		if (g_spec.move && g_spec.barShown && g_spec.winW > 0 && g_spec.winH > 0) {
			const double barW = g_spec.bar.right - g_spec.bar.left;
			double cx = (g_hover.x - g_spec.moveDX + barW / 2) / g_spec.winW, top = static_cast<double>(g_hover.y - g_spec.moveDY) / g_spec.winH;
			SpectatorBarPos(cx, top, 1);
		}
	}
	if (g_icon.shown && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)) {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		if (msg == WM_LBUTTONDOWN && g_icon.rect.right > g_icon.rect.left && PtInRect(&g_icon.rect, pt)) {
			OpenDiscordInvite();
			g_icon.swallowUp = true;
			return true;
		}
		if (msg == WM_LBUTTONUP && g_icon.swallowUp) {
			g_icon.swallowUp = false;
			return true;
		}
	}
	if (g_spec.barShown && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)) {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		if (msg == WM_LBUTTONDOWN) {
			for (int i = 0; i < 9; i++) {
				if (g_spec.btn[i].right <= g_spec.btn[i].left || !PtInRect(&g_spec.btn[i], pt)) continue;
				if (i == 5) {
					g_spec.drag = true;
					g_spec.dragFrac = std::max(0.0, std::min(1.0, (pt.x - g_spec.btn[5].left) / static_cast<double>(std::max<LONG>(1, g_spec.btn[5].right - g_spec.btn[5].left))));
				}
				else {
					SpectatorControl(i == 6 ? 5 : i == 7 ? 6 : i == 8 ? 7 : i);
				}
				g_spec.swallowUp = true;
				return true;
			}
			if (PtInRect(&g_spec.bar, pt)) {
				g_spec.move = true;
				g_spec.moveDX = pt.x - g_spec.bar.left;
				g_spec.moveDY = pt.y - g_spec.bar.top;
				g_spec.swallowUp = true;
				return true;
			}
		}
		else {
			if (g_spec.drag) {
				g_spec.drag = false;
				SpectatorSeekFraction(g_spec.dragFrac);
				g_spec.swallowUp = false;
				return true;
			}
			if (g_spec.move) {
				g_spec.move = false;
				double cx = 0.5, top = -1;
				SpectatorBarPos(cx, top, 0);
				SpectatorBarPos(cx, top, 2);
				g_spec.swallowUp = false;
				return true;
			}
			if (g_spec.swallowUp) {
				g_spec.swallowUp = false;
				return true;
			}
		}
	}
	if (g_ch.shown && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP)) {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		if (msg != WM_LBUTTONUP) {
			for (int i = 0; i < g_ch.btnCount; i++) {
				if (!PtInRect(&g_ch.btn[i].rect, pt)) continue;
				const int v = g_ch.btn[i].verb;
				if (v == -52 || ChIsValue(v)) {
					const bool was = (v == -52 && g_ch.searchEdit) || (ChIsValue(v) && g_ch.edit == -30 - v);
					ChSetFocus(v);
					const int slot = ChSlot();
					if (g_ch.geomOk[slot]) {
						const int at = EditIndexAt(g_ch.geom[slot], ChText(), pt.x);
						g_ch.es.caret = at;
						if (!(GetKeyState(VK_SHIFT) & 0x8000)) g_ch.es.anchor = at;
					}
					if (was && msg == WM_LBUTTONDBLCLK) EditSelectWord(ChText(), g_ch.es);
					else g_ch.es.dragging = true;
				}
				else {
					g_ch.focus = kChNoFocus;
					ChActivate(v, g_ch.btn[i].amount, false);
				}
				g_ch.swallowUp = true;
				return true;
			}
			ChClearFocus();
			if (CheatsBarPress(pt)) {
				g_ch.swallowUp = true;
				return true;
			}
			if (PtInRect(&g_ch.tab, pt)) {
				CheatsPanelToggle();
				g_ch.swallowUp = true;
				return true;
			}
			if (g_ch.panel.right > g_ch.panel.left && PtInRect(&g_ch.panel, pt)) {
				g_ch.swallowUp = true;
				return true;
			}
		}
		else {
			g_ch.drag = false;
			g_ch.es.dragging = false;
			if (g_ch.move) {
				if (g_ch.moved) ChSavePos();
				else if (g_ch.move == 1) CheatsPanelToggle();
				g_ch.move = 0;
				g_ch.moved = false;
			}
			if (g_ch.swallowUp) {
				g_ch.swallowUp = false;
				return true;
			}
		}
	}
	if (g_card.shown && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)) {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		if (msg == WM_LBUTTONDOWN && PtInRect(&g_card.close, pt)) {
			DlCancel();
			g_card.swallowUp = true;
			return true;
		}
		if (msg == WM_LBUTTONUP && g_card.swallowUp) {
			g_card.swallowUp = false;
			return true;
		}
	}
	return false;
}

bool ModUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	if ((msg == WM_LBUTTONDOWN || msg == WM_KEYDOWN) && ModUiIsOpen()) ModsReadPatchedFiles();
	std::unique_lock<std::mutex> lock(g_lock);
	if (msg == WM_MOUSEMOVE) {
		g_hover.x = static_cast<short>(LOWORD(lp));
		g_hover.y = static_cast<short>(HIWORD(lp));
	}
	if (!g_s.open) {
		return false;
	}
	switch (msg) {
	case WM_MOUSEMOVE:
		g_s.mouseX = static_cast<short>(LOWORD(lp));
		g_s.mouseY = static_cast<short>(HIWORD(lp));
		if (g_s.edit.dragging && g_s.focus != kNoField) {
			FieldGeom geom;
			if (FieldGet(ActionOfField(g_s.focus), geom)) g_s.edit.caret = EditIndexAt(geom, FieldRef(g_s.focus), g_s.mouseX);
		}
		{
			const POINT at = {g_s.mouseX, g_s.mouseY};
			ListMove(g_s.list, g_s.listGeom, at);
			std::lock_guard<std::mutex> rich(g_rich.mx);
			ListMove(g_rich.list, g_rich.geom, at);
		}
		if (g_s.confirm) {
			const POINT pt = {g_s.mouseX, g_s.mouseY};
			ListMove(g_s.confirmList, g_s.confirmGeom, pt);
		}
		{
			ScrollTo to;
			if (ScrollbarMouse(g_s.dropBar, g_s.hits, msg, wp, {g_s.mouseX, g_s.mouseY}, to) && to.id == kDropMenuBar) g_s.dropScroll = to.pos;
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseX = static_cast<short>(LOWORD(lp));
		g_s.mouseY = static_cast<short>(HIWORD(lp));
		POINT pt = {g_s.mouseX, g_s.mouseY};
		g_s.tabAction = -1;
		g_s.mouseDown = true;
		{
			ScrollTo to;
			if (ScrollbarMouse(g_s.dropBar, g_s.hits, msg, wp, pt, to)) {
				if (to.id == kDropMenuBar) g_s.dropScroll = to.pos;
				return true;
			}
		}
		if (g_s.confirm && ListDown(g_s.confirmList, g_s.confirmGeom, pt)) return true;
		if (!g_s.confirm && g_s.drop == kDropNone && !g_s.ioFilterOpen && !g_s.catOpen && g_s.ioShot < 0) {
			if (g_s.tab == kTabListInstalled && ListDown(g_s.list, g_s.listGeom, pt)) return true;
			std::lock_guard<std::mutex> rich(g_rich.mx);
			if (ListDown(g_rich.list, g_rich.geom, pt)) return true;
		}
		bool hitSomething = false;
		for (auto it = g_s.hits.rbegin(); it != g_s.hits.rend(); ++it) {
			if (PtInRect(&it->r, pt)) {
				const Hit hit = *it;
				hitSomething = true;
				if (hit.action == kPickArchive) {
					g_s.focus = kNoField;
					lock.unlock();
					const std::string file = DlPickArchive();
					lock.lock();
					if (!file.empty()) {
						DlInstallArchive(file);
					}
				}
				else {
					DoAction(hit.action, hit.arg);
				}
				break;
			}
		}
		if (!hitSomething) g_s.focus = kNoField;
		else if (g_s.focus != kNoField) {
			FieldGeom geom;
			if (FieldGet(ActionOfField(g_s.focus), geom) && PtInRect(&geom.r, pt)) {
				std::string &text = FieldRef(g_s.focus);
				g_s.edit.caret = EditIndexAt(geom, text, g_s.mouseX);
				if (!(GetKeyState(VK_SHIFT) & 0x8000)) g_s.edit.anchor = g_s.edit.caret;
				if (msg == WM_LBUTTONDBLCLK) EditSelectWord(text, g_s.edit);
				else g_s.edit.dragging = true;
			}
		}
		return true;
	}
	case WM_LBUTTONUP:
		g_s.edit.dragging = false;
		g_s.mouseDown = false;
		g_s.dropBar = ScrollDrag();
		ListUp(g_s.confirmList);
		ListUp(g_s.list);
		{
			std::lock_guard<std::mutex> rich(g_rich.mx);
			ListUp(g_rich.list);
		}
		return true;
	case WM_RBUTTONDOWN: {
		const POINT pt = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
		const bool menuOpen = g_s.drop == kDropRowMenu || g_s.drop == kDropModMenu;
		if (g_s.tab != 0 || g_s.confirm || g_s.catOpen || (g_s.drop != kDropNone && !menuOpen)) return true;
		if (menuOpen) g_s.drop = kDropNone;
		for (auto it = g_s.hits.rbegin(); it != g_s.hits.rend(); ++it) {
			if (!PtInRect(&it->r, pt)) continue;
			if (menuOpen && (it->action == kDropClose || it->action == kDropPick)) continue;
			if (it->action != kRowClick && it->action != kMark && it->action != kToggle) break;
			const int row = it->arg;
			if (row < 0 || row >= static_cast<int>(g_s.rows.size())) break;
			if (!IsMarked(g_s, g_s.rows[row].folder)) g_s.marked.clear();
			g_s.selected = row;
			g_s.menuRow = row;
			g_s.menuX = pt.x;
			g_s.menuY = pt.y;
			g_s.drop = kDropRowMenu;
			g_s.dropScroll = 0;
			g_s.focus = kNoField;
			break;
		}
		return true;
	}
	case WM_RBUTTONUP:
	case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
		return true;
	case WM_MOUSEWHEEL:
		if (g_s.confirm) {
			const POINT pt = {g_s.mouseX, g_s.mouseY};
			ListWheel(g_s.confirmList, g_s.confirmGeom, pt, GET_WHEEL_DELTA_WPARAM(wp));
			return true;
		}
		if (g_s.drop != kDropNone) {
			g_s.dropScroll = std::max(0, std::min(g_s.dropScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3, DropMaxScroll(g_drop.count, g_drop.rows)));
			return true;
		}
		if (g_s.ioFilterOpen || g_s.catOpen) return true;
		{
			std::lock_guard<std::mutex> lock(g_rich.mx);
			const POINT pt = {g_s.mouseX, g_s.mouseY};
			if ((g_rich.rectValid || GetTickCount() - g_rich.rectTick < 250) && PtInRect(&g_rich.rect, pt)) {
				g_rich.list.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 40;
				if (g_rich.list.scroll < 0) g_rich.list.scroll = 0;
				return true;
			}
		}
		if (g_s.tab == 2) {
			if (g_s.ioDetail < 0) DoAction(GET_WHEEL_DELTA_WPARAM(wp) < 0 ? kIoNext : kIoPrev, 0);
			return true;
		}
		g_s.list.scroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 2;
		DoAction(kNone, 0);
		return true;
	case WM_CHAR:
		if (g_s.focus != kNoField) FieldKey(msg, wp);
		return true;
	case WM_KEYDOWN: {
		if (wp == VK_TAB) {
			TabStep((GetKeyState(VK_SHIFT) & 0x8000) != 0);
			return true;
		}
		if (g_s.focus != kNoField && !g_s.confirm) {
			FieldKey(msg, wp);
			return true;
		}
		if (g_s.tabAction >= 0 && (wp == VK_RETURN || wp == VK_SPACE)) {
			const int action = g_s.tabAction, arg = g_s.tabArg, dropWas = g_s.drop;
			if (action == kPickArchive) {
				lock.unlock();
				const std::string file = DlPickArchive();
				lock.lock();
				if (!file.empty()) DlInstallArchive(file);
			}
			else {
				DoAction(action, arg);
			}
			if (action == kDropOpen && g_s.drop != kDropNone) {
				g_s.tabAction = kDropPick;
				g_s.tabArg = std::max(0, DropCurrent(g_s));
			}
			else if (action == kDropPick && dropWas != kDropNone) {
				g_s.tabAction = kDropOpen;
				g_s.tabArg = dropWas;
			}
			return true;
		}
		if (wp == VK_ESCAPE) g_s.tabAction = -1;
		if (g_s.drop != kDropNone) {
			if (wp == VK_ESCAPE) {
				g_s.tabAction = kDropOpen;
				g_s.tabArg = g_s.drop;
				g_s.drop = kDropNone;
			}
			else if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END) {
				const int count = static_cast<int>(DropItems(g_s).size());
				if (count > 0) {
					const int cur = g_s.tabAction == kDropPick ? g_s.tabArg : DropCurrent(g_s);
					int to = wp == VK_HOME ? 0 : wp == VK_END ? count - 1 : cur < 0 ? 0 : cur + (wp == VK_UP ? -1 : 1);
					to = std::max(0, std::min(count - 1, to));
					g_s.tabAction = kDropPick;
					g_s.tabArg = to;
					if (to < g_s.dropScroll) g_s.dropScroll = to;
					else if (to >= g_s.dropScroll + kMenuMaxRows) g_s.dropScroll = to - kMenuMaxRows + 1;
				}
			}
			return true;
		}
		if (g_s.tab == 2 && !g_s.confirm) {
			if (wp == VK_ESCAPE && g_s.ioFilterOpen) g_s.ioFilterOpen = false;
			else if (g_s.ioFilterOpen) return true;
			else if (g_s.ioShot >= 0 && g_s.ioDetail >= 0) {
				if (wp == VK_ESCAPE) DoAction(kIoShotClose, 0);
				else if (wp == VK_LEFT) DoAction(kIoShotStep, -1);
				else if (wp == VK_RIGHT) DoAction(kIoShotStep, 1);
			}
			else if (wp == VK_ESCAPE && g_s.ioDetail >= 0) DoAction(kIoBack, 0);
			else if (wp == VK_ESCAPE) DoAction(kClose, 0);
			else if (wp == VK_LEFT || wp == VK_PRIOR) DoAction(kIoPrev, 0);
			else if (wp == VK_RIGHT || wp == VK_NEXT) DoAction(kIoNext, 0);
			return true;
		}
		if (g_s.catOpen && !g_s.confirm) {
			if (wp == VK_ESCAPE) g_s.catOpen = false;
			return true;
		}
		if (wp == VK_ESCAPE) {
			if (!g_s.confirm && !g_s.marked.empty()) DoAction(kClearMarks, 0);
			else DoAction(g_s.confirm ? kConfirmNo : kClose, 0);
			return true;
		}
		if (wp == VK_RETURN && g_s.confirm) {
			DoAction(kConfirmYes, 0);
			return true;
		}
		if (g_s.confirm) return true;
		const std::vector<int> view = VisibleRows(g_s);
		int pos = 0;
		for (size_t i = 0; i < view.size(); i++) {
			if (view[i] == g_s.selected) pos = static_cast<int>(i);
		}
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (wp == VK_UP && !view.empty()) g_s.selected = view[std::max(0, pos - 1)];
		else if (wp == VK_DOWN && !view.empty()) g_s.selected = view[std::min(static_cast<int>(view.size()) - 1, pos + 1)];
		else if (wp == 'A' && ctrl) {
			g_s.marked.clear();
			for (int row : view) g_s.marked.push_back(g_s.rows[row].folder);
		}
		else if (wp == VK_DELETE) AskUninstall(Targets(g_s));
		else if (wp == VK_SPACE) {
			const std::vector<std::string> targets = Targets(g_s);
			bool allOn = !targets.empty();
			for (const Row &r : g_s.rows) {
				if (std::find(targets.begin(), targets.end(), r.folder) != targets.end() && !r.active) allOn = false;
			}
			SetActive(targets, !allOn);
			g_s.restartNeeded = ModsRestartNeeded();
		}
		for (size_t i = 0; i < view.size(); i++) {
			if (view[i] == g_s.selected) pos = static_cast<int>(i);
		}
		const int visible = g_installedRows.load();
		if (pos < g_s.list.scroll) g_s.list.scroll = pos;
		if (pos >= g_s.list.scroll + visible) g_s.list.scroll = pos - visible + 1;
		return true;
	}
	case WM_KEYUP:
		return true;
	}
	return false;
}
