// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/options.h"
#include "i18n/tr.h"
#include "ui/modui.h"
#include "ui/optionsui_view.h"
#include "ui/report.h"
#include "ui/widgets.h"

using namespace ui;
using namespace optionsui;

namespace {

const int kNativeOk = 0x1002, kNativeCancel = 0x1003, kNativeApply = 0x1114, kNativeReset = 0x1115, kNativeControls = 0x111B;
const int kNativeChecks[] = {kMusicOn, kEffectsOn, kNatureOn, kSpeechOn, kConversationsOn, kWindowed, kVSync, kTrapMouse, kMuteBackground, kShowPerf, kAutosave, kMovieOn, kAlerts, kSaveOnExit, kShowNet};
const int kNativeScrolls[] = {kMusicVolume, kEffectsVolume, kSpeechVolume, kGameSpeed, kScrollSpeed, kBrightness, kMovieVolume};
const int kNativeCombos[] = {kResolution, kAutosaveEvery, kAutosaveCopies, kLanguage, kMonitor, kUpdates};
const DWORD kReadEveryMs = 100, kCloseTimeoutMs = 2000;

struct State {
	void *dlg = nullptr;
	View view;
	Values baseline;
	std::vector<Hit> hits;
	float scale = 1.0f;
	bool mouseDown = false;
	DWORD lastRead = 0, closedAt = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

void SetExtras(Values &v, const Config &c)
{
	v.checks[kPanelCompact] = c.panelCompact;
	v.checks[kSmoothCamera] = c.smoothScroll;
	v.checks[kModernAudio] = c.modernAudio;
	v.scrolls[kPanelOpacity] = c.panelOpacity;
}

Values ReadNative()
{
	Values v;
	for (int id : kNativeChecks) v.checks[id] = OptionsReadCheck(id, false);
	for (int id : kNativeScrolls) v.scrolls[id] = OptionsReadScroll(id, 0);
	for (int id : kNativeCombos) {
		v.combos[id] = OptionsReadCombo(id);
		v.items[id] = OptionsComboItems(id);
	}
	return v;
}

void CopyExtras(Values &to, const Values &from)
{
	for (int id : {kPanelCompact, kSmoothCamera, kModernAudio}) {
		const auto it = from.checks.find(id);
		if (it != from.checks.end()) to.checks[id] = it->second;
	}
	const auto opacity = from.scrolls.find(kPanelOpacity);
	if (opacity != from.scrolls.end()) to.scrolls[kPanelOpacity] = opacity->second;
}

void Refresh(bool setBaseline = false)
{
	void *dlg = OptionsDialogOpen();
	if (!dlg) return;
	Values fresh = ReadNative();
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_s.dlg != dlg) {
		g_s = State();
		g_s.dlg = dlg;
		SetExtras(fresh, GetConfig());
		setBaseline = true;
		LogD("options", "the new options screen opens");
	}
	else {
		CopyExtras(fresh, g_s.view.values);
	}
	g_s.view.values = std::move(fresh);
	if (setBaseline) g_s.baseline = g_s.view.values;
	g_s.view.dirty = !(g_s.view.values == g_s.baseline);
	g_s.lastRead = GetTickCount();
}

bool CommitExtras(const Values &v)
{
	Config &c = MutableConfig();
	const auto check = [&](int id, bool fallback) {
		const auto it = v.checks.find(id);
		return it == v.checks.end() ? fallback : it->second;
	};
	const bool audioBefore = c.modernAudio;
	c.panelCompact = check(kPanelCompact, c.panelCompact);
	c.smoothScroll = check(kSmoothCamera, c.smoothScroll);
	c.modernAudio = check(kModernAudio, c.modernAudio);
	const auto opacity = v.scrolls.find(kPanelOpacity);
	if (opacity != v.scrolls.end()) c.panelOpacity = opacity->second;
	SaveConfig();
	return c.modernAudio != audioBefore;
}

struct Command {
	int action = kActNone;
	int arg = 0;
	int value = 0;
	Values values;
};

bool IsExtra(int id) { return id >= kPanelCompact && id <= kModernAudio; }

int HitAt(const State &st, POINT at, int &arg, RECT *rect = nullptr)
{
	for (auto it = st.hits.rbegin(); it != st.hits.rend(); ++it) {
		if (!PtInRect(&it->r, at)) continue;
		arg = it->arg;
		if (rect) *rect = it->r;
		return it->action;
	}
	return kActNone;
}

const Row *FindRow(const State &st, int id)
{
	static std::vector<Page> pages;
	pages = BuildPages(st.view.values);
	for (const Page &page : pages) {
		for (const std::vector<Card> *column : {&page.left, &page.right}) {
			for (const Card &card : *column) {
				for (const Row &row : card.rows) {
					if (row.id == id) return &row;
				}
			}
		}
	}
	return nullptr;
}

int SliderValue(const State &st, int id, const RECT &hit, int mouseX)
{
	const Row *row = FindRow(st, id);
	if (!row) return 0;
	const int margin = static_cast<int>(kSliderMargin * st.scale + 0.5f);
	const int left = hit.left + margin, width = std::max(1, static_cast<int>(hit.right - hit.left) - margin * 2);
	const float f = std::max(0.0f, std::min(1.0f, static_cast<float>(mouseX - left) / width));
	return row->min + static_cast<int>((row->max - row->min) * f + 0.5f);
}

RECT g_dragRect = {};

void SetSlider(int id, int value)
{
	if (IsExtra(id)) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_s.view.values.scrolls[id] = value;
		g_s.view.dirty = !(g_s.view.values == g_s.baseline);
		return;
	}
	OptionsWriteScroll(id, value);
	Refresh();
}

void Execute(const Command &cmd)
{
	switch (cmd.action) {
	case kActToggle:
		if (IsExtra(cmd.arg)) {
			std::lock_guard<std::mutex> lock(g_lock);
			bool &on = g_s.view.values.checks[cmd.arg];
			on = !on;
			g_s.view.dirty = !(g_s.view.values == g_s.baseline);
		}
		else {
			OptionsWriteCheck(cmd.arg, !OptionsReadCheck(cmd.arg, false));
			Refresh();
		}
		break;
	case kActSlider: SetSlider(cmd.arg, cmd.value); break;
	case kActSegment:
		OptionsWriteCheck(cmd.arg >> 4, (cmd.arg & 15) != 0);
		Refresh();
		break;
	case kActPick:
		OptionsWriteCombo(cmd.value, cmd.arg);
		Refresh();
		break;
	case kActRow:
		if (cmd.arg == kOpenLogs) ReportOpenLogsFolder();
		else if (cmd.arg == kCreateReport) ReportStart();
		break;
	case kActControls: OptionsClick(kNativeControls); break;
	case kActReset: {
		OptionsClick(kNativeReset);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			SetExtras(g_s.view.values, Config());
		}
		Refresh();
		break;
	}
	case kActApply: {
		const bool audio = CommitExtras(cmd.values);
		OptionsClick(kNativeApply);
		Refresh(true);
		if (audio) UiAskRestart(Tr("The game will restart to change the sound system."));
		break;
	}
	case kActOk: {
		const bool audio = CommitExtras(cmd.values);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = GetTickCount();
		}
		OptionsClick(kNativeOk);
		if (audio) UiAskRestart(Tr("The game will restart to change the sound system."));
		break;
	}
	case kActCancel: {
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = GetTickCount();
		}
		OptionsClick(kNativeCancel);
		break;
	}
	default: break;
	}
}

// Everything that touches the game runs without the lock: its dialog can call back into the mod.
bool Handle(UINT msg, WPARAM wp, LPARAM lp, Command &cmd)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg) return false;
	View &v = g_s.view;
	const POINT at = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (v.dragId) {
			cmd.action = kActSlider;
			cmd.arg = v.dragId;
			cmd.value = SliderValue(g_s, v.dragId, g_dragRect, at.x);
		}
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		g_s.mouseDown = true;
		int arg = 0;
		RECT rect = {};
		const int action = HitAt(g_s, at, arg, &rect);
		switch (action) {
		case kActPage:
			v.page = arg;
			v.dropId = 0;
			break;
		case kActCombo:
			v.dropId = v.dropId == arg ? 0 : arg;
			v.dropScroll = 0;
			break;
		case kActDropClose: v.dropId = 0; break;
		case kActPick:
			cmd.action = action;
			cmd.arg = arg;
			cmd.value = v.dropId;
			v.dropId = 0;
			break;
		case kActSlider:
			v.dragId = arg;
			g_dragRect = rect;
			cmd.action = action;
			cmd.arg = arg;
			cmd.value = SliderValue(g_s, arg, rect, at.x);
			break;
		case kActApply:
		case kActOk:
			cmd.action = action;
			cmd.values = v.values;
			break;
		case kActNone: break;
		default:
			cmd.action = action;
			cmd.arg = arg;
			break;
		}
		return true;
	}
	case WM_LBUTTONUP:
		g_s.mouseDown = false;
		v.dragId = 0;
		return true;
	case WM_MOUSEWHEEL:
		if (v.dropId) {
			const Row *row = FindRow(g_s, v.dropId);
			const int count = row ? static_cast<int>(row->items.size()) : 0;
			const int max = DropMaxScroll(count, std::min(count, kMenuMaxRows));
			v.dropScroll = std::max(0, std::min(max, v.dropScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA));
		}
		return true;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) {
			if (v.dropId) v.dropId = 0;
			else cmd.action = kActCancel;
		}
		else if (wp == VK_RETURN && !v.dropId) {
			cmd.action = kActOk;
			cmd.values = v.values;
		}
		return true;
	case WM_KEYUP:
	case WM_CHAR:
		return true;
	default:
		return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
	}
}

}

void OptionsUiGameTick()
{
	void *dlg = OptionsDialogOpen();
	bool due = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!dlg) {
			if (g_s.dlg) g_s = State();
			return;
		}
		due = g_s.dlg != dlg || (!g_s.closedAt && GetTickCount() - g_s.lastRead >= kReadEveryMs);
	}
	if (due) Refresh();
}

bool OptionsUiNeedsDraw()
{
	void *dlg = OptionsDialogOpen();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!dlg || g_s.dlg != dlg) return false;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void OptionsUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || OptionsDialogOpen() != g_s.dlg) return;
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	PanelRender r(g_canvas, buf, w, h);
	if (!r.Begin(kScreenW, kScreenH, 16)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOptionsScreen(r.p, view);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.scale = r.s;
}

bool OptionsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	void *dlg = OptionsDialogOpen();
	if (!dlg || ModUiIsOpen()) return false;
	{
		bool fresh = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			fresh = g_s.dlg != dlg;
			if (!fresh && g_s.closedAt) return msg >= WM_KEYFIRST && msg <= WM_KEYLAST && msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP;
		}
		if (fresh) Refresh();
	}
	Command cmd;
	const bool consumed = Handle(msg, wp, lp, cmd);
	if (cmd.action != kActNone) Execute(cmd);
	return consumed;
}
