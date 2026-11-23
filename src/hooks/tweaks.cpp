// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hooks/codepatch.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "core/uiport.h"
#include "i18n/tr.h"
#include "hooks/displaymode.h"
#include "hooks/focuspause.h"
#include "hooks/gamestate.h"
#include "hooks/respick.h"
#include "core/log.h"
#include "hooks/tweaks.h"
#include "addr/engine.h"
#include "addr/state.h"
#include "addr/net.h"
#include "addr/script.h"
#include "addr/ui.h"
#include "addr/world.h"
#include "addr/media.h"

namespace {
const uint8_t kCrashFixOriginal[] = {0x8b, 0x11, 0xff, 0x52, 0x50};
const uint8_t kCrashFixStub[] = {
	0x8b, 0x41, 0x04,
	0x8b, 0x40, 0x04,
	0x8d, 0x4c, 0x01, 0x04,
	0x85, 0xc9,
	0x74, 0x16,
	0x8b, 0x11,
	0xff, 0x35, 0xb8, 0x12, 0x82, 0x00,
	0xff, 0x52, 0x04,
	0x85, 0xc0,
	0x74, 0x07,
	0x8b, 0xc8,
	0x8b, 0x10,
	0xff, 0x62, 0x50,
	0xb8, 0x01, 0x00, 0x00, 0x00,
	0xc3,
};

int g_crashFixSites = 0;

void InstallCrashFix()
{
	void *stub = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!stub) {
		LogE("boot", "crash protection: could not reserve memory");
		return;
	}
	memcpy(stub, kCrashFixStub, sizeof(kCrashFixStub));
	int done = 0;
	for (const uintptr_t site : addr::kCrashFixSites) {
		uint8_t *at = reinterpret_cast<uint8_t *>(site);
		if (memcmp(at, kCrashFixOriginal, sizeof(kCrashFixOriginal)) != 0) {
			if (at[0] == 0xe8) done++;
			else LogW("boot", "crash protection: the code at %08x is not the expected one, left as is", static_cast<unsigned>(site));
			continue;
		}
		uint8_t call[5];
		CallBytes(call, site, reinterpret_cast<uintptr_t>(stub));
		if (WriteCode(at, call, sizeof(call))) done++;
	}
	LogI("boot", "crash protection: %d of %d sites protected", done, static_cast<int>(sizeof(addr::kCrashFixSites) / sizeof(addr::kCrashFixSites[0])));
	g_crashFixSites = done;
}

// The room's Leave (button 0x9007 of CVXUIMultiplayerGame::OnMessage) leaves the GameSpy staging room whenever the online service
// exists, and the service only gets its peer once GameSpy's availability check has answered (COnlineService::Think). A room made
// straight from the menu and left before that, or with GameSpy unreachable, closed the game reading peer+0x60.
typedef void(__cdecl *PeerLeaveRoom_t)(void *peer, int roomType, const char *reason);
PeerLeaveRoom_t g_origPeerLeaveRoom = nullptr;

void __cdecl HookPeerLeaveRoom(void *peer, int roomType, const char *reason)
{
	if (!peer) {
		LogI("net", "GameSpy: no peer to leave the room with, skipped");
		return;
	}
	g_origPeerLeaveRoom(peer, roomType, reason);
}

void RepaintClosedMenusWhole()
{
	const uint8_t original[] = {0x6a, 0x01, 0x6a, 0x00, 0xff, 0x50, 0x30};
	uint8_t *at = reinterpret_cast<uint8_t *>(addr::kMenuSwitchInvalidate);
	if (memcmp(at, original, sizeof(original)) != 0) {
		LogW("display", "menus: the menu switch code is not the expected one, a closed menu may leave its image behind");
		return;
	}
	const uint8_t push0 = 0x00;
	if (!WriteCode(at + 1, &push0, 1)) return;
	LogI("display", "menus: a closed menu is repainted whole, also under its own opaque background");
}

typedef bool(__cdecl *InitResolutionList_t)();
typedef void(__cdecl *LoadScrollSettings_t)();

typedef void *(__cdecl *ChangeDisplayModeTo_t)(DWORD, DWORD, unsigned, int);

InitResolutionList_t g_origInitResolutionList = nullptr;
LoadScrollSettings_t g_origLoadScroll = nullptr;
ChangeDisplayModeTo_t g_origChangeDisplayModeTo = nullptr;

void *__cdecl HookChangeDisplayModeTo(DWORD w, DWORD h, unsigned freq, int testOnly)
{
	if (DisplayFakeMode(static_cast<int>(w), static_cast<int>(h), 16, testOnly != 0)) {
		return nullptr;
	}
	return g_origChangeDisplayModeTo(w, h, freq, testOnly);
}

typedef void(__fastcall *PushBack_t)(void *vec, void *edx, const POINT *p);

typedef void(__fastcall *OptionsBlock_t)(unsigned *block);
OptionsBlock_t g_origSaveOptions = nullptr;
const int kResolutionSlot = 0xd;
POINT g_sizeAtStart = {};

int ResolutionCount()
{
	auto begin = *reinterpret_cast<POINT **>(addr::kResolutionList + 4);
	auto end = *reinterpret_cast<POINT **>(addr::kResolutionList + 8);
	return begin ? static_cast<int>(end - begin) : 0;
}

int ResolutionIndexOf(int w, int h)
{
	auto begin = *reinterpret_cast<POINT **>(addr::kResolutionList + 4);
	for (int i = 0, n = ResolutionCount(); i < n; i++) {
		if (begin[i].x == w && begin[i].y == h) return i;
	}
	return -1;
}

std::vector<ResSize> ResolutionSizes()
{
	std::vector<ResSize> sizes;
	auto begin = *reinterpret_cast<POINT **>(addr::kResolutionList + 4);
	for (int i = 0, n = ResolutionCount(); i < n; i++) sizes.push_back({begin[i].x, begin[i].y});
	return sizes;
}

ResSize ScreenSize()
{
	ResSize s;
	DisplayTargetSize(s.w, s.h);
	return s;
}

// While the options are open, the list follows the monitor, mode and extended view shown there, before they
// are applied; the size the game is at is kept apart, since that list may not have it.
bool g_previewing = false;
ResolutionChoice g_preview;
ResSize g_heldSize;
int g_customWindowSlot = -1;

bool ResizeGameClient(int w, int h)
{
	if (g_previewing || !ResolutionCount() || !FrameSizeSupported({w, h})) return false;
	auto &begin = *reinterpret_cast<POINT **>(addr::kResolutionList + 4);
	auto &end = *reinterpret_cast<POINT **>(addr::kResolutionList + 8);
	auto &current = *reinterpret_cast<int *>(addr::kCurrentResolutionIndex);
	const int previous = current;
	int index = ResolutionIndexOf(w, h);
	const int count = ResolutionCount();
	const bool replace = index < 0 && g_customWindowSlot >= 0 && g_customWindowSlot == count - 1;
	const bool append = index < 0 && !replace;
	const POINT size = {w, h};
	POINT old = {};
	if (replace) {
		index = g_customWindowSlot;
		old = begin[index];
		begin[index] = size;
	} else if (append) {
		index = count;
		reinterpret_cast<PushBack_t>(addr::kVectorPointPushBack)(reinterpret_cast<void *>(addr::kResolutionList), nullptr, &size);
	}

	// Use the same operation as the native Options dialog so the map, camera and UI surfaces resize together.
	current = index;
	const uintptr_t resize = addr::kApplyResolutionIndex;
	int error;
	__asm {
		mov eax, index
		call resize
		mov error, eax
	}
	if (error) {
		if (replace) begin[index] = old;
		if (append) end = begin + count;
		current = previous;
		LogW("display", "the game could not resize its canvas to %dx%d", w, h);
		return false;
	}
	if (append) g_customWindowSlot = index;
	g_sizeAtStart = size;
	return true;
}

void SetResolutionIndex(int index)
{
	if (index < 0) return;
	*reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = index;
	const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
	g_sizeAtStart = size;
}

// Where the size saved for a mode is in the list: that size, else the screen's own, else the first.
ResPick SavedPick(bool windowed)
{
	const Config &c = GetConfig();
	const SavedSize s = ConfigSavedSize(c, windowed);
	const ResSize screen = ScreenSize();
	if (windowed && DisplayFakesModeChange() && c.windowClientW > 0 && c.windowClientH > 0) {
		ResSize area;
		DisplayWindowArea(-1, area.w, area.h);
		const ResSize canvas = WindowFrameForClient({(std::min)(area.w, c.windowClientW), (std::min)(area.h, c.windowClientH)});
		if (canvas.w > 0) return PickStartResolution(ResolutionSizes(), canvas, screen, screen);
	}
	return PickStartResolution(ResolutionSizes(), {s.w, s.h}, {s.screenW, s.screenH}, screen, DisplayMonitorHasMode(s.w, s.h));
}

void UseSavedResolution()
{
	const bool windowed = DisplayIsWindowed();
	const SavedSize s = ConfigSavedSize(GetConfig(), windowed);
	const ResSize screen = ScreenSize();
	const ResPick pick = SavedPick(windowed);
	SetResolutionIndex(pick.index);
	static const char *const kWhy[] = {"the saved size", "the screen's own size", "the first of the list", "none"};
	LogI("display", "resolution at start (%s): saved %dx%d (chosen on a %dx%d screen), the screen is %dx%d -> %ldx%ld, %s", windowed ? "window" : "full screen",
		s.w, s.h, s.screenW, s.screenH, screen.w, screen.h, g_sizeAtStart.x, g_sizeAtStart.y, kWhy[static_cast<int>(pick.why)]);
}

void SaveSize(bool windowed, ResSize size)
{
	const ResSize screen = ScreenSize();
	SavedSize s;
	s.w = size.w;
	s.h = size.h;
	s.screenW = screen.w;
	s.screenH = screen.h;
	ConfigSetSavedSize(MutableConfig(), windowed, s);
	SaveConfig();
	LogI("display", "resolution saved for the %s: %dx%d, chosen on a %dx%d screen", windowed ? "window" : "full screen", size.w, size.h, screen.w, screen.h);
}

// g_gameOptions: 8 the game speed in percent, 13 the resolution (kResolutionSlot). A match's speed is game+0x1244, 700..3000, and
// ApplyGameOptions sends it as percent * 0x8fd / 100 + 700.
const int kSpeedSlot = 8, kOptionsCount = 15;
const size_t kGameSpeed = 0x1244;
const int kSpeedMin = 700, kSpeedSpan = 0x8fd;
const unsigned kFlagVisible = 2;
typedef bool(__fastcall *HasFlags_t)(void *window, void *edx, unsigned flags);
typedef void(__fastcall *ModifyFlags_t)(void *window, void *edx, unsigned add, unsigned remove, int redraw);

// The game changes its size through ApplyGameOptions, as the options dialog does, with the options it has and another size. That
// call also sends the options' speed to a running match, so it gets the one the match runs at; and the change of size shows the
// pause notice at its end, which goes back to hidden if it was.
// In a match with other humans the change of size stays the game's own, from its options: ApplyGameOptions would send them a speed
// command.
bool OthersInMatch()
{
	typedef int(__cdecl *IsMultiHumans_t)();
	return *reinterpret_cast<void **>(addr::kGame) && reinterpret_cast<IsMultiHumans_t>(addr::kMultiHumans)() != 0;
}

bool ApplySize(ResSize size)
{
	const int index = ResolutionIndexOf(size.w, size.h);
	if (index < 0) {
		LogW("display", "the size %dx%d is not in the list of this mode: the game keeps its own", size.w, size.h);
		return false;
	}
	int block[kOptionsCount];
	memcpy(block, reinterpret_cast<const int *>(addr::kGameOptions), sizeof(block));
	if (block[kResolutionSlot] == index && g_sizeAtStart.x == size.w && g_sizeAtStart.y == size.h) return true;
	int &current = *reinterpret_cast<int *>(addr::kCurrentResolutionIndex);
	const int before = ResolutionIndexOf(g_sizeAtStart.x, g_sizeAtStart.y);
	// The list was built again for another mode: the game's position may already name the new size while it is at another one.
	if (block[kResolutionSlot] == index) current = -1;
	block[kResolutionSlot] = index;
	const char *game = *reinterpret_cast<const char *const *>(addr::kGame);
	if (game) {
		const int speed = (std::max)(kSpeedMin, (std::min)(kSpeedMin + kSpeedSpan, *reinterpret_cast<const int *>(game + kGameSpeed)));
		block[kSpeedSlot] = ((speed - kSpeedMin) * 100 + kSpeedSpan / 2) / kSpeedSpan;
	}
	void *notice = *reinterpret_cast<void **>(addr::kPausedMenu);
	const bool noticeShown = notice && reinterpret_cast<HasFlags_t>(addr::kWindowHasFlags)(notice, nullptr, kFlagVisible);
	reinterpret_cast<int(__cdecl *)(int *)>(addr::kApplyGameOptions)(block);
	notice = *reinterpret_cast<void **>(addr::kPausedMenu);
	if (notice && !noticeShown) reinterpret_cast<ModifyFlags_t>(addr::kModifyFlags)(notice, nullptr, 0, kFlagVisible, 1);
	// A failed change leaves the game at the size before, and a position the options would not find.
	if (current != index) {
		LogW("display", "the game did not take %dx%d (its position is %d, not %d): it stays at %ldx%ld", size.w, size.h, current, index, g_sizeAtStart.x,
			g_sizeAtStart.y);
		if (current < 0) current = before;
		return false;
	}
	g_sizeAtStart = {size.w, size.h};
	return true;
}

// A full screen's size chosen in the options is tried first: it is saved when the player keeps it, and without an answer in kTrialMs
// (a black screen, a monitor out of range) the size before comes back. On the game thread, but for the answer.
const DWORD kTrialMs = 15000;
struct Trial {
	bool active = false;
	ResSize tried, back;
};
Trial g_trial;
enum Answer { kNoAnswer, kKeep, kBack };
std::atomic<int> g_answer{kNoAnswer};
bool g_modeSwitched = false;
bool g_knownExe = false;

void AnswerKeep()
{
	g_answer = kKeep;
	UiWakeGameThread();
}

void AnswerBack()
{
	g_answer = kBack;
	UiWakeGameThread();
}

void StartTrial(ResSize tried, ResSize back)
{
	g_trial.active = true;
	g_trial.tried = tried;
	g_trial.back = back;
	g_answer = kNoAnswer;
	const std::string size = std::to_string(tried.w) + " x " + std::to_string(tried.h);
	LogI("display", "full screen at %dx%d on trial; it goes back to %dx%d without an answer", tried.w, tried.h, back.w, back.h);
	UiAskTimed(TrFmt("Keep %s?", {size}), "If the screen looks right, keep it. If not, the previous resolution comes back in %s seconds.", "Keep", &AnswerKeep,
		"Go back", &AnswerBack, GetTickCount() + kTrialMs);
}

void __fastcall HookSaveOptions(unsigned *block)
{
	const int index = static_cast<int>(block[kResolutionSlot]);
	if (index >= 0 && index < ResolutionCount()) {
		const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
		if (size.x != g_sizeAtStart.x || size.y != g_sizeAtStart.y) {
			const bool windowed = DisplayIsWindowed();
			const ResPick back = SavedPick(windowed);
			const std::vector<ResSize> list = ResolutionSizes();
			// The size this mode had (after a change of mode in the options) needs no question; a window is seen as it changes.
			if (!windowed && back.index >= 0 && back.index != index && !OthersInMatch()) StartTrial({size.x, size.y}, list[back.index]);
			else {
				SaveSize(windowed, {size.x, size.y});
				if (windowed) SaveWindowClient(size.x, size.y);
			}
			g_sizeAtStart = size;
		}
	}
	g_origSaveOptions(block);
}

// The game's list is a fixed table of 2017 sizes plus the 16-bit modes up to 2048x1536 that Windows
// reports, so wide and large screens never found theirs: the list is built from the monitor's own modes.
void ReplaceResolutionList()
{
	const ResolutionChoice choice = g_previewing ? g_preview : ResolutionChoice{-1, DisplayIsWindowed(), GetConfig().extendedView};
	std::vector<ResSize> windows, extended;
	for (POINT p : DisplayWindowsSizes(choice.monitor)) windows.push_back({p.x, p.y});
	if (choice.extendedView && DisplayFakesModeChange()) {
		for (POINT p : DisplayExtendedSizes(choice.monitor)) extended.push_back({p.x, p.y});
	}
	ResSize screen;
	DisplayMonitorSize(choice.monitor, screen.w, screen.h);
	std::vector<ResSize> sizes;
	g_customWindowSlot = -1;
	if (DisplayFakesModeChange() && choice.windowed) {
		ResSize area;
		DisplayWindowArea(choice.monitor, area.w, area.h);
		sizes = BuildWindowResolutionList(windows, screen, area);
		const Config &c = GetConfig();
		const ResSize saved = WindowFrameForClient({(std::min)(area.w, c.windowClientW), (std::min)(area.h, c.windowClientH)});
		if (saved.w > 0 && std::none_of(sizes.begin(), sizes.end(), [&](ResSize s) { return s.w == saved.w && s.h == saved.h; })) {
			g_customWindowSlot = static_cast<int>(sizes.size());
			sizes.push_back(saved);
		}
	}
	else {
		sizes = BuildResolutionList(windows, screen, extended, DisplayFakesModeChange());
	}
	auto &begin = *reinterpret_cast<POINT **>(addr::kResolutionList + 4);
	auto &end = *reinterpret_cast<POINT **>(addr::kResolutionList + 8);
	if (begin) end = begin;
	for (ResSize size : sizes) {
		const POINT p = {size.w, size.h};
		reinterpret_cast<PushBack_t>(addr::kVectorPointPushBack)(reinterpret_cast<void *>(addr::kResolutionList), nullptr, &p);
	}
	static size_t logged = 0;
	if (!sizes.empty() && logged != sizes.size()) {
		logged = sizes.size();
		LogI("display", "resolutions: %u sizes from the %u modes of the monitor, %dx%d down to %dx%d", static_cast<unsigned>(sizes.size()),
			static_cast<unsigned>(windows.size()), sizes.front().w, sizes.front().h, sizes.back().w, sizes.back().h);
	}
}

bool __cdecl HookInitResolutionList()
{
	static bool started = false;
	int keepW = 0, keepH = 0;
	const bool keep = started && TweaksIntendedResolution(keepW, keepH);
	g_origInitResolutionList();
	ReplaceResolutionList();
	// The list is built again each time the options open, for the screen of that moment: the position
	// chosen before would point to another size if the screen or the list changed.
	if (!started && ResolutionCount() > 0) {
		started = true;
		UseSavedResolution();
	}
	else if (keep && g_previewing) {
		*reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = ResolutionIndexOf(keepW, keepH);
	}
	else if (keep) {
		const int at = KeepResolution(ResolutionSizes(), {keepW, keepH}, ScreenSize());
		// Not in the list (a window too small for it): no position, so whatever is chosen changes the mode.
		*reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = at;
		if (at < 0) LogI("display", "resolution %dx%d is not in this list and the screen's own size is not either", keepW, keepH);
	}
	return ResolutionCount() > 0;
}

const unsigned kSmoothAlign = 4;
const int kSmoothRefreshMs = 5;

bool g_scrollSaved = false;
int g_scrollMin0 = 0, g_scrollMax0 = 0;
unsigned g_scrollStep0 = 0;
unsigned g_scrollAlign0 = 0;
int g_scrollRefresh0 = 0;

void ApplySmoothScroll()
{
	if (!g_scrollSaved) {
		return;
	}
	const bool smooth = GetConfig().smoothScroll;
	auto &align = *reinterpret_cast<unsigned *>(addr::kScrollStepAlign);
	const unsigned wanted = smooth && g_scrollAlign0 > kSmoothAlign ? kSmoothAlign : g_scrollAlign0;
	auto &refresh = *reinterpret_cast<int *>(addr::kScrollRefresh);
	const int wantedRefresh = smooth && g_scrollRefresh0 > kSmoothRefreshMs ? kSmoothRefreshMs : g_scrollRefresh0;
	if (align != wanted || refresh != wantedRefresh) {
		align = wanted;
		refresh = wantedRefresh;
		LogD("game", "scroll: steps of %u px and refresh every %d ms (%s)", wanted, wantedRefresh, smooth ? "smooth" : "original");
	}
}

void __cdecl HookLoadScrollSettings()
{
	g_origLoadScroll();
	auto &mn = *reinterpret_cast<int *>(addr::kScrollSpeedMin);
	auto &mx = *reinterpret_cast<int *>(addr::kScrollSpeedMax);
	auto &step = *reinterpret_cast<unsigned *>(addr::kScrollStepMax);
	if (!g_scrollSaved) {
		g_scrollMin0 = mn;
		g_scrollMax0 = mx;
		g_scrollStep0 = step;
		g_scrollAlign0 = *reinterpret_cast<unsigned *>(addr::kScrollStepAlign);
		g_scrollRefresh0 = *reinterpret_cast<int *>(addr::kScrollRefresh);
		g_scrollSaved = true;
	}
	ApplySmoothScroll();
	unsigned align = *reinterpret_cast<unsigned *>(addr::kScrollStepAlign);
	LogD("game", "scroll: game values: minimum step %u px (original %u), refresh every %d ms (original %d), initial "
		"wait %d ms, edge %d px",
		align, g_scrollAlign0, *reinterpret_cast<int *>(addr::kScrollRefresh), g_scrollRefresh0, *reinterpret_cast<int *>(addr::kScrollInitialWait),
		*reinterpret_cast<int *>(addr::kScrollEdge));
	const float f = GetConfig().scrollSpeed;
	mn = static_cast<int>(g_scrollMin0 * f);
	mx = static_cast<int>(g_scrollMax0 * f);
	unsigned s = static_cast<unsigned>(g_scrollStep0 * f);
	if (align) {
		s = (s + align / 2) / align * align;
		if (s < align) {
			s = align;
		}
	}
	step = s;
	LogD("game", "scroll: x%.2f -> speed %d..%d px/s, maximum step %u px (original %d..%d, %u)", f, mn, mx, step,
		g_scrollMin0, g_scrollMax0, g_scrollStep0);
}

typedef void *(__cdecl *LoadConfigIni_t)(void *);
LoadConfigIni_t g_origLoadConfigIni = nullptr;

void RaiseTimerResolution()
{
	wchar_t path[MAX_PATH];
	GetSystemDirectoryW(path, MAX_PATH);
	lstrcatW(path, L"\\winmm.dll");
	typedef UINT(WINAPI * TimeBeginPeriod_t)(UINT);
	HMODULE real = GetModuleHandleW(path);
	auto begin = real ? reinterpret_cast<TimeBeginPeriod_t>(GetProcAddress(real, "timeBeginPeriod")) : nullptr;
	if (begin && begin(1) == 0) {
		LogI("boot", "tweaks: Windows timers at 1 ms");
	}
}

std::vector<void (*)()> &ConfigLoadedCallbacks()
{
	static std::vector<void (*)()> callbacks;
	return callbacks;
}

void *__cdecl HookLoadConfigIni(void *param)
{
	static bool timers = false;
	if (!timers) {
		timers = true;
		RaiseTimerResolution();
	}
	void *r = g_origLoadConfigIni(param);
	for (void (*callback)() : ConfigLoadedCallbacks()) callback();
	if (*reinterpret_cast<int *>(addr::kShowSplash)) {
		*reinterpret_cast<int *>(addr::kShowSplash) = 0;
		LogI("boot", "tweaks: presentation disabled");
	}
	return r;
}

template <class Fn> bool HookAt(uintptr_t addr, Fn *detour, Fn **orig, const char *what)
{
	if (HookInstall(addr, detour, orig)) return true;
	LogE("boot", "tweaks: could not hook %s (%08x)", what, static_cast<unsigned>(addr));
	return false;
}

}

namespace {
typedef int(__cdecl *ScriptFn_t)(int *sp);
ScriptFn_t g_origGateInside = nullptr;

bool SquadExists(unsigned player, unsigned index)
{
	char *game = *reinterpret_cast<char **>(addr::kGame);
	if (!game || player >= 16) return false;
	char *army = *reinterpret_cast<char **>(game + 0x12cc + player * 800 + 0x8c);
	if (!army) return false;
	char **begin = *reinterpret_cast<char ***>(army + 0xc), **end = *reinterpret_cast<char ***>(army + 0x10);
	return begin && end >= begin && index < static_cast<unsigned>(end - begin) && begin[index];
}

int __cdecl HookGateInside(int *sp)
{
	char *top = reinterpret_cast<char *>(*sp);
	const unsigned short id = *reinterpret_cast<unsigned short *>(top - 4);
	if (!SquadExists(id & 0xf, id >> 4)) {
		static int logged = 0;
		if (logged++ < 5) LogW("script", "Gate::Inside with squad %u of player %u, which does not exist: answering no", id >> 4, id & 0xf);
		*sp = reinterpret_cast<int>(top - 0xc);
		*reinterpret_cast<char *>(*sp) = 0;
		*sp += 1;
		return 0;
	}
	return g_origGateInside(sp);
}

bool g_quietPause = false;
std::atomic<bool> g_pauseHeld{false};

typedef void(__fastcall *PauseRequest_t)(void *game, void *edx, int param);
PauseRequest_t g_origRequestPause = nullptr, g_origRequestUnpause = nullptr, g_origRequestToggle = nullptr;

bool OnlyHumanOnline(char *game)
{
	typedef int(__cdecl *IsMultiHumans_t)();
	return game && *reinterpret_cast<uintptr_t *>(game) == addr::kMultiplayerVtable &&
		*reinterpret_cast<int *>(game + 0x22c) == 0 &&
		!reinterpret_cast<IsMultiHumans_t>(addr::kMultiHumans)();
}

// CVXBasicGame::SetPaused is slot 16 of every kind of match and IsPaused slot 22; quiet leaves out the pause notice.
void CallSetPaused(char *game, int paused, bool quiet)
{
	typedef void(__fastcall *SetPaused_t)(void *game, void *edx, int paused);
	g_quietPause = quiet;
	reinterpret_cast<SetPaused_t>((*reinterpret_cast<void ***>(game))[16])(game, nullptr, paused);
	g_quietPause = false;
}

bool GamePaused(char *game)
{
	typedef int(__fastcall *IsPaused_t)(void *game);
	return reinterpret_cast<IsPaused_t>((*reinterpret_cast<void ***>(game))[22])(game) != 0;
}

void SetPausedNow(char *game, int paused, bool quiet = true)
{
	CallSetPaused(game, paused, quiet);
	static DWORD logged = 0;
	if (GetTickCount() - logged > 60000) {
		logged = GetTickCount();
		LogD("game", "pause: you are the only player of the online game; the menu pause applies immediately");
	}
}

void LogPauseRequest(const char *what, char *game, bool only, void *ret)
{
	static int logged = 0;
	if (logged >= 40) return;
	logged++;
	typedef int(__cdecl * IsMultiHumans_t)();
	LogD("game", "diagnostics: %s requested from 0x%08x (paused now: %d, only online human: %d, several humans: %d, +0x22c: %d) -> %s", what,
		static_cast<unsigned>(reinterpret_cast<uintptr_t>(ret)), game ? *reinterpret_cast<int *>(game + 0x1240) : -1, only ? 1 : 0,
		reinterpret_cast<IsMultiHumans_t>(addr::kMultiHumans)(), game ? *reinterpret_cast<int *>(game + 0x22c) : -1,
		only ? "the DLL applies it immediately" : "follows the game (sent over the network)");
}

std::atomic<bool (*)()> g_pauseGate{nullptr};

// A request of this computer that would pause a game with other players, which the league keeps here.
bool PauseRefused(char *game)
{
	bool (*gate)() = g_pauseGate;
	return gate && !GamePaused(game) && !gate();
}

void __fastcall HookRequestPause(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Pause", static_cast<char *>(game), only, _ReturnAddress());
	if (only) return SetPausedNow(static_cast<char *>(game), 1);
	if (PauseRefused(static_cast<char *>(game))) return;
	g_origRequestPause(game, edx, param);
}

void __fastcall HookRequestUnpause(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Resume", static_cast<char *>(game), only, _ReturnAddress());
	if (only && g_pauseHeld) return;
	if (only) return SetPausedNow(static_cast<char *>(game), 0);
	g_origRequestUnpause(game, edx, param);
}

void __fastcall HookRequestToggle(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Alternar", static_cast<char *>(game), only, _ReturnAddress());
	if (only) {
		const bool paused = GamePaused(static_cast<char *>(game));
		if (paused && g_pauseHeld) return;
		return SetPausedNow(static_cast<char *>(game), paused ? 0 : 1, false);
	}
	if (PauseRefused(static_cast<char *>(game))) return;
	g_origRequestToggle(game, edx, param);
}

FocusPause g_focusPause;

enum FocusStep { kFocusNothing, kFocusPaused, kFocusResumed, kFocusFailed };

FocusStep FocusChanged(bool active)
{
	__try {
		char *game = *reinterpret_cast<char **>(addr::kGame);
		const bool usable = game && !*reinterpret_cast<int *>(addr::kEditorMode);
		if (!active) {
			if (!g_focusPause.Leave(GetConfig().pauseInBackground, usable ? game : nullptr, usable && GameIsMultiplayer(), usable && GamePaused(game))) return kFocusNothing;
			CallSetPaused(game, 1, true);
			return kFocusPaused;
		}
		if (!g_focusPause.Return(game, game && GamePaused(game))) return kFocusNothing;
		if (g_pauseHeld) return kFocusNothing;
		CallSetPaused(game, 0, true);
		return kFocusResumed;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		g_focusPause = FocusPause();
		return kFocusFailed;
	}
}

typedef void(__cdecl *ShowPausedMenu_t)();
ShowPausedMenu_t g_origShowPausedMenu = nullptr;
typedef int(__fastcall *BringChildToTop_t)(void *parent, void *edx, void *child, int flag);
typedef int(__fastcall *IsKindOf_t)(void *obj, void *edx, const char *cls);

bool IsConfirm(char *w)
{
	__try {
		char *obj = w + 4 + *reinterpret_cast<int *>(*reinterpret_cast<char **>(w + 4) + 4);
		const char *cls = *reinterpret_cast<const char *const *>(addr::kClassNameCVXUIConfirm);
		return reinterpret_cast<IsKindOf_t>((*reinterpret_cast<void ***>(obj))[1])(obj, nullptr, cls) != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

void __cdecl HookShowPausedMenu()
{
	char *const *slot = reinterpret_cast<char *const *>(addr::kPausedMenu);
	const bool created = *slot == nullptr;
	if (created && g_quietPause) return;
	g_origShowPausedMenu();
	char *menu = *slot;
	if (!created || !menu) return;
	char *parent = *reinterpret_cast<char **>(menu + 0xc);
	if (!parent) return;
	char *found = nullptr;
	for (char *w = *reinterpret_cast<char **>(parent + 0x20); w && !found; w = *reinterpret_cast<char **>(w + 0x18)) {
		if (w != menu && IsConfirm(w)) found = w;
	}
	if (!found) return;
	reinterpret_cast<BringChildToTop_t>(addr::kBringChildToTop)(parent, nullptr, found, 1);
	LogD("game", "pause: the confirmation question stays above the pause notice");
}
}

namespace {
void *g_townhallCont = nullptr;

void __cdecl LogMissingSettlementClass(const char *name)
{
	static int logged = 0;
	if (logged++ < 10) LogW("game", "map: settlement with the class \"%s\", which does not exist (map made with another mod?): skipped", name ? name : "?");
}

static_assert(addr::kRegistry == 0x009bdb00 && addr::kFindClassByName == 0x00409e40 && addr::kMapSettlementSkip == 0x0054ee6d,
	"HookTownhallSettlement hard-codes these addresses in its assembly");

__declspec(naked) void HookTownhallSettlement()
{
	__asm {
		cmp dword ptr [esp+38h], ebx
		mov eax, dword ptr [esp+24h]
		jae have_name
		lea eax, [esp+24h]
	have_name:
		mov ecx, dword ptr ds:[0x009BDB00]
		push eax
		mov edx, 0x00409E40
		call edx
		test eax, eax
		jz missing
		mov eax, dword ptr [esp+38h]
		jmp dword ptr [g_townhallCont]
	missing:
		cmp dword ptr [esp+38h], ebx
		mov eax, dword ptr [esp+24h]
		jae log_name
		lea eax, [esp+24h]
	log_name:
		push eax
		call LogMissingSettlementClass
		add esp, 4
		mov eax, 0x0054EE6D
		jmp eax
	}
}
}

void TweaksOnConfigLoaded(void (*callback)())
{
	ConfigLoadedCallbacks().push_back(callback);
}

void TweaksWindowActive(bool active)
{
	switch (FocusChanged(active)) {
	case kFocusPaused: LogI("game", "pause: the match waits while the window is in the background"); break;
	case kFocusResumed: LogI("game", "pause: the window is back, the match goes on"); break;
	case kFocusFailed: LogE("game", "pause: ERROR pausing or resuming the match with the window"); break;
	default: break;
	}
}

bool TweaksOnlyHumanOnline(void *game)
{
	return OnlyHumanOnline(static_cast<char *>(game));
}

void TweaksSetPausedQuietly(void *game, bool paused)
{
	if (game) CallSetPaused(static_cast<char *>(game), paused ? 1 : 0, true);
}

bool TweaksGamePaused(void *game)
{
	return game && GamePaused(static_cast<char *>(game));
}

void TweaksHoldPause(bool held)
{
	g_pauseHeld = held;
}

void TweaksSetPauseGate(bool (*mayPause)())
{
	g_pauseGate = mayPause;
}

void TweaksRequestUnpause(void *game)
{
	// Slot 0x50 is RequestUnpause (CVXGameMultiplayer 0x550400, hooked above), what the game's menu calls when it closes.
	if (game) reinterpret_cast<PauseRequest_t>((*reinterpret_cast<void ***>(game))[0x50 / 4])(game, nullptr, 0);
}

namespace {
ScriptFn_t g_origObjPlayer = nullptr;

int __cdecl HookObjPlayer(int *sp)
{
	char *top = reinterpret_cast<char *>(*sp);
	const unsigned handle = *reinterpret_cast<unsigned *>(top - 8);
	const int offset = *reinterpret_cast<int *>(top - 4);
	const char *base = reinterpret_cast<char **>(addr::kHandleTable)[handle & 0xffff];
	if (base && base + offset) {
		const char *obj = base + offset;
		if (!*reinterpret_cast<char *const *>(obj + 0x70)) {
			static int logged = 0;
			if (logged++ < 5) LogW("script", "Obj::player with an object without a player: answering -1");
			*sp = reinterpret_cast<int>(top - 8);
			*reinterpret_cast<int *>(*sp) = -1;
			*sp += 4;
			return 0;
		}
	}
	return g_origObjPlayer(sp);
}
}

bool TweaksKnownExe()
{
	return g_knownExe;
}

int TweaksCrashFixSites()
{
	return g_crashFixSites;
}

void InstallTweaks(bool knownExe)
{
	g_knownExe = knownExe;
	if (knownExe) {
		HookAt(addr::kScriptFnGateInside, &HookGateInside, &g_origGateInside, "Gate::Inside");
		HookAt(addr::kMapSettlementSite, reinterpret_cast<void *>(&HookTownhallSettlement), &g_townhallCont, "map settlements");
		HookAt(addr::kScriptFnObjPlayer, &HookObjPlayer, &g_origObjPlayer, "Obj::player");
	}
	if (!knownExe) {
		LogW("boot", "tweaks: unknown executable (patched?) -> address-dependent tweaks are disabled");
		return;
	}
	if (GetConfig().crashFix) InstallCrashFix();
	HookAt(addr::kPeerLeaveRoomA, &HookPeerLeaveRoom, &g_origPeerLeaveRoom, "peerLeaveRoomA");
	RepaintClosedMenusWhole();
	HookAt(addr::kShowPausedMenu, &HookShowPausedMenu, &g_origShowPausedMenu, "ShowPausedMenu");
	HookAt(addr::kRequestPause, &HookRequestPause, &g_origRequestPause, "CVXGameMultiplayer::RequestPause");
	HookAt(addr::kRequestUnpause, &HookRequestUnpause, &g_origRequestUnpause, "CVXGameMultiplayer::RequestUnpause");
	HookAt(addr::kRequestPauseOnline, &HookRequestToggle, &g_origRequestToggle, "CVXGameMultiplayer::RequestPauseOnline");
	HookAt(addr::kChangeDisplayModeTo, &HookChangeDisplayModeTo, &g_origChangeDisplayModeTo, "ChangeDisplayModeTo");
	if (HookAt(addr::kInitResolutionList, &HookInitResolutionList, &g_origInitResolutionList, "InitResolutionList")) {
		DisplaySetResizeHandler(&ResizeGameClient);
	}
	HookAt(addr::kSaveGameOptionsToIni, &HookSaveOptions, &g_origSaveOptions, "SaveGameOptionsToIni");
	HookAt(addr::kLoadScrollSettings, &HookLoadScrollSettings, &g_origLoadScroll, "LoadScrollSettings");
	HookAt(addr::kLoadConfigIni, &HookLoadConfigIni, &g_origLoadConfigIni, "LoadConfigIni");
	LogI("boot", "tweaks: installed (ScrollSpeed=%.2f)", GetConfig().scrollSpeed);
}

bool TweaksIntendedResolution(int &w, int &h)
{
	const int index = *reinterpret_cast<int *>(addr::kCurrentResolutionIndex);
	if (index < 0 || index >= ResolutionCount()) {
		if (!g_previewing || g_heldSize.w <= 0) return false;
		w = g_heldSize.w;
		h = g_heldSize.h;
		return true;
	}
	const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
	w = size.x;
	h = size.y;
	return true;
}

void TweaksPreviewResolutions(const ResolutionChoice *choice)
{
	if (!choice && !g_previewing) return;
	int w = 0, h = 0;
	if (TweaksIntendedResolution(w, h)) g_heldSize = {w, h};
	g_previewing = choice != nullptr;
	if (choice) g_preview = *choice;
	ReplaceResolutionList();
	// The game changes its mode when the size chosen is not the one at this position: -1 when the list lacks it.
	*reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = g_heldSize.w > 0 ? ResolutionIndexOf(g_heldSize.w, g_heldSize.h) : -1;
}

std::vector<ResSize> TweaksResolutionList()
{
	return ResolutionSizes();
}

void TweaksApplySmoothScroll()
{
	ApplySmoothScroll();
}

void TweaksModeSwitched()
{
	g_modeSwitched = true;
	UiWakeGameThread();
}

void TweaksResolutionTick()
{
	if (!g_knownExe) return;
	if (g_modeSwitched && !*reinterpret_cast<void **>(addr::kOptionsDialog)) {
		g_modeSwitched = false;
		// The trial of a full screen's size ends with the mode it was tried in.
		if (g_trial.active) {
			g_trial.active = false;
			UiCancelTimed();
		}
		const bool windowed = DisplayIsWindowed();
		ReplaceResolutionList();
		const ResPick pick = SavedPick(windowed);
		if (OthersInMatch()) {
			LogI("display", "%s in a match with other players: the size stays as it is", windowed ? "window" : "full screen");
		}
		else if (pick.index >= 0) {
			const ResSize size = ResolutionSizes()[pick.index];
			LogI("display", "%s: the size of this mode is %dx%d", windowed ? "window" : "full screen", size.w, size.h);
			ApplySize(size);
		}
	}
	const int answer = g_answer.exchange(kNoAnswer);
	if (!g_trial.active || answer == kNoAnswer) return;
	g_trial.active = false;
	if (answer == kKeep) {
		SaveSize(false, g_trial.tried);
		return;
	}
	LogI("display", "full screen at %dx%d not kept: back to %dx%d", g_trial.tried.w, g_trial.tried.h, g_trial.back.w, g_trial.back.h);
	ApplySize(g_trial.back);
}

void TweaksFrameTick()
{
	if (!g_knownExe) return;
	// The size changes between frames, where the game reads its window's messages, as it does from its options: never inside a frame.
	if ((g_modeSwitched && !*reinterpret_cast<void **>(addr::kOptionsDialog)) || g_answer != kNoAnswer) UiWakeGameThread();
	static LARGE_INTEGER freq = {}, last = {}, start = {};
	static unsigned frames = 0;
	static double maxGapMs = 0;
	static bool scrolling = false;
	if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const bool moving = *reinterpret_cast<unsigned *>(addr::kScrollBits) != 0;
	if (moving) {
		if (!scrolling) {
			scrolling = true;
			start = now;
			frames = 0;
			maxGapMs = 0;
		}
		else {
			const double gap = (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart;
			if (gap > maxGapMs) maxGapMs = gap;
		}
		frames++;
	}
	else if (scrolling) {
		scrolling = false;
		const double secs = (now.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
		if (secs > 0.5) {
			LogD("game", "scroll: %.1f s moving the camera: %u game frames (%.0f per second), maximum gap %.0f ms, minimum "
				"step %u px", secs, frames, frames / secs, maxGapMs, *reinterpret_cast<unsigned *>(addr::kScrollStepAlign));
		}
	}
	last = now;
}
