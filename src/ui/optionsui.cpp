// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/uiport.h"
#include "game/options.h"
#include "hooks/audio.h"
#include "hooks/dmshim.h"
#include "hooks/soundlevels.h"
#include "i18n/tr.h"
#include "mods/updater.h"
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
	bool mouseDown = false, escArmed = false, enterArmed = false;
	DWORD lastRead = 0, closedAt = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

const int kFpsLimits[] = {0, 30, 60, 120, 144, 240};
const char *const kFpsLabels[] = {"No limit", "30", "60", "120", "144", "240"};

int FpsIndex(int fps)
{
	int best = 0;
	for (int i = 0; i < static_cast<int>(sizeof(kFpsLimits) / sizeof(kFpsLimits[0])); i++) {
		if (abs(kFpsLimits[i] - fps) < abs(kFpsLimits[best] - fps)) best = i;
	}
	return best;
}

int ComboIndex(const Values &v, int id, int count)
{
	const auto it = v.combos.find(id);
	return it != v.combos.end() && it->second >= 0 && it->second < count ? it->second : -1;
}

void SetDisplayExtras(Values &v, const Config &c)
{
	v.items[kFpsLimit] = std::vector<std::string>(std::begin(kFpsLabels), std::end(kFpsLabels));
	v.combos[kFpsLimit] = FpsIndex(c.fpsLimit);
}

void CopyDisplayExtras(Values &to, const Values &from)
{
	for (int id : {kFpsLimit}) {
		const auto sel = from.combos.find(id);
		if (sel != from.combos.end()) to.combos[id] = sel->second;
		const auto items = from.items.find(id);
		if (items != from.items.end()) to.items[id] = items->second;
	}
}

void ApplyDisplayLive(const Values &v)
{
	const int fps = ComboIndex(v, kFpsLimit, static_cast<int>(sizeof(kFpsLimits) / sizeof(kFpsLimits[0])));
	if (fps >= 0) MutableConfig().fpsLimit = kFpsLimits[fps];
}

void CommitDisplayExtras(Config &c, const Values &v)
{
	const int fps = ComboIndex(v, kFpsLimit, static_cast<int>(sizeof(kFpsLimits) / sizeof(kFpsLimits[0])));
	if (fps >= 0) c.fpsLimit = kFpsLimits[fps];
}

void ApplyDisplayFromView()
{
	Values values;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		values = g_s.view.values;
	}
	ApplyDisplayLive(values);
}

const int kSoundScrolls[] = {kMasterVolume, kNatureVolume, kConversationVolume};
const char *const kDefaultDevice = "Default device";

void SetDevices(Values &v, const std::string &wanted)
{
	const std::vector<std::string> names = DmShimOutputDevices();
	std::vector<std::string> &items = v.items[kOutputDevice];
	items.assign(1, kDefaultDevice);
	items.insert(items.end(), names.begin(), names.end());
	v.combos[kOutputDevice] = SoundPickDevice(names, wanted) + 1;
	v.disabled[kOutputDevice] = !DmShimReady();
}

void SetSystemExtras(Values &v, const Config &c)
{
	v.combos[kLogLevel] = c.logLevel <= kLogDebug ? 1 : 0;
	v.items[kLogLevel] = {"Normal", "Detailed"};
}

void CopySystemExtras(Values &to, const Values &from)
{
	for (int id : {kLogLevel}) {
		const auto combo = from.combos.find(id);
		if (combo != from.combos.end()) to.combos[id] = combo->second;
		const auto items = from.items.find(id);
		if (items != from.items.end()) to.items[id] = items->second;
	}
}

// "Normal" and "Detailed" keep a level set by hand in the ini (trace, warn) until they are changed.
void CommitSystemExtras(const Values &v)
{
	Config &c = MutableConfig();
	const auto level = v.combos.find(kLogLevel);
	if (level == v.combos.end() || (level->second == 1) == (c.logLevel <= kLogDebug)) return;
	c.logLevel = level->second == 1 ? kLogDebug : kLogInfo;
	LogSetLevel(c.logLevel);
	SaveConfig();
}

void SetExtras(Values &v, const Config &c)
{
	SetSystemExtras(v, c);
	SetDisplayExtras(v, c);
	v.checks[kPanelCompact] = c.panelCompact;
	v.checks[kSmoothCamera] = c.smoothScroll;
	v.scrolls[kPanelOpacity] = c.panelOpacity;
	v.combos[kHealthBars] = c.healthBarOwners;
	v.checks[kHealthBarHeroes] = c.healthBarHeroesOnly;
	v.checks[kPauseInBackground] = c.pauseInBackground;
	v.scrolls[kMasterVolume] = c.masterVolume;
	v.scrolls[kNatureVolume] = c.natureVolume;
	v.scrolls[kConversationVolume] = c.conversationVolume;
	SetDevices(v, c.outputDevice);
}

Values ReadNative()
{
	Values v;
	for (int id : kNativeChecks) {
		v.checks[id] = OptionsReadCheck(id, false);
		v.disabled[id] = !OptionsControlEnabled(id);
	}
	for (int id : kNativeScrolls) {
		v.scrolls[id] = OptionsReadScroll(id, 0);
		v.disabled[id] = !OptionsControlEnabled(id);
	}
	for (int id : kNativeCombos) {
		v.combos[id] = OptionsReadCombo(id);
		v.items[id] = OptionsComboItems(id);
		v.disabled[id] = !OptionsControlEnabled(id);
	}
	return v;
}

// The update panel only draws on the main menu, which is hidden while this screen is open, so the result goes in the row.
void SetUpdateStatus(Values &v)
{
	const UpdaterInfo info = UpdaterGetInfo();
	std::string text;
	switch (info.state) {
	case UpdaterInfo::kAvailable: text = TrFmt("Version %s is out: see the main menu", {info.newVersion}); break;
	case UpdaterInfo::kDownloading: text = TrFmt("Downloading %s...", {info.newVersion}) + " " + std::to_string(info.percent) + " %"; break;
	case UpdaterInfo::kReady: text = TrFmt("Version %s installed: restart the game", {info.newVersion}); break;
	case UpdaterInfo::kFailed: text = Tr("Couldn't update: see the main menu"); break;
	default:
		if (info.check == UpdaterInfo::kChecking) text = Tr("Checking...");
		else if (info.check == UpdaterInfo::kUpToDate) text = Tr("You have the latest version");
		else if (info.check == UpdaterInfo::kCheckFailed) text = Tr("Couldn't check. Try again later");
		break;
	}
	v.texts[kCheckUpdates] = text;
	v.disabled[kCheckUpdates] = info.check == UpdaterInfo::kChecking || info.state == UpdaterInfo::kDownloading || info.state == UpdaterInfo::kReady;
}

void CopyExtras(Values &to, const Values &from)
{
	CopySystemExtras(to, from);
	for (int id : {kSmoothCamera, kPauseInBackground, kHealthBarHeroes}) {
		const auto it = from.checks.find(id);
		if (it != from.checks.end()) to.checks[id] = it->second;
	}
	for (int id : {kHealthBars}) {
		const auto it = from.combos.find(id);
		if (it != from.combos.end()) to.combos[id] = it->second;
	}
	CopyDisplayExtras(to, from);
	for (int id : kSoundScrolls) {
		const auto it = from.scrolls.find(id);
		if (it != from.scrolls.end()) to.scrolls[id] = it->second;
	}
	const auto combo = from.combos.find(kOutputDevice);
	if (combo != from.combos.end()) to.combos[kOutputDevice] = combo->second;
	const auto items = from.items.find(kOutputDevice);
	if (items != from.items.end()) to.items[kOutputDevice] = items->second;
	const auto disabled = from.disabled.find(kOutputDevice);
	if (disabled != from.disabled.end()) to.disabled[kOutputDevice] = disabled->second;
	// The panel changes as it is edited, so the setting is the truth; its own mouse controls change it too.
	to.checks[kPanelCompact] = GetConfig().panelCompact;
	to.scrolls[kPanelOpacity] = GetConfig().panelOpacity;
}

void ApplyPanelLive(const Values &v)
{
	Config &c = MutableConfig();
	const auto check = [&](int id, bool fallback) {
		const auto it = v.checks.find(id);
		return it == v.checks.end() ? fallback : it->second;
	};
	c.showPerf = check(kShowPerf, c.showPerf);
	c.showNet = check(kShowNet, c.showNet);
	c.panelCompact = check(kPanelCompact, c.panelCompact);
	const auto opacity = v.scrolls.find(kPanelOpacity);
	if (opacity != v.scrolls.end()) c.panelOpacity = opacity->second;
}

// The volumes are heard while they are dragged; Cancel puts back the ones of the baseline.
void ApplySoundLive(const Values &v)
{
	Config &c = MutableConfig();
	const auto scroll = [&](int id, int fallback) {
		const auto it = v.scrolls.find(id);
		return it == v.scrolls.end() ? fallback : it->second;
	};
	c.masterVolume = SoundMasterPercent(scroll(kMasterVolume, c.masterVolume));
	c.natureVolume = scroll(kNatureVolume, c.natureVolume);
	c.conversationVolume = SoundTypeVolume(scroll(kConversationVolume, c.conversationVolume), c.conversationVolume);
	DmShimSetMasterVolume(c.masterVolume);
	SoundLevelsApply();
}

void ApplySoundFromView()
{
	Values values;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		values = g_s.view.values;
	}
	ApplySoundLive(values);
}

void ApplyPanelFromView()
{
	Values values;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		values = g_s.view.values;
	}
	ApplyPanelLive(values);
}

void Refresh(bool setBaseline = false)
{
	void *dlg = OptionsDialogOpen();
	if (!dlg) return;
	Values fresh = ReadNative();
	SetUpdateStatus(fresh);
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

// A device that is not connected shows as the default one: it is only replaced when the player picks another.
void CommitDevice(const Values &v)
{
	Values baseline;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		baseline = g_s.baseline;
	}
	const auto now = v.combos.find(kOutputDevice);
	const auto before = baseline.combos.find(kOutputDevice);
	const auto items = v.items.find(kOutputDevice);
	if (now == v.combos.end() || items == v.items.end() || (before != baseline.combos.end() && before->second == now->second)) return;
	const int index = now->second;
	const std::string name = index > 0 && index < static_cast<int>(items->second.size()) ? items->second[index] : std::string();
	MutableConfig().outputDevice = name;
	LogI("options", "sound output: %s", name.empty() ? "default device" : name.c_str());
	DmShimSetOutputDevice(name);
}

void CommitExtras(const Values &v)
{
	Config &c = MutableConfig();
	const auto check = [&](int id, bool fallback) {
		const auto it = v.checks.find(id);
		return it == v.checks.end() ? fallback : it->second;
	};
	c.panelCompact = check(kPanelCompact, c.panelCompact);
	c.smoothScroll = check(kSmoothCamera, c.smoothScroll);
	const auto opacity = v.scrolls.find(kPanelOpacity);
	if (opacity != v.scrolls.end()) c.panelOpacity = opacity->second;
	const auto bars = v.combos.find(kHealthBars);
	if (bars != v.combos.end()) c.healthBarOwners = bars->second;
	c.healthBarHeroesOnly = check(kHealthBarHeroes, c.healthBarHeroesOnly);
	c.pauseInBackground = check(kPauseInBackground, c.pauseInBackground);
	CommitDisplayExtras(c, v);
	ApplySoundLive(v);
	CommitDevice(v);
	SaveConfig();
}

struct Command {
	int action = kActNone;
	int arg = 0;
	int value = 0;
	Values values;
};

bool IsExtra(int id) { return id >= 0x9000 && id < 0x9100; }

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

// The resolution list follows the monitor and mode picked here before they are applied.
void PreviewResolutions()
{
	int monitor = 0;
	bool windowed = false, extended = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const Values &v = g_s.view.values;
		const auto combo = v.combos.find(kMonitor);
		monitor = combo == v.combos.end() ? GetConfig().monitor : combo->second;
		const auto check = [&](int id, bool fallback) {
			const auto it = v.checks.find(id);
			return it == v.checks.end() ? fallback : it->second;
		};
		windowed = check(kWindowed, GetConfig().windowed);
		extended = GetConfig().extendedView;
	}
	OptionsPreviewResolutions(monitor, windowed, extended);
	Refresh();
}

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
			{
				std::lock_guard<std::mutex> lock(g_lock);
				bool &on = g_s.view.values.checks[cmd.arg];
				on = !on;
				g_s.view.dirty = !(g_s.view.values == g_s.baseline);
			}
		}
		else {
			OptionsWriteCheck(cmd.arg, !OptionsReadCheck(cmd.arg, false));
			Refresh();
		}
		ApplyPanelFromView();
		break;
	case kActSlider:
		SetSlider(cmd.arg, cmd.value);
		ApplyPanelFromView();
		if (IsExtra(cmd.arg)) ApplySoundFromView();
		break;
	case kActSegment:
		OptionsWriteCheck(cmd.arg >> 4, (cmd.arg & 15) != 0);
		Refresh();
		if (cmd.arg >> 4 == kWindowed) PreviewResolutions();
		break;
	case kActPick:
		if (IsExtra(cmd.value)) {
			{
				std::lock_guard<std::mutex> lock(g_lock);
				g_s.view.values.combos[cmd.value] = cmd.arg;
				g_s.view.dirty = !(g_s.view.values == g_s.baseline);
			}
			ApplyDisplayFromView();
			break;
		}
		OptionsWriteCombo(cmd.value, cmd.arg);
		Refresh();
		if (cmd.value == kMonitor) PreviewResolutions();
		break;
	case kActRow:
		if (cmd.arg == kOpenLogs) ReportOpenLogsFolder();
		else if (cmd.arg == kCreateReport) ReportStart();
		else if (cmd.arg == kCheckUpdates) {
			UpdaterCheckNow();
			Refresh();
		}
		break;
	case kActControls: OptionsClick(kNativeControls); break;
	case kActReset: {
		OptionsClick(kNativeReset);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			SetExtras(g_s.view.values, Config());
			ApplyPanelLive(g_s.view.values);
		}
		ApplyDisplayFromView();
		OptionsWriteScroll(kBrightness, OptionsReadScroll(kBrightness, 100));
		Refresh();
		PreviewResolutions();
		ApplyPanelFromView();
		ApplySoundFromView();
		break;
	}
	case kActApply: {
		CommitSystemExtras(cmd.values);
		CommitExtras(cmd.values);
		OptionsClick(kNativeApply);
		Refresh(true);
		break;
	}
	case kActOk: {
		CommitSystemExtras(cmd.values);
		CommitExtras(cmd.values);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = GetTickCount();
		}
		OptionsClick(kNativeOk);
		break;
	}
	case kActCancel: {
		Values baseline;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_s.closedAt = GetTickCount();
			ApplyPanelLive(g_s.baseline);
			baseline = g_s.baseline;
		}
		ApplyDisplayLive(baseline);
		ApplySoundLive(baseline);
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
			else g_s.escArmed = true;
		}
		else if (wp == VK_RETURN && !v.dropId) g_s.enterArmed = true;
		return true;
	// On the release: the game opens its menu when Esc comes up, so closing on the press would open it behind.
	case WM_KEYUP:
		if (wp == VK_ESCAPE && g_s.escArmed) {
			g_s.escArmed = false;
			cmd.action = kActCancel;
		}
		else if (wp == VK_RETURN && g_s.enterArmed) {
			g_s.enterArmed = false;
			cmd.action = kActOk;
			cmd.values = v.values;
		}
		return true;
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
