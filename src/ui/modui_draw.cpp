// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <mutex>
#include "ui/updateui.h"
#include "ui/mpui.h"
#include "net/spectator.h"
#include "game/cheats.h"
#include "ui/savesui.h"
#include "ui/support.h"
#include "ui/controlsui.h"
#include "addr/state.h"

#include "ui/modui_internal.h"

using namespace modui_impl;

bool ModUiNeedsDraw()
{
	const bool inGame = *reinterpret_cast<void **>(addr::kGame) != nullptr;
	SpectatorState spState;
	const bool menu = !inGame && MpUiMainMenuVisible() && !MpUiIsOpen() && !SavesUiIsOpen() && !ControlsUiNeedsDraw();
	std::lock_guard<std::mutex> lock(g_lock);
	g_icon.shown = menu && !g_s.open;
	return g_s.open || (!g_s.toast.empty() && static_cast<int>(g_s.toastUntil - GetTickCount()) > 0) || DlStatus().active || g_icon.shown || SpectatorViewing() || SpectatorControls(spState) || CheatsPanelAvailable();
}

void ModUiDraw(uint32_t *buf, int w, int h)
{
	{
		std::lock_guard<std::mutex> lock(g_rich.mx);
		g_rich.rectValid = false;
	}
	State st;
	bool timed = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.confirm && g_s.confirmDeadline && static_cast<int>(GetTickCount() - g_s.confirmDeadline) >= 0) DoAction(kConfirmAlt, 0);
		timed = g_s.confirm && g_s.confirmDeadline;
		if (!g_s.open) ModsWatchStop();
		if (g_s.open && ModsListSerial() != g_s.listSerial) {
			const Row *sel = SelectedRow(g_s);
			const std::string keep = sel ? sel->folder : "";
			Reload();
			if (!keep.empty()) SelectFolder(keep);
			g_s.marked.erase(std::remove_if(g_s.marked.begin(), g_s.marked.end(), [](const std::string &f) { return !FindMod(f); }), g_s.marked.end());
		}
		if (g_s.open) {
			g_s.dl = DlStatus();
			if (g_s.dl.finished != g_s.dlFinished) {
				g_s.dlFinished = g_s.dl.finished;
				g_s.mods = ModsSummaries();
				BuildRows();
				BuildUsage();
				g_s.restartNeeded = ModsRestartNeeded();
			}
			if (!g_s.importWant.empty() || !g_s.importLoading.empty()) ImportTick();
			if (g_s.tab == 2 && g_s.ioSearchDue && static_cast<int>(GetTickCount() - g_s.ioSearchDue) >= 0) {
				g_s.ioSearchDue = 0;
				g_s.ioPage = 0;
				IoSearch();
			}
			if (g_s.tab == 2) {
				const uint64_t before = g_s.io.serial;
				g_s.io = GbGetResults();
				if (g_s.io.serial != before) g_s.ioDetail = -1;
				if (!g_s.io.loading) g_s.ioRefreshing = false;
			}
		}
		// Closed, only the toast is drawn from the state: the mod list and the catalogue it keeps are not copied on every pass.
		if (g_s.open) st = g_s;
		else {
			st.toast = g_s.toast;
			st.toastUntil = g_s.toastUntil;
			st.toastLen = g_s.toastLen;
			st.toastErr = g_s.toastErr;
		}
	}
	const float s = FitScale(w, h, kDesignW, kDesignH, 24);
	if (st.open) {
		int confirmW = kConfirmW, confirmH = kConfirmH;
		ConfirmSize(st, confirmW, confirmH);
		PanelRender r(g_canvas, buf, w, h);
		// The content screen fills the whole screen; a question asked on its own stays a dialog over the dimmed game.
		const ScreenView view = ScreenViewFor(w, h);
		if (!st.confirmOnly) {
			st.viewW = view.w;
			st.viewH = view.h;
		}
		if (st.confirmOnly ? r.Begin(confirmW, confirmH, 24, true, s) : BeginFullScreen(r, view)) {
			r.Focus(st.tabAction, st.tabArg);
			r.Press(st.hits, st.mouseDown, POINT{st.mouseX, st.mouseY});
			if (st.confirmOnly) PaintConfirm(r.p, st);
			else PaintPanel(r.p, st);
			r.End(255);
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.hits = std::move(r.hits);
			g_s.panes = std::move(r.panes);
			g_s.confirmGeom = g_confirmGeomOut;
			g_s.listGeom = g_listGeomOut;
		}
	}
	// In the menu the game sends no frames: the seconds of a timed question ask for the next one.
	if (timed) MpUiWakeGameThread();
	const int left = static_cast<int>(st.toastUntil - GetTickCount());
	if (!st.toast.empty() && left > 0) {
		const int shown = static_cast<int>(st.toastLen) - left;
		const int alpha = std::max(0, static_cast<int>(240 * std::min({1.0f, shown / 200.0f, left / 500.0f})));
		Card card;
		card.text = st.toast;
		card.accent = st.toastErr ? pal::bad : pal::redText;
		DrawCard(g_toastCanvas, buf, w, h, card, s, alpha);
	}
	bool iconShown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		iconShown = g_icon.shown;
	}
	if (iconShown) {
		DrawVersionCorner(buf, w, h);
		DrawDiscordCorner(buf, w, h);
		const RECT discord = DiscordIconRect(w, h);
		SupportDrawCorner(buf, w, h, discord);
		{
			const int size = discord.right - discord.left, gap = std::max(8, size / 4);
			const RECT heart = {discord.left, discord.top - gap - size, discord.right, discord.top - gap};
			UpdateUiDrawCorner(buf, w, h, heart);
		}
		SupportDrawPanel(buf, w, h);
		UpdateUiDrawPanel(buf, w, h);
	}
	{
		DrawSpectatorBar(buf, w, h, s, static_cast<int>(12 * s));
		DrawWatchersBadge(buf, w, h);
		DrawCheatsPanel(buf, w, h, s);
	}
	const DownloadStatus dl = DlStatus();
	RECT closeBox = {};
	bool cardShown = false;
	if (!dl.active) g_card.rate.Reset();
	else {
		g_card.rate.Sample(dl.done, GetTickCount());
		Card card;
		const bool installing = dl.unpacking || (dl.total && dl.done >= dl.total);
		const bool unpackShown = dl.unpacking && dl.unpackTotal;
		card.text = std::string(installing ? Tr("Installing ") : Tr("Downloading ")) + (dl.what.empty() ? std::string("mod") : dl.what);
		card.progress = unpackShown ? static_cast<float>(static_cast<double>(dl.unpacked) / dl.unpackTotal)
					   : dl.total ? static_cast<float>(dl.done) / static_cast<float>(dl.total) : 0.0f;
		card.sub = (unpackShown ? TransferText(dl.unpacked, dl.unpackTotal, 0) : TransferText(dl.done, dl.total, installing ? 0 : g_card.rate.perSecond)) +
			(dl.queued ? "  ·  +" + std::to_string(dl.queued) + Tr(" queued") : "");
		card.close = true;
		card.corner = true;
		card.fixedW = 400;
		const RECT r = DrawCard(g_card.canvas, buf, w, h, card, s, 240);
		closeBox = {r.right - static_cast<LONG>(30 * s), r.top, r.right, r.top + static_cast<LONG>(30 * s)};
		cardShown = r.right > r.left;
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_card.close = closeBox;
	g_card.shown = cardShown;
}
