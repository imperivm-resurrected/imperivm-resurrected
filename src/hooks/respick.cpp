// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/respick.h"

#include <algorithm>
#include <cmath>

namespace {

int IndexOf(const std::vector<ResSize> &list, ResSize size)
{
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i].w == size.w && list[i].h == size.h) return static_cast<int>(i);
	}
	return -1;
}

bool LargerFirst(ResSize a, ResSize b)
{
	return a.w != b.w ? a.w > b.w : a.h > b.h;
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

// Every size Windows reports for the monitor is offered, at any colour depth or refresh rate, except:
// - below 1024 wide or 768 high: the game lays its menus out on a frame of that size centred on the
//   screen (the conquest map is 1024x768, the other menus are 768 high), so a smaller image cuts them;
//   1024x768 is also the size the game itself falls back to. The screen's own size is offered anyway, so
//   a smaller screen still has one.
// - beyond the software frame's pixel budget, regardless of its aspect ratio.
// - above the size Windows uses for the monitor, unless allowed: in full screen the mod switches the
//   monitor to that mode; a window cannot, and in a window the list is the one of BuildWindowResolutionList.
constexpr int kMinW = 1024, kMinH = 768, kFramePixels = 4096 * 2304;

bool FrameSizeSupported(ResSize size)
{
	return size.w > 0 && size.h > 0 && size.w <= kMaxFrameSide && size.h <= kMaxFrameSide && size.w <= kFramePixels / size.h;
}

ResSize WindowFrameForClient(ResSize client)
{
	if (client.w <= 0 || client.h <= 0) return {};
	if (client.w >= kMinW && client.h >= kMinH && FrameSizeSupported(client)) return client;
	// The menus need 1024x768. Fit larger or smaller clients uniformly and let the presenter scale back.
	const double minScale = std::max(double(kMinW) / client.w, double(kMinH) / client.h);
	const double maxScale = std::min({double(kMaxFrameSide) / client.w, double(kMaxFrameSide) / client.h,
		std::sqrt(double(kFramePixels) / client.w / client.h)});
	const double scale = std::min(maxScale, std::max(1.0, minScale));
	const ResSize frame = {std::max(kMinW, int(client.w * scale)), std::max(kMinH, int(client.h * scale))};
	return FrameSizeSupported(frame) ? frame : ResSize{};
}

std::vector<ResSize> BuildResolutionList(const std::vector<ResSize> &windows, ResSize screen, const std::vector<ResSize> &extended, bool monitorModesBeyondScreen)
{
	std::vector<ResSize> out;
	auto add = [&](ResSize s, bool beyondScreen) {
		const bool isScreen = s.w == screen.w && s.h == screen.h;
		if (!FrameSizeSupported(s)) return;
		if (!isScreen && (s.w < kMinW || s.h < kMinH)) return;
		if (!beyondScreen && (s.w > screen.w || s.h > screen.h)) return;
		if (IndexOf(out, s) < 0) out.push_back(s);
	};
	add(screen, false);
	for (ResSize s : windows) add(s, monitorModesBeyondScreen);
	for (ResSize s : extended) add(s, true);
	std::sort(out.begin(), out.end(), LargerFirst);
	return out;
}

std::vector<ResSize> BuildWindowResolutionList(const std::vector<ResSize> &windows, ResSize screen, ResSize area)
{
	const std::vector<ResSize> all = BuildResolutionList(windows, screen, {});
	std::vector<ResSize> out;
	for (ResSize s : all) {
		if (s.w <= area.w && s.h <= area.h) out.push_back(s);
	}
	// A screen too small for any size the game takes, with the frame and the taskbar, still gets its own:
	// the window is then kept inside the work area and the image shrunk a little.
	if (out.empty() && !all.empty()) out.push_back(all.front());
	return out;
}

int KeepResolution(const std::vector<ResSize> &list, ResSize size, ResSize screen)
{
	const int at = IndexOf(list, size);
	return at >= 0 ? at : IndexOf(list, screen);
}
