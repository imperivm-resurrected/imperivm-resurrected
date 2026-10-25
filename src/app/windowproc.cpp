// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "app/overlay.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/log.h"
#include "game/autosave.h"
#include "game/cheats.h"
#include "game/controls.h"
#include "game/loadcurtain.h"
#include "game/zoomview.h"
#include "hooks/audio.h"
#include "hooks/displaymode.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "ui/controlsui.h"
#include "ui/modui.h"
#include "ui/mpui.h"
#include "ui/savesui.h"
#include "ui/support.h"
#include "ui/updateui.h"

namespace {

bool g_closing = false;

LRESULT GameWindowHandler(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == kMsgMpUiWake) {
		MpUiWakeHandled();
		Guard("game queue", [] {
			AutosaveQuitIfReady();
			MpUiGameThreadTick();
			RejoinMenuTick();
			SpectatorGameThreadTick();
		});
		return 0;
	}
	MpUiSetGameWindow(hwnd);
	// A screen that waits for the player is not a loading one.
	if ((msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_KEYDOWN) && LoadCurtainUp()) LoadCurtainLift("the player acts");
	if (OverlayHandleMouse(hwnd, msg, wp, lp)) return 0;
	bool uiHandled = false;
	Guard("interface message", [&] {
		uiHandled = UpdateUiHandleMessage(msg, wp, lp) || ControlsUiHandleMessage(msg, wp, lp) || SupportHandleMessage(msg, wp, lp) ||
			ModUiHandleCornerMessage(msg, wp, lp) || SavesUiHandleMessage(msg, wp, lp) || ModUiHandleMessage(msg, wp, lp) || MpUiHandleMessage(msg, wp, lp);
	});
	if (uiHandled) return 0;
	if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
		bool zoomHandled = false;
		Guard("zoom map", [&] { zoomHandled = ZoomViewHandleMouse(hwnd, msg, wp, lp); });
		if (zoomHandled) return 0;
	}
	if (SpectatorHandleKey(msg, static_cast<unsigned>(wp), static_cast<long>(lp))) return 0;
	if (CheatsHandleKey(msg, static_cast<unsigned>(wp), static_cast<long>(lp))) return 0;
	if (OverlayHandleKey(msg, wp, lp)) return 0;
	{
		unsigned vk;
		bool down;
		if (ControlsMouseButton(msg, static_cast<unsigned>(wp), vk, down)) {
			DisplayForwardMessage(hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, down ? 1 : 0xC0000001);
			return msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK ? TRUE : 0;
		}
	}
	if (ControlsEatChar(msg, static_cast<unsigned>(wp))) return 0;
	if (msg == WM_SIZE && DisplayIsWindowed()) {
		InvalidateRect(hwnd, nullptr, FALSE);
	}
	if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1 << 29)) && !(lp & (1 << 30))) {
		Config &c = MutableConfig();
		c.windowed = !c.windowed;
		SaveConfig();
		LogI("display", "Alt+Enter: %s", c.windowed ? "windowed mode" : "full screen");
		DisplayApplySettings(true);
		return 0;
	}
	if (msg == WM_SYSCHAR && wp == VK_RETURN) {
		return 0;
	}
	if (msg == WM_ACTIVATEAPP) LogI("display", "window %s", wp ? "returns to the foreground" : "goes to the background");
	if (msg == WM_ACTIVATEAPP && GetConfig().muteInBackground && !g_closing) {
		AudioSetBackgroundMute(wp == FALSE);
	}
	if (msg == WM_CLOSE || msg == WM_DESTROY || (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_CLOSE)) {
		if (!g_closing) LogI("display", "window: closing (message 0x%x)", msg);
		g_closing = true;
		AudioSetBackgroundMute(false);
	}
	return DisplayForwardMessage(hwnd, msg, wp, lp);
}

const bool g_registered = (DisplaySetWindowHandler(&GameWindowHandler), true);

}
