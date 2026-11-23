// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <mutex>

#include "hooks/alerts.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/log.h"
#include "hooks/audio.h"
#include "hooks/displaymode.h"
#include "hooks/dmshim.h"

namespace {

const int kHornTone = 8;
// The horn lasts about 1.2 s: the background mute waits a little more before it closes again.
const DWORD kHornHoldMs = 1600;
// A countdown cancelled and started again in the same room does not call again before this.
const uint32_t kCountdownQuietMs = 20000;

std::mutex g_gateMx;
PlayerAlertGate g_countdownGate;

const char *KindName(PlayerAlert kind)
{
	switch (kind) {
	case PlayerAlert::MatchFound: return "ranked match found";
	case PlayerAlert::CupMatchReady: return "cup match ready";
	case PlayerAlert::Countdown: return "room countdown";
	default: return "game starting";
	}
}

// Windows lets a process take the foreground only in some cases: joined to the input of the window that has it, it usually may. When it
// still refuses, the taskbar's flash stays.
bool BringToFront(HWND w)
{
	if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
	const HWND front = GetForegroundWindow();
	const DWORD self = GetCurrentThreadId();
	const DWORD other = front ? GetWindowThreadProcessId(front, nullptr) : 0;
	const bool attached = other && other != self && AttachThreadInput(self, other, TRUE);
	BringWindowToTop(w);
	const bool ok = SetForegroundWindow(w) != FALSE;
	if (attached) AttachThreadInput(self, other, FALSE);
	return ok && GetForegroundWindow() == w;
}

void Call(PlayerAlertPlan plan, HWND w, PlayerAlert kind)
{
	if (plan.flash && w) {
		FLASHWINFO fi = {sizeof(fi), w, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
		FlashWindowEx(&fi);
	}
	if (plan.front && w && !BringToFront(w)) LogD("ui", "the call to the player (%s) could not bring the window to the front; the taskbar flashes", KindName(kind));
	if (plan.sound) {
		AudioAlertHold(true);
		UiSoundPlay(kHornTone);
		Sleep(kHornHoldMs);
		AudioAlertHold(false);
	}
}

}

void AlertPlayer(PlayerAlert kind)
{
	const Config &c = GetConfig();
	PlayerAlertOptions o;
	o.sound = c.onlineAlerts;
	o.matchSound = c.matchFoundSound;
	o.front = c.alertFront;
	const HWND w = DisplayGameWindow();
	const bool foreground = w && GetForegroundWindow() == w;
	const PlayerAlertPlan plan = PlayerAlertPlanFor(kind, foreground, o);
	if (!plan.sound && !plan.flash && !plan.front) return;
	LogI("ui", "calling the player: %s (%s%s%s)", KindName(kind), plan.sound ? "horn" : "silent", plan.flash ? ", flash" : "", plan.front ? ", to the front" : "");
	// Flashing and activating the window send it messages at once, which come back into the menu's handlers on the caller's thread while
	// it may hold the menu's lock: all of it goes from another thread.
	SafeThread(Call, plan, w, kind).detach();
}

void AlertPlayerCountdown(uint64_t room)
{
	{
		std::lock_guard<std::mutex> lock(g_gateMx);
		if (!g_countdownGate.Allow(room, GetTickCount(), kCountdownQuietMs)) return;
	}
	AlertPlayer(PlayerAlert::Countdown);
}
