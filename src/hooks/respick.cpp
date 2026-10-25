// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/respick.h"

#include <algorithm>

namespace {

int IndexOf(const std::vector<ResSize> &list, ResSize size)
{
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i].w == size.w && list[i].h == size.h) return static_cast<int>(i);
	}
	return -1;
}

}

ResPick PickStartResolution(const std::vector<ResSize> &list, ResSize saved, ResSize savedOn, ResSize screen, bool savedIsMonitorMode)
{
	ResPick pick;
	const int savedAt = saved.w > 0 ? IndexOf(list, saved) : -1;
	const bool fits = saved.w <= screen.w && saved.h <= screen.h;
	const bool sameScreen = savedOn.w == screen.w && savedOn.h == screen.h;
	// A size the monitor has is shown as it is (switching the monitor if needed); a larger one is drawn and
	// shrunk (extended view), kept only on the screen it was chosen on.
	if (savedAt >= 0 && (fits || sameScreen || savedIsMonitorMode)) {
		pick.index = savedAt;
		pick.why = ResChoice::kSaved;
	}
	else if ((pick.index = IndexOf(list, screen)) >= 0) {
		pick.why = ResChoice::kScreen;
	}
	else if (!list.empty()) {
		pick.index = 0;
		pick.why = ResChoice::kFirst;
	}
	return pick;
}

// The game's menus are a 1366x768 frame: its own list never went below 1280 wide or 768 high. The
// screen's own size is offered anyway, so a smaller screen still has one.
const int kMinW = 1280, kMinH = 768, kMaxW = 4096, kMaxH = 2304;

std::vector<ResSize> BuildResolutionList(const std::vector<ResSize> &windows, ResSize screen, const std::vector<ResSize> &extended, bool monitorModesBeyondScreen)
{
	std::vector<ResSize> out;
	auto add = [&](ResSize s, bool beyondScreen) {
		const bool isScreen = s.w == screen.w && s.h == screen.h;
		if (s.w <= 0 || s.h <= 0 || s.w > kMaxW || s.h > kMaxH) return;
		if (!isScreen && (s.w < kMinW || s.h < kMinH)) return;
		if (!beyondScreen && (s.w > screen.w || s.h > screen.h)) return;
		if (IndexOf(out, s) < 0) out.push_back(s);
	};
	add(screen, false);
	for (ResSize s : windows) add(s, monitorModesBeyondScreen);
	for (ResSize s : extended) add(s, true);
	std::sort(out.begin(), out.end(), [](ResSize a, ResSize b) { return a.w != b.w ? a.w > b.w : a.h > b.h; });
	return out;
}

int KeepResolution(const std::vector<ResSize> &list, ResSize size, ResSize screen)
{
	const int at = IndexOf(list, size);
	return at >= 0 ? at : IndexOf(list, screen);
}
