// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "hooks/displaymode.h"
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
		DWORD old = 0;
		if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old)) continue;
		at[0] = 0xe8;
		*reinterpret_cast<int32_t *>(at + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (site + 5));
		VirtualProtect(at, 5, old, &old);
		FlushInstructionCache(GetCurrentProcess(), at, 5);
		done++;
	}
	LogI("boot", "crash protection: %d of %d sites protected", done, static_cast<int>(sizeof(addr::kCrashFixSites) / sizeof(addr::kCrashFixSites[0])));
}

void RepaintClosedMenusWhole()
{
	const uint8_t original[] = {0x6a, 0x01, 0x6a, 0x00, 0xff, 0x50, 0x30};
	uint8_t *at = reinterpret_cast<uint8_t *>(addr::kMenuSwitchInvalidate);
	if (memcmp(at, original, sizeof(original)) != 0) {
		LogW("display", "menus: the menu switch code is not the expected one, a closed menu may leave its image behind");
		return;
	}
	DWORD old = 0;
	if (!VirtualProtect(at, 2, PAGE_EXECUTE_READWRITE, &old)) return;
	at[1] = 0x00;
	VirtualProtect(at, 2, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, 2);
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
	if (!testOnly && DisplayFakeMode(static_cast<int>(w), static_cast<int>(h), 16, false)) {
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

void SetResolutionIndex(int index)
{
	if (index < 0) return;
	*reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = index;
	const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
	g_sizeAtStart = size;
}

void UseSavedResolution()
{
	const Config &c = GetConfig();
	const ResSize screen = ScreenSize();
	const ResPick pick = PickStartResolution(ResolutionSizes(), {c.resolutionW, c.resolutionH}, {c.resolutionScreenW, c.resolutionScreenH}, screen,
		DisplayMonitorHasMode(c.resolutionW, c.resolutionH));
	SetResolutionIndex(pick.index);
	static const char *const kWhy[] = {"the saved size", "the screen's own size", "the first of the list", "none"};
	LogI("display", "resolution at start: saved %dx%d (chosen on a %dx%d screen), the screen is %dx%d -> %ldx%ld, %s", c.resolutionW, c.resolutionH,
		c.resolutionScreenW, c.resolutionScreenH, screen.w, screen.h, g_sizeAtStart.x, g_sizeAtStart.y, kWhy[static_cast<int>(pick.why)]);
}

void __fastcall HookSaveOptions(unsigned *block)
{
	const int index = static_cast<int>(block[kResolutionSlot]);
	if (index >= 0 && index < ResolutionCount()) {
		const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
		if (size.x != g_sizeAtStart.x || size.y != g_sizeAtStart.y) {
			const ResSize screen = ScreenSize();
			Config &c = MutableConfig();
			c.resolutionW = size.x;
			c.resolutionH = size.y;
			c.resolutionScreenW = screen.w;
			c.resolutionScreenH = screen.h;
			SaveConfig();
			LogI("display", "resolution saved by size: %ldx%ld, chosen on a %dx%d screen", size.x, size.y, screen.w, screen.h);
			g_sizeAtStart = size;
		}
	}
	g_origSaveOptions(block);
}

// The game's list is a fixed table of 2017 sizes plus the 16-bit modes up to 2048x1536 that Windows
// reports, so wide and large screens never found theirs: the list is built from the monitor's own modes.
void ReplaceResolutionList()
{
	std::vector<ResSize> windows, extended;
	for (POINT p : DisplayWindowsSizes()) windows.push_back({p.x, p.y});
	if (GetConfig().extendedView && DisplayFakesModeChange()) {
		for (POINT p : DisplayExtendedSizes()) extended.push_back({p.x, p.y});
	}
	const std::vector<ResSize> sizes = BuildResolutionList(windows, ScreenSize(), extended, DisplayFakesModeChange() && !DisplayIsWindowed());
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
	else if (keep) {
		const int at = KeepResolution(ResolutionSizes(), {keepW, keepH}, ScreenSize());
		if (at >= 0) *reinterpret_cast<int *>(addr::kCurrentResolutionIndex) = at;
		else LogW("display", "resolution %dx%d is no longer in the list and the screen's own size is not either", keepW, keepH);
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

bool HookAt(uintptr_t addr, void *detour, void **orig, const char *what)
{
	if (HookCreate(reinterpret_cast<void *>(addr), detour, orig) == MH_OK &&
		HookEnable(reinterpret_cast<void *>(addr)) == MH_OK) {
		return true;
	}
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

typedef void(__fastcall *PauseRequest_t)(void *game, void *edx, int param);
PauseRequest_t g_origRequestPause = nullptr, g_origRequestUnpause = nullptr, g_origRequestToggle = nullptr;

bool OnlyHumanOnline(char *game)
{
	typedef int(__cdecl *IsMultiHumans_t)();
	return game && *reinterpret_cast<uintptr_t *>(game) == addr::kMultiplayerVtable &&
		*reinterpret_cast<int *>(game + 0x22c) == 0 &&
		!reinterpret_cast<IsMultiHumans_t>(addr::kMultiHumans)();
}

void SetPausedNow(char *game, int paused, bool quiet = true)
{
	typedef void(__fastcall *SetPaused_t)(void *game, void *edx, int paused);
	g_quietPause = quiet;
	reinterpret_cast<SetPaused_t>((*reinterpret_cast<void ***>(game))[16])(game, nullptr, paused);
	g_quietPause = false;
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

void __fastcall HookRequestPause(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Pause", static_cast<char *>(game), only, _ReturnAddress());
	if (only) return SetPausedNow(static_cast<char *>(game), 1);
	g_origRequestPause(game, edx, param);
}

void __fastcall HookRequestUnpause(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Resume", static_cast<char *>(game), only, _ReturnAddress());
	if (only) return SetPausedNow(static_cast<char *>(game), 0);
	g_origRequestUnpause(game, edx, param);
}

void __fastcall HookRequestToggle(void *game, void *edx, int param)
{
	const bool only = OnlyHumanOnline(static_cast<char *>(game));
	LogPauseRequest("Alternar", static_cast<char *>(game), only, _ReturnAddress());
	if (only) {
		typedef int(__fastcall * IsPaused_t)(void *game);
		const int paused = reinterpret_cast<IsPaused_t>((*reinterpret_cast<void ***>(game))[22])(game);
		return SetPausedNow(static_cast<char *>(game), paused ? 0 : 1, false);
	}
	g_origRequestToggle(game, edx, param);
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

bool TweaksOnlyHumanOnline(void *game)
{
	return OnlyHumanOnline(static_cast<char *>(game));
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

void InstallTweaks(bool knownExe)
{
	HookAt(addr::kScriptFnGateInside, reinterpret_cast<void *>(&HookGateInside), reinterpret_cast<void **>(&g_origGateInside), "Gate::Inside");
	if (knownExe) {
		HookAt(addr::kMapSettlementSite, reinterpret_cast<void *>(&HookTownhallSettlement), &g_townhallCont, "map settlements");
		HookAt(addr::kScriptFnObjPlayer, reinterpret_cast<void *>(&HookObjPlayer), reinterpret_cast<void **>(&g_origObjPlayer), "Obj::player");
	}
	if (!knownExe) {
		LogW("boot", "tweaks: unknown executable (patched?) -> address-dependent tweaks are disabled");
		return;
	}
	if (GetConfig().crashFix) InstallCrashFix();
	RepaintClosedMenusWhole();
	HookAt(addr::kShowPausedMenu, reinterpret_cast<void *>(&HookShowPausedMenu), reinterpret_cast<void **>(&g_origShowPausedMenu), "ShowPausedMenu");
	HookAt(addr::kRequestPause, reinterpret_cast<void *>(&HookRequestPause), reinterpret_cast<void **>(&g_origRequestPause), "CVXGameMultiplayer::RequestPause");
	HookAt(addr::kRequestUnpause, reinterpret_cast<void *>(&HookRequestUnpause), reinterpret_cast<void **>(&g_origRequestUnpause),
		"CVXGameMultiplayer::RequestUnpause");
	HookAt(addr::kRequestPauseOnline, reinterpret_cast<void *>(&HookRequestToggle), reinterpret_cast<void **>(&g_origRequestToggle),
		"CVXGameMultiplayer::RequestPauseOnline");
	HookAt(addr::kChangeDisplayModeTo, reinterpret_cast<void *>(&HookChangeDisplayModeTo), reinterpret_cast<void **>(&g_origChangeDisplayModeTo),
		"ChangeDisplayModeTo");
	HookAt(addr::kInitResolutionList, reinterpret_cast<void *>(&HookInitResolutionList),
		reinterpret_cast<void **>(&g_origInitResolutionList), "InitResolutionList");
	HookAt(addr::kSaveGameOptionsToIni, reinterpret_cast<void *>(&HookSaveOptions), reinterpret_cast<void **>(&g_origSaveOptions),
		"SaveGameOptionsToIni");
	HookAt(addr::kLoadScrollSettings, reinterpret_cast<void *>(&HookLoadScrollSettings),
		reinterpret_cast<void **>(&g_origLoadScroll), "LoadScrollSettings");
	HookAt(addr::kLoadConfigIni, reinterpret_cast<void *>(&HookLoadConfigIni), reinterpret_cast<void **>(&g_origLoadConfigIni),
		"LoadConfigIni");
	LogI("boot", "tweaks: installed (ScrollSpeed=%.2f)", GetConfig().scrollSpeed);
}

bool TweaksIntendedResolution(int &w, int &h)
{
	const int index = *reinterpret_cast<int *>(addr::kCurrentResolutionIndex);
	if (index < 0 || index >= ResolutionCount()) return false;
	const POINT size = (*reinterpret_cast<POINT **>(addr::kResolutionList + 4))[index];
	w = size.x;
	h = size.y;
	return true;
}

void TweaksApplySmoothScroll()
{
	ApplySmoothScroll();
}

void TweaksFrameTick()
{
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
