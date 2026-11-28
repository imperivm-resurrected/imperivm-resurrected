// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/optionsui.h"
#include "ui/chatui.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "addr/state.h"
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
#include "ui/fullscreen.h"
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
	std::vector<PaneArea> panes;
	float scale = 1.0f;
	bool mouseDown = false, escArmed = false, enterArmed = false;
	DWORD lastRead = 0, closedAt = 0;
};

std::mutex g_lock;
State g_s;
Canvas g_canvas;

Values ViewValues()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_s.view.values;
}

bool CheckOr(const Values &v, int id, bool fallback)
{
	const auto it = v.checks.find(id);
	return it == v.checks.end() ? fallback : it->second;
}

int ScrollOr(const Values &v, int id, int fallback)
{
	const auto it = v.scrolls.find(id);
	return it == v.scrolls.end() ? fallback : it->second;
}

template <typename T>
void CopyId(std::map<int, T> &to, const std::map<int, T> &from, int id)
{
	const auto it = from.find(id);
	if (it != from.end()) to[id] = it->second;
}

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

const char *const kCornerLabels[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
const int kPerfWindows[] = {10, 30, 60};
const char *const kPerfWindowLabels[] = {"10 s", "30 s", "60 s"};

// The corner the panel is nearest to: one dragged elsewhere keeps its place until another corner is picked.
int NearestCorner(const Config &c)
{
	return (c.panelX >= 500 ? 1 : 0) + (c.panelY >= 500 ? 2 : 0);
}

int PerfWindowIndex(int seconds)
{
	int best = 0;
	for (int i = 0; i < 3; i++) {
		if (abs(kPerfWindows[i] - seconds) < abs(kPerfWindows[best] - seconds)) best = i;
	}
	return best;
}

void SetPanelValues(Values &v, const Config &c)
{
	v.checks[kPanelCompact] = c.panelCompact;
	v.scrolls[kPanelOpacity] = c.panelOpacity;
	v.scrolls[kPanelScale] = c.perfScale;
	v.combos[kPanelCorner] = NearestCorner(c);
	v.combos[kPerfWindow] = PerfWindowIndex(c.perfWindow);
}

void SetPanelExtras(Values &v, const Config &c)
{
	v.items[kPanelCorner] = std::vector<std::string>(std::begin(kCornerLabels), std::end(kCornerLabels));
	v.items[kPerfWindow] = std::vector<std::string>(std::begin(kPerfWindowLabels), std::end(kPerfWindowLabels));
	SetPanelValues(v, c);
}

// The panel's settings that its own menu also changes.
void ApplyPanelSettings(Config &c, const Values &v)
{
	c.panelCompact = CheckOr(v, kPanelCompact, c.panelCompact);
	c.panelOpacity = ScrollOr(v, kPanelOpacity, c.panelOpacity);
	c.perfScale = ScrollOr(v, kPanelScale, c.perfScale);
	const int corner = ComboIndex(v, kPanelCorner, 4);
	if (corner >= 0 && corner != NearestCorner(c)) {
		c.panelX = (corner % 2) * 1000;
		c.panelY = (corner / 2) * 1000;
	}
	const int window = ComboIndex(v, kPerfWindow, 3);
	if (window >= 0) c.perfWindow = kPerfWindows[window];
}

void SetDisplayExtras(Values &v, const Config &c)
{
	v.items[kFpsLimit] = std::vector<std::string>(std::begin(kFpsLabels), std::end(kFpsLabels));
	v.combos[kFpsLimit] = FpsIndex(c.fpsLimit);
}

void CopyDisplayExtras(Values &to, const Values &from)
{
	CopyId(to.combos, from.combos, kFpsLimit);
	CopyId(to.items, from.items, kFpsLimit);
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

const int kSoundScrolls[] = {kMasterVolume, kNatureVolume, kConversationVolume, kAlertVolume};
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
	CopyId(to.combos, from.combos, kLogLevel);
	CopyId(to.items, from.items, kLogLevel);
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
	SetPanelExtras(v, c);
	v.checks[kSmoothCamera] = c.smoothScroll;
	v.combos[kHealthBars] = c.healthBarOwners;
	v.combos[kAllyPalette] = c.allyPalette;
	v.combos[kAllySelf] = static_cast<int>(c.allyRgb[0]);
	v.combos[kAllyAllies] = static_cast<int>(c.allyRgb[1]);
	v.combos[kAllyEnemies] = static_cast<int>(c.allyRgb[2]);
	v.checks[kHealthBarHeroes] = c.healthBarHeroesOnly;
	v.checks[kRespawnTimers] = c.respawnTimers;
	v.checks[kPauseInBackground] = c.pauseInBackground;
	v.checks[kChatBadges] = c.chatBadges;
	v.checks[kMatchFoundSound] = c.matchFoundSound;
	v.checks[kAlertFront] = c.alertFront;
	v.checks[kAllyColors] = c.allyColors;
	v.scrolls[kMasterVolume] = c.masterVolume;
	v.scrolls[kNatureVolume] = c.natureVolume;
	v.scrolls[kAlertVolume] = c.alertVolume;
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
	if (OptionsSpeedFixed()) v.disabled[kGameSpeed] = true;
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
	v.disabled[kResetChatPlace] = !ChatUiPositionMoved();
	v.disabled[kCheckUpdates] = info.check == UpdaterInfo::kChecking || info.state == UpdaterInfo::kDownloading || info.state == UpdaterInfo::kReady;
}

void CopyExtras(Values &to, const Values &from)
{
	CopySystemExtras(to, from);
	for (int id : {kSmoothCamera, kPauseInBackground, kHealthBarHeroes, kChatBadges, kMatchFoundSound, kAlertFront, kAllyColors, kRespawnTimers}) CopyId(to.checks, from.checks, id);
	CopyId(to.combos, from.combos, kHealthBars);
	CopyId(to.combos, from.combos, kAllyPalette);
	for (int id : {kAllySelf, kAllyAllies, kAllyEnemies}) CopyId(to.combos, from.combos, id);
	CopyDisplayExtras(to, from);
	for (int id : kSoundScrolls) CopyId(to.scrolls, from.scrolls, id);
	CopyId(to.combos, from.combos, kOutputDevice);
	CopyId(to.items, from.items, kOutputDevice);
	CopyId(to.disabled, from.disabled, kOutputDevice);
	CopyId(to.items, from.items, kPanelCorner);
	CopyId(to.items, from.items, kPerfWindow);
	// The panel changes as it is edited, so the setting is the truth; its own menu and keys change it too.
	SetPanelValues(to, GetConfig());
}

void ApplyPanelLive(const Values &v)
{
	Config &c = MutableConfig();
	c.showPerf = CheckOr(v, kShowPerf, c.showPerf);
	c.showNet = CheckOr(v, kShowNet, c.showNet);
	ApplyPanelSettings(c, v);
}

// The volumes are heard while they are dragged; Cancel puts back the ones of the baseline.
void ApplySoundLive(const Values &v)
{
	Config &c = MutableConfig();
	c.masterVolume = SoundMasterPercent(ScrollOr(v, kMasterVolume, c.masterVolume));
	c.natureVolume = ScrollOr(v, kNatureVolume, c.natureVolume);
	c.alertVolume = std::max(1, std::min(100, ScrollOr(v, kAlertVolume, c.alertVolume)));
	c.conversationVolume = SoundTypeVolume(ScrollOr(v, kConversationVolume, c.conversationVolume), c.conversationVolume);
	DmShimSetMasterVolume(c.masterVolume);
	SoundLevelsApply();
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
	ApplyPanelSettings(c, v);
	c.smoothScroll = CheckOr(v, kSmoothCamera, c.smoothScroll);
	const auto bars = v.combos.find(kHealthBars);
	if (bars != v.combos.end()) c.healthBarOwners = bars->second;
	const auto palette = v.combos.find(kAllyPalette);
	if (palette != v.combos.end()) c.allyPalette = palette->second >= 0 && palette->second <= 2 ? palette->second : 0;
	{
		const int ids[3] = {kAllySelf, kAllyAllies, kAllyEnemies};
		for (int i = 0; i < 3; i++) {
			const auto color = v.combos.find(ids[i]);
			if (color != v.combos.end()) c.allyRgb[i] = static_cast<uint32_t>(color->second) & 0xffffff;
		}
	}
	c.healthBarHeroesOnly = CheckOr(v, kHealthBarHeroes, c.healthBarHeroesOnly);
	c.respawnTimers = CheckOr(v, kRespawnTimers, c.respawnTimers);
	c.pauseInBackground = CheckOr(v, kPauseInBackground, c.pauseInBackground);
	c.chatBadges = CheckOr(v, kChatBadges, c.chatBadges);
	c.matchFoundSound = CheckOr(v, kMatchFoundSound, c.matchFoundSound);
	c.alertFront = CheckOr(v, kAlertFront, c.alertFront);
	c.allyColors = CheckOr(v, kAllyColors, c.allyColors);
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
RECT g_colorRect = {};

// A click or a drag in the colour picker: the colour changes and the palette becomes the player's own. Under the lock.
void PickColor(View &v, int part, const RECT &hit, POINT at)
{
	if (!v.colorId) return;
	int &value = v.values.combos[v.colorId];
	value = static_cast<int>(PickedColor(part, hit, at.x, at.y, static_cast<uint32_t>(value)));
	v.values.combos[kAllyPalette] = 2;
	v.dirty = !(v.values == g_s.baseline);
}

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
		windowed = CheckOr(v, kWindowed, GetConfig().windowed);
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
		ApplyPanelLive(ViewValues());
		break;
	case kActSlider:
		SetSlider(cmd.arg, cmd.value);
		ApplyPanelLive(ViewValues());
		if (IsExtra(cmd.arg)) ApplySoundLive(ViewValues());
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
				// A preset fills the three colours; the player's own keeps them.
				if (cmd.value == kAllyPalette && cmd.arg >= 0 && cmd.arg < 2) {
					const int ids[3] = {kAllySelf, kAllyAllies, kAllyEnemies};
					for (int i = 0; i < 3; i++) g_s.view.values.combos[ids[i]] = static_cast<int>(kAllyPresets[cmd.arg][i]);
				}
				g_s.view.dirty = !(g_s.view.values == g_s.baseline);
			}
			ApplyDisplayLive(ViewValues());
			ApplyPanelLive(ViewValues());
			break;
		}
		OptionsWriteCombo(cmd.value, cmd.arg);
		Refresh();
		if (cmd.value == kMonitor) PreviewResolutions();
		break;
	case kActRow:
		if (cmd.arg == kOpenLogs) ReportOpenLogsFolder();
		else if (cmd.arg == kCreateReport) ReportStart();
		else if (cmd.arg == kResetChatPlace) {
			ChatUiResetPosition();
			Refresh();
		}
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
		ApplyDisplayLive(ViewValues());
		OptionsWriteScroll(kBrightness, OptionsReadScroll(kBrightness, 100));
		Refresh();
		PreviewResolutions();
		ApplyPanelLive(ViewValues());
		ApplySoundLive(ViewValues());
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
	ScrollTo to;
	const bool onBar = ScrollbarMouse(v.dropBar, g_s.hits, msg, wp, at, to);
	if (to.id == kDropMenuBar) v.dropScroll = to.pos;
	else v.scroll.Apply(g_s.panes, to);
	switch (msg) {
	case WM_MOUSEMOVE:
		v.mouseX = at.x;
		v.mouseY = at.y;
		if (v.dragId) {
			cmd.action = kActSlider;
			cmd.arg = v.dragId;
			cmd.value = SliderValue(g_s, v.dragId, g_dragRect, at.x);
		}
		if (v.colorDrag) PickColor(v, v.colorDrag, g_colorRect, at);
		return false;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK: {
		if (onBar) return true;
		g_s.mouseDown = true;
		int arg = 0;
		RECT rect = {};
		const int action = HitAt(g_s, at, arg, &rect);
		switch (action) {
		case kActPage:
			v.page = arg;
			v.dropId = 0;
			v.scroll.Reset();
			break;
		case kActCombo:
			v.dropId = v.dropId == arg ? 0 : arg;
			v.dropScroll = 0;
			break;
		case kActDropClose: v.dropId = 0; break;
		case kActColor:
			v.colorId = v.colorId == arg ? 0 : arg;
			v.dropId = 0;
			break;
		case kActColorClose: v.colorId = 0; break;
		case kActColorWheel:
		case kActColorValue:
			v.colorDrag = action;
			g_colorRect = rect;
			PickColor(v, action, rect, at);
			break;
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
		v.colorDrag = 0;
		return true;
	case WM_MOUSEWHEEL:
		if (v.dropId) {
			const Row *row = FindRow(g_s, v.dropId);
			const int count = row ? static_cast<int>(row->items.size()) : 0;
			const int max = DropMaxScroll(count, std::min(count, kMenuMaxRows));
			v.dropScroll = std::max(0, std::min(max, v.dropScroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA));
		}
		// The wheel's message carries where the mouse is on the screen, not in the window.
		else if (!v.dragId && !v.colorId) v.scroll.Wheel(g_s.panes, {v.mouseX, v.mouseY}, GET_WHEEL_DELTA_WPARAM(wp));
		return true;
	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) {
			if (v.dropId) v.dropId = 0;
			else if (v.colorId) v.colorId = 0;
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

std::vector<std::string> OptionsUiPageTitles()
{
	std::vector<std::string> titles;
	for (const Page &page : BuildPages(ViewValues())) titles.push_back(page.title);
	return titles;
}

void OptionsUiShowPage(int page)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_s.dlg || g_s.closedAt) return;
	g_s.view.page = page;
	g_s.view.dropId = 0;
	g_s.view.scroll.Reset();
}

void OptionsUiBack()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || g_s.closedAt) return;
	}
	Command cmd;
	cmd.action = kActCancel;
	Execute(cmd);
}

bool OptionsUiNeedsDraw()
{
	void *dlg = OptionsDialogOpen();
	std::lock_guard<std::mutex> lock(g_lock);
	if (!dlg) return false;
	// The game's dialog is open but not read yet: the screen's backdrop already covers it, so it never shows for a frame.
	if (g_s.dlg != dlg) return true;
	return !g_s.closedAt || GetTickCount() - g_s.closedAt < kCloseTimeoutMs;
}

void OptionsUiDraw(uint32_t *buf, int w, int h)
{
	View view;
	std::vector<Hit> previous;
	bool mouseDown = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (!g_s.dlg || OptionsDialogOpen() != g_s.dlg) {
			if (OptionsDialogOpen()) {
				PanelRender shell(g_canvas, buf, w, h);
				if (*reinterpret_cast<void **>(addr::kGame) == nullptr) {
					if (BeginFullScreen(shell, ScreenViewFor(w, h))) shell.End();
				}
				else if (BeginWindow(shell, kScreenW, kScreenH, 16)) {
					PaintWindowCard(shell.p, kScreenW, kScreenH);
					shell.End();
				}
			}
			return;
		}
		view = g_s.view;
		previous = g_s.hits;
		mouseDown = g_s.mouseDown;
	}
	// Over a running game the screen stays a window, so the map shows around it.
	view.fullScreen = *reinterpret_cast<void **>(addr::kGame) == nullptr;
	PanelRender r(g_canvas, buf, w, h);
	if (view.fullScreen) {
		const ScreenView screen = ScreenViewFor(w, h);
		view.screenW = screen.w;
		view.screenH = screen.h;
		if (!BeginFullScreen(r, screen)) return;
	}
	else if (!BeginWindow(r, kScreenW, kScreenH, 16)) return;
	r.Press(previous, mouseDown, {view.mouseX, view.mouseY});
	PaintOptionsScreen(r.p, view);
	r.End();
	std::lock_guard<std::mutex> lock(g_lock);
	g_s.hits = std::move(r.hits);
	g_s.panes = std::move(r.panes);
	g_s.scale = r.s;
}

bool OptionsUiHandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	// OK/Cancel/Apply close on the press: its release would reach the game's main menu under them, which acts on releases.
	static bool swallowRelease = false;
	if (msg == WM_LBUTTONUP && swallowRelease) {
		swallowRelease = false;
		return true;
	}
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) swallowRelease = false;
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
	if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_s.closedAt) swallowRelease = true;
	}
	return consumed;
}
