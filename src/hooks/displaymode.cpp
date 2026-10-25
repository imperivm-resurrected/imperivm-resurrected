// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "MinHook.h"
#include "addr/media.h"
#include "hooks/hookcount.h"
#include "hooks/audio.h"
#include "core/config.h"
#include "hooks/displaymode.h"
#include "core/log.h"
#include "i18n/tr.h"

namespace {

typedef LONG(WINAPI *ChangeDisplaySettingsA_t)(DEVMODEA *, DWORD);
typedef BOOL(WINAPI *EnumDisplaySettingsA_t)(LPCSTR, DWORD, DEVMODEA *);
typedef int(WINAPI *GetSystemMetrics_t)(int);
typedef BOOL(WINAPI *SetWindowPos_t)(HWND, HWND, int, int, int, int, UINT);
typedef BOOL(WINAPI *MoveWindow_t)(HWND, int, int, int, int, BOOL);
typedef BOOL(WINAPI *GetClientRect_t)(HWND, LPRECT);
typedef BOOL(WINAPI *GetCursorPos_t)(LPPOINT);
typedef BOOL(WINAPI *ClipCursor_t)(const RECT *);
typedef LONG(WINAPI *SetWindowLongA_t)(HWND, int, LONG);

ChangeDisplaySettingsA_t g_origChange = nullptr;
EnumDisplaySettingsA_t g_origEnum = nullptr;
GetSystemMetrics_t g_origMetrics = nullptr;
SetWindowPos_t g_origSetWindowPos = nullptr;
MoveWindow_t g_origMoveWindow = nullptr;
GetClientRect_t g_origGetClientRect = nullptr;
GetCursorPos_t g_origGetCursorPos = nullptr;
ClipCursor_t g_origClipCursor = nullptr;
bool g_gameClips = false;
SetWindowLongA_t g_origSetWindowLong = nullptr;

bool g_faking = false;
int g_logW = 0, g_logH = 0;
DWORD g_logBpp = 16;
HWND g_gameWnd = nullptr;
WNDPROC g_gameWndProc = nullptr;
LONG g_originalStyle = WS_POPUP | WS_VISIBLE;
DisplayWindowHandler g_windowHandler = nullptr;
std::string g_lastGeometry;
int g_clicksLogged = 0;
const int kClicksLogged = 5;

const LONG kWindowedStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

int DesktopW() { return g_origMetrics ? g_origMetrics(SM_CXSCREEN) : GetSystemMetrics(SM_CXSCREEN); }
int DesktopH() { return g_origMetrics ? g_origMetrics(SM_CYSCREEN) : GetSystemMetrics(SM_CYSCREEN); }

struct MonitorEntry {
	RECT rect;
	bool primary;
	std::wstring device;
};

BOOL CALLBACK EnumMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
	MONITORINFOEXW mi = {};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(monitor, &mi)) reinterpret_cast<std::vector<MonitorEntry> *>(param)->push_back({mi.rcMonitor, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0, mi.szDevice});
	return TRUE;
}

std::string FriendlyName(const std::wstring &device)
{
	UINT32 pathCount = 0, modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS || !pathCount) return "";
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) return "";
	for (UINT32 i = 0; i < pathCount; i++) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
		source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		source.header.size = sizeof(source);
		source.header.adapterId = paths[i].sourceInfo.adapterId;
		source.header.id = paths[i].sourceInfo.id;
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || device != source.viewGdiDeviceName) continue;
		DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
		target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		target.header.size = sizeof(target);
		target.header.adapterId = paths[i].targetInfo.adapterId;
		target.header.id = paths[i].targetInfo.id;
		if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS || !target.monitorFriendlyDeviceName[0]) return "";
		char utf8[160] = {};
		WideCharToMultiByte(CP_UTF8, 0, target.monitorFriendlyDeviceName, -1, utf8, sizeof(utf8), nullptr, nullptr);
		return utf8;
	}
	return "";
}

std::vector<MonitorEntry> Monitors()
{
	std::vector<MonitorEntry> list;
	EnumDisplayMonitors(nullptr, nullptr, &EnumMonitor, reinterpret_cast<LPARAM>(&list));
	std::stable_sort(list.begin(), list.end(), [](const MonitorEntry &a, const MonitorEntry &b) {
		if (a.primary != b.primary) return a.primary;
		return a.rect.left != b.rect.left ? a.rect.left < b.rect.left : a.rect.top < b.rect.top;
	});
	return list;
}

RECT TargetMonitor()
{
	const std::vector<MonitorEntry> list = Monitors();
	if (list.empty()) return RECT{0, 0, DesktopW(), DesktopH()};
	const int index = GetConfig().monitor;
	return list[index >= 0 && index < static_cast<int>(list.size()) ? index : 0].rect;
}

bool Enabled();

std::string TargetDevice()
{
	if (!Enabled()) return std::string();
	const std::vector<MonitorEntry> list = Monitors();
	if (list.empty()) return std::string();
	const int index = GetConfig().monitor;
	const std::wstring &device = list[index >= 0 && index < static_cast<int>(list.size()) ? index : 0].device;
	char name[64] = {};
	WideCharToMultiByte(CP_ACP, 0, device.c_str(), -1, name, sizeof(name), nullptr, nullptr);
	return name;
}

bool Enabled() { return GetConfig().fakeModeChange && GetConfig().renderEnabled; }
bool Windowed() { return Enabled() && GetConfig().windowed; }

RECT PhysClient()
{
	RECT rc = {};
	if (g_gameWnd) {
		g_origGetClientRect(g_gameWnd, &rc);
	}
	return rc;
}

bool Scaling()
{
	if (!g_faking || !g_gameWnd || g_logW <= 0) {
		return false;
	}
	RECT rc = PhysClient();
	return rc.right - rc.left != g_logW || rc.bottom - rc.top != g_logH;
}

void Mapping(float &s, float &offX, float &offY)
{
	RECT rc = PhysClient();
	float pw = static_cast<float>(rc.right - rc.left), ph = static_cast<float>(rc.bottom - rc.top);
	float sx = pw / g_logW, sy = ph / g_logH;
	s = sx < sy ? sx : sy;
	offX = (pw - g_logW * s) / 2;
	offY = (ph - g_logH * s) / 2;
}

void PhysToLog(int &x, int &y)
{
	float s, ox, oy;
	Mapping(s, ox, oy);
	int lx = static_cast<int>((x - ox) / s), ly = static_cast<int>((y - oy) / s);
	x = lx < 0 ? 0 : (lx >= g_logW ? g_logW - 1 : lx);
	y = ly < 0 ? 0 : (ly >= g_logH ? g_logH - 1 : ly);
}

void LogGeometry(const char *why)
{
	if (!g_gameWnd) return;
	RECT wr = {};
	GetWindowRect(g_gameWnd, &wr);
	const RECT cr = PhysClient();
	POINT origin = {0, 0};
	ClientToScreen(g_gameWnd, &origin);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	GetMonitorInfoA(MonitorFromWindow(g_gameWnd, MONITOR_DEFAULTTONEAREST), &mi);
	typedef UINT(WINAPI * GetDpiForWindow_t)(HWND);
	static const GetDpiForWindow_t dpiFor = reinterpret_cast<GetDpiForWindow_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	float s = 1, ox = 0, oy = 0;
	if (Scaling()) Mapping(s, ox, oy);
	char text[400];
	sprintf_s(text, "game %dx%d, window %ldx%ld at %ld,%ld, client %ldx%ld at %ld,%ld, monitor %ldx%ld at %ld,%ld, window DPI %u, scale %.3f, margins %.0f,%.0f",
		g_logW, g_logH, wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top, cr.right - cr.left, cr.bottom - cr.top, origin.x, origin.y,
		mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcMonitor.left, mi.rcMonitor.top, dpiFor ? dpiFor(g_gameWnd) : 0u,
		s, ox, oy);
	if (g_lastGeometry == text) return;
	g_lastGeometry = text;
	LogI("display", "geometry (%s): %s", why, text);
}

void LogClick(LPARAM lp)
{
	if (g_clicksLogged >= kClicksLogged) return;
	g_clicksLogged++;
	const int rx = static_cast<short>(LOWORD(lp)), ry = static_cast<short>(HIWORD(lp));
	int gx = rx, gy = ry;
	if (Scaling()) PhysToLog(gx, gy);
	POINT cursor = {};
	if (g_origGetCursorPos ? g_origGetCursorPos(&cursor) : GetCursorPos(&cursor)) ScreenToClient(g_gameWnd, &cursor);
	const RECT cr = PhysClient();
	LogI("display", "click %d: message at %d,%d of a %ldx%ld client, cursor at %ld,%ld -> game %d,%d of %dx%d", g_clicksLogged, rx, ry,
		cr.right - cr.left, cr.bottom - cr.top, cursor.x, cursor.y, gx, gy, g_logW, g_logH);
}

void PlaceWindow()
{
	if (!g_gameWnd || !g_faking) {
		return;
	}
	if (Windowed()) {
		g_origSetWindowLong(g_gameWnd, GWL_STYLE, kWindowedStyle);
		RECT r = {0, 0, GetConfig().windowW, GetConfig().windowH};
		AdjustWindowRectEx(&r, kWindowedStyle, FALSE, 0);
		int w = r.right - r.left, h = r.bottom - r.top;
		const RECT mr = TargetMonitor();
		g_origSetWindowPos(g_gameWnd, HWND_NOTOPMOST, mr.left + (mr.right - mr.left - w) / 2, mr.top + (mr.bottom - mr.top - h) / 2, w, h,
			SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
	else {
		const RECT mr = TargetMonitor();
		g_origSetWindowPos(g_gameWnd, nullptr, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	LogGeometry(Windowed() ? "windowed" : "full screen");
}

bool MonitorHasMode(int w, int h);
bool SwitchMonitorTo(int w, int h);

bool FakeMode(int w, int h, DWORD bpp, bool testOnly)
{
	if (!Enabled()) {
		return false;
	}
	int homeW = 0, homeH = 0;
	DisplayTargetSize(homeW, homeH);
	// A size the monitor has but larger than the one Windows keeps can only be shown by switching the
	// monitor to it, as games always did; any other size is drawn as asked and scaled to the monitor.
	if ((w > homeW || h > homeH) && !Windowed() && MonitorHasMode(w, h)) {
		if (testOnly) {
			return true;
		}
		if (!SwitchMonitorTo(w, h)) {
			return false;
		}
	}
	else {
		if (!DisplayModeAllowed(w, h)) {
			return false;
		}
		if (testOnly) {
			return true;
		}
		DisplayRestoreMonitor();
	}
	g_faking = true;
	g_logW = w;
	g_logH = h;
	g_logBpp = bpp ? bpp : 16;
	LogD("display", "the game requests %dx%d %lubpp -> drawn at that size on the monitor at %dx%d", w, h, g_logBpp, DesktopW(), DesktopH());
	PlaceWindow();
	return true;
}

std::mutex g_modesMx;
std::string g_modeDevice;
std::vector<DEVMODEA> g_modes;
bool g_modesReady = false;
bool g_realHas16 = false;

void PrepareModes();

LONG WINAPI HookChangeDisplaySettingsA(DEVMODEA *mode, DWORD flags)
{
	if (!mode) {
		g_faking = false;
		DisplayRestoreMonitor();
		if (!Enabled()) return g_origChange(nullptr, flags);
		LogD("display", "restore mode -> not needed, the monitor never changed");
		return DISP_CHANGE_SUCCESSFUL;
	}
	const int w = static_cast<int>(mode->dmPelsWidth), h = static_cast<int>(mode->dmPelsHeight);
	if (FakeMode(w, h, mode->dmBitsPerPel, (flags & CDS_TEST) != 0)) {
		return DISP_CHANGE_SUCCESSFUL;
	}
	LogD("display", "%dx%d -> real change", w, h);
	g_faking = false;
	PrepareModes();
	DEVMODEA real = *mode;
	if ((real.dmFields & DM_DISPLAYFREQUENCY) && real.dmDisplayFrequency == 0) real.dmFields &= ~static_cast<DWORD>(DM_DISPLAYFREQUENCY);
	if (real.dmBitsPerPel == 16 && !g_realHas16) {
		real.dmBitsPerPel = 32;
		real.dmFields |= DM_BITSPERPEL;
	}
	LONG result = g_origChange(&real, flags);
	if (result == DISP_CHANGE_BADMODE && real.dmBitsPerPel == 16) {
		real.dmBitsPerPel = 32;
		real.dmFields |= DM_BITSPERPEL;
		result = g_origChange(&real, flags);
	}
	if (result != DISP_CHANGE_SUCCESSFUL) {
		LogW("display", "Windows refused %dx%d at %lu bits (code %ld, flags 0x%lx, fields 0x%lx, frequency %lu)", w, h, real.dmBitsPerPel, result, flags,
			real.dmFields, real.dmDisplayFrequency);
		LogW("display", "  mode: device \"%s\", size %u, extra %u, version %u, display flags 0x%lx, orientation %lu, position %ld,%ld", real.dmDeviceName,
			static_cast<unsigned>(real.dmSize), static_cast<unsigned>(real.dmDriverExtra), static_cast<unsigned>(real.dmSpecVersion), real.dmDisplayFlags,
			real.dmDisplayOrientation, real.dmPosition.x, real.dmPosition.y);
	}
	return result;
}

void PrepareModes()
{
	const std::string target = TargetDevice();
	std::lock_guard<std::mutex> lock(g_modesMx);
	if (g_modesReady && target == g_modeDevice) return;
	bool has16 = false;
	g_modeDevice = target;
	g_modes.clear();
	const char *device = g_modeDevice.empty() ? nullptr : g_modeDevice.c_str();
	const DWORD t0 = GetTickCount();
	for (DWORD n = 0;; n++) {
		DEVMODEA dm = {};
		dm.dmSize = sizeof(dm);
		if (!g_origEnum(device, n, &dm)) break;
		if (dm.dmBitsPerPel == 16) has16 = true;
		dm.dmDriverExtra = 0;
		g_modes.push_back(dm);
	}
	g_realHas16 = has16;
	g_modesReady = true;
	LogI("display", "Windows lists %d display modes%s (%lu ms; kept for the next lists)", static_cast<int>(g_modes.size()),
		has16 ? "" : ", none of 16 bits: the game is offered the 32-bit ones as 16", GetTickCount() - t0);
}

bool g_monitorSwitched = false;

bool MonitorHasMode(int w, int h)
{
	if (w > 4096 || h > 2304) return false;
	PrepareModes();
	std::lock_guard<std::mutex> lock(g_modesMx);
	for (const DEVMODEA &m : g_modes) {
		if (static_cast<int>(m.dmPelsWidth) == w && static_cast<int>(m.dmPelsHeight) == h) return true;
	}
	return false;
}

bool SwitchMonitorTo(int w, int h)
{
	const RECT now = TargetMonitor();
	if (now.right - now.left == w && now.bottom - now.top == h) return true;
	DEVMODEA best = {};
	{
		PrepareModes();
		std::lock_guard<std::mutex> lock(g_modesMx);
		for (const DEVMODEA &m : g_modes) {
			if (static_cast<int>(m.dmPelsWidth) != w || static_cast<int>(m.dmPelsHeight) != h || m.dmBitsPerPel < 24) continue;
			if (!best.dmPelsWidth || m.dmDisplayFrequency > best.dmDisplayFrequency) best = m;
		}
	}
	if (!best.dmPelsWidth) return false;
	best.dmSize = sizeof(best);
	best.dmDriverExtra = 0;
	best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
	const std::string device = TargetDevice();
	// CDS_FULLSCREEN: Windows does not store it, so its own resolution is still the one it starts with.
	const LONG result = ChangeDisplaySettingsExA(device.empty() ? nullptr : device.c_str(), &best, nullptr, CDS_FULLSCREEN, nullptr);
	if (result != DISP_CHANGE_SUCCESSFUL) {
		LogW("display", "the monitor could not be switched to %dx%d (code %ld)", w, h, result);
		return false;
	}
	g_monitorSwitched = true;
	LogI("display", "the monitor is switched to %dx%d at %lu Hz while the game uses it", w, h, best.dmDisplayFrequency);
	return true;
}

BOOL CopyMode(DWORD index, DEVMODEA *mode, bool as16)
{
	if (!mode) return FALSE;
	const WORD size = mode->dmSize;
	if (size != sizeof(DEVMODEA) || mode->dmDriverExtra != 0) {
		const char *listed = g_modeDevice.empty() ? nullptr : g_modeDevice.c_str();
		const BOOL ok = g_origEnum(listed, index, mode);
		if (ok && as16) mode->dmBitsPerPel = 16;
		return ok;
	}
	std::lock_guard<std::mutex> lock(g_modesMx);
	if (index >= g_modes.size()) return FALSE;
	memcpy(mode, &g_modes[index], sizeof(DEVMODEA));
	mode->dmDriverExtra = 0;
	if (as16) mode->dmBitsPerPel = 16;
	return TRUE;
}

BOOL WINAPI HookEnumDisplaySettingsA(LPCSTR device, DWORD modeNum, DEVMODEA *mode)
{
	if (!device && modeNum != ENUM_CURRENT_SETTINGS && modeNum != ENUM_REGISTRY_SETTINGS) {
		PrepareModes();
		const DWORD n = static_cast<DWORD>(g_modes.size());
		if (modeNum < n) {
			return CopyMode(modeNum, mode, false);
		}
		if (g_realHas16 || modeNum >= 2 * n) {
			return FALSE;
		}
		return CopyMode(modeNum - n, mode, true);
	}
	const std::string target = device ? std::string() : TargetDevice();
	BOOL ok = g_origEnum(device ? device : target.empty() ? nullptr : target.c_str(), modeNum, mode);
	if (ok && g_faking && modeNum == ENUM_CURRENT_SETTINGS && !device) {
		mode->dmBitsPerPel = g_logBpp;
		mode->dmPelsWidth = g_logW;
		mode->dmPelsHeight = g_logH;
	}
	return ok;
}

typedef int(__fastcall *CurrentModeIndex_t)(void *os, void *edx);
CurrentModeIndex_t g_origCurrentModeIndex = nullptr;

int __fastcall HookCurrentModeIndex(void *os, void *edx)
{
	if (!g_faking) return g_origCurrentModeIndex(os, edx);
	DEVMODEA dm = {};
	for (DWORD i = 0;; i++) {
		dm.dmSize = sizeof(dm);
		dm.dmDriverExtra = 0;
		if (!HookEnumDisplaySettingsA(nullptr, i, &dm)) break;
		if (static_cast<int>(dm.dmPelsWidth) == g_logW && static_cast<int>(dm.dmPelsHeight) == g_logH && dm.dmBitsPerPel == g_logBpp) {
			return static_cast<int>(i) + 1;
		}
	}
	LogD("display", "current mode %dx%d %lubpp is not in the list; the game reads the monitor", g_logW, g_logH, g_logBpp);
	return g_origCurrentModeIndex(os, edx);
}

int WINAPI HookGetSystemMetrics(int index)
{
	if (g_faking) {
		if (index == SM_CXSCREEN) return g_logW;
		if (index == SM_CYSCREEN) return g_logH;
	}
	return g_origMetrics(index);
}

BOOL WINAPI HookSetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
	if (g_faking && hwnd == g_gameWnd && !(flags & SWP_NOSIZE)) {
		PlaceWindow();
		return TRUE;
	}
	if (Windowed() && hwnd == g_gameWnd && after == HWND_TOPMOST) {
		after = HWND_NOTOPMOST;
	}
	return g_origSetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

BOOL WINAPI HookMoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint)
{
	if (g_faking && hwnd == g_gameWnd) {
		PlaceWindow();
		return TRUE;
	}
	return g_origMoveWindow(hwnd, x, y, w, h, repaint);
}

BOOL WINAPI HookGetClientRect(HWND hwnd, LPRECT rc)
{
	if (rc && hwnd == g_gameWnd && Scaling()) {
		rc->left = rc->top = 0;
		rc->right = g_logW;
		rc->bottom = g_logH;
		return TRUE;
	}
	return g_origGetClientRect(hwnd, rc);
}

BOOL WINAPI HookGetCursorPos(LPPOINT pt)
{
	BOOL ok = g_origGetCursorPos(pt);
	if (ok && Scaling()) {
		POINT origin = {0, 0};
		ClientToScreen(g_gameWnd, &origin);
		int x = pt->x - origin.x, y = pt->y - origin.y;
		PhysToLog(x, y);
		pt->x = x + origin.x;
		pt->y = y + origin.y;
	}
	return ok;
}

BOOL WINAPI HookClipCursor(const RECT *rc)
{
	g_gameClips = rc != nullptr;
	if (rc && (Windowed() || !GetConfig().trapMouse)) {
		return g_origClipCursor(nullptr);
	}
	if (rc && Scaling()) {
		float s, ox, oy;
		Mapping(s, ox, oy);
		POINT origin = {0, 0};
		ClientToScreen(g_gameWnd, &origin);
		const RECT image = {static_cast<LONG>(origin.x + ox), static_cast<LONG>(origin.y + oy), static_cast<LONG>(origin.x + ox + g_logW * s),
			static_cast<LONG>(origin.y + oy + g_logH * s)};
		RECT p = image;
		if (!IntersectRect(&p, rc, &image)) p = image;
		static int logged = 0;
		if (logged++ < 5) {
			LogD("display", "mouse limited to %ld,%ld-%ld,%ld (the game asked for %ld,%ld-%ld,%ld)", p.left, p.top, p.right, p.bottom, rc->left, rc->top, rc->right,
				rc->bottom);
		}
		return g_origClipCursor(&p);
	}
	return g_origClipCursor(rc);
}

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_LBUTTONDOWN) LogClick(lp);
	if (msg == WM_DESTROY) DisplayRestoreMonitor();
	if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED) {
		LogGeometry("window changed");
		// The game clips the cursor to the window it has when it starts; the mod resizes the window
		// right after, and the clip would keep the old size until the game clips again.
		if (g_gameClips && GetForegroundWindow() == hwnd) {
			RECT r;
			g_origGetClientRect(hwnd, &r);
			MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT *>(&r), 2);
			HookClipCursor(&r);
		}
	}
	if (msg == WM_DISPLAYCHANGE) {
		std::lock_guard<std::mutex> lock(g_modesMx);
		g_modesReady = false;
	}
	if (Scaling() && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL) {
		int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
		PhysToLog(x, y);
		lp = MAKELPARAM(x, y);
	}
	if (g_windowHandler) return g_windowHandler(hwnd, msg, wp, lp);
	return CallWindowProcA(g_gameWndProc, hwnd, msg, wp, lp);
}

LONG WINAPI HookSetWindowLongA(HWND hwnd, int index, LONG value)
{
	if (hwnd == g_gameWnd && g_gameWndProc) {
		if (index == GWL_WNDPROC) {
			LONG prev = reinterpret_cast<LONG>(g_gameWndProc);
			g_gameWndProc = reinterpret_cast<WNDPROC>(value);
			return prev;
		}
		if (index == GWL_STYLE && Windowed() && g_faking) {
			value = kWindowedStyle;
		}
		if (index == GWL_EXSTYLE && Windowed()) {
			value &= ~WS_EX_TOPMOST;
		}
	}
	return g_origSetWindowLong(hwnd, index, value);
}

bool Hook(const char *name, void *detour, void **orig)
{
	HMODULE m = GetModuleHandleW(L"user32.dll");
	void *target = m ? reinterpret_cast<void *>(GetProcAddress(m, name)) : nullptr;
	if (target && HookCreate(target, detour, orig) == MH_OK && HookEnable(target) == MH_OK) {
		return true;
	}
	LogE("display", "could not intercept %s", name);
	return false;
}

}

bool DisplayFakeMode(int w, int h, DWORD bpp, bool testOnly)
{
	return FakeMode(w, h, bpp, testOnly);
}

bool InstallDisplayModeHooks()
{
	bool ok = Hook("GetSystemMetrics", reinterpret_cast<void *>(&HookGetSystemMetrics), reinterpret_cast<void **>(&g_origMetrics)) &&
		Hook("GetClientRect", reinterpret_cast<void *>(&HookGetClientRect), reinterpret_cast<void **>(&g_origGetClientRect));
	if (ok && GetConfig().renderEnabled) {
		ok = Hook("ChangeDisplaySettingsA", reinterpret_cast<void *>(&HookChangeDisplaySettingsA), reinterpret_cast<void **>(&g_origChange)) &&
			Hook("EnumDisplaySettingsA", reinterpret_cast<void *>(&HookEnumDisplaySettingsA), reinterpret_cast<void **>(&g_origEnum)) &&
			Hook("SetWindowLongA", reinterpret_cast<void *>(&HookSetWindowLongA), reinterpret_cast<void **>(&g_origSetWindowLong));
	}
	if (ok && Enabled()) {
		ok = Hook("SetWindowPos", reinterpret_cast<void *>(&HookSetWindowPos), reinterpret_cast<void **>(&g_origSetWindowPos)) &&
			Hook("MoveWindow", reinterpret_cast<void *>(&HookMoveWindow), reinterpret_cast<void **>(&g_origMoveWindow)) &&
			Hook("GetCursorPos", reinterpret_cast<void *>(&HookGetCursorPos), reinterpret_cast<void **>(&g_origGetCursorPos)) &&
			Hook("ClipCursor", reinterpret_cast<void *>(&HookClipCursor), reinterpret_cast<void **>(&g_origClipCursor));
		void *current = reinterpret_cast<void *>(addr::kCurrentDisplayModeIndex);
		if (ok && !(HookCreate(current, reinterpret_cast<void *>(&HookCurrentModeIndex), reinterpret_cast<void **>(&g_origCurrentModeIndex)) == MH_OK &&
					  HookEnable(current) == MH_OK)) {
			LogE("display", "could not intercept the game's current display mode");
			ok = false;
		}
	}
	LogAt(ok ? kLogInfo : kLogError, "display", "hooks %s (FakeModeChange=%d, mode %s)", ok ? "installed" : "with errors",
		GetConfig().fakeModeChange, GetConfig().windowed ? "windowed" : "full screen");
	return ok;
}

void DisplayAttachWindow(HWND hwnd)
{
	if (g_gameWnd || !hwnd) {
		return;
	}
	g_gameWnd = hwnd;
	g_originalStyle = GetWindowLongA(hwnd, GWL_STYLE);
	LogGeometry("game window");
	AudioSetBackgroundMute(false);
	if (GetConfig().renderEnabled) {
		g_gameWndProc = reinterpret_cast<WNDPROC>(
			g_origSetWindowLong(hwnd, GWL_WNDPROC, reinterpret_cast<LONG>(&GameWndProc)));
	}
	if (Enabled()) {
		PlaceWindow();
		if (Windowed() || !GetConfig().trapMouse) {
			g_origClipCursor(nullptr);
		}
	}
}

void DisplayGetLogicalSize(HWND hwnd, int &w, int &h)
{
	if (g_faking && g_logW > 0) {
		w = g_logW;
		h = g_logH;
		return;
	}
	RECT rc = {};
	g_origGetClientRect(hwnd, &rc);
	w = rc.right - rc.left;
	h = rc.bottom - rc.top;
}

BOOL DisplayRealClientRect(HWND hwnd, RECT *rc)
{
	return g_origGetClientRect ? g_origGetClientRect(hwnd, rc) : GetClientRect(hwnd, rc);
}

void DisplayApplySettings(bool windowModeChanged)
{
	if (!g_gameWnd || !Enabled()) {
		return;
	}
	if (windowModeChanged) {
		if (!Windowed()) {
			g_origSetWindowLong(g_gameWnd, GWL_STYLE, g_originalStyle);
		}
		PlaceWindow();
	}
	if (Windowed() || !GetConfig().trapMouse) {
		g_origClipCursor(nullptr);
	}
	else {
		RECT r;
		g_origGetClientRect(g_gameWnd, &r);
		MapWindowPoints(g_gameWnd, nullptr, reinterpret_cast<POINT *>(&r), 2);
		g_origClipCursor(&r);
	}
}

void DisplaySetWindowHandler(DisplayWindowHandler handler)
{
	g_windowHandler = handler;
}

LRESULT DisplayForwardMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	return CallWindowProcA(g_gameWndProc, hwnd, msg, wp, lp);
}

bool DisplayIsWindowed()
{
	return Windowed();
}

bool DisplayFakesModeChange()
{
	return Enabled();
}

bool DisplayModeAllowed(int w, int h)
{
	if (!Enabled()) return true;
	int monitorW = 0, monitorH = 0;
	DisplayTargetSize(monitorW, monitorH);
	if (w <= 0 || h <= 0) return false;
	if (w <= monitorW && h <= monitorH) return true;
	if (!GetConfig().extendedView) return false;
	return w <= (2 * monitorW < 4096 ? 2 * monitorW : 4096) && h <= (2 * monitorH < 2304 ? 2 * monitorH : 2304);
}

std::vector<POINT> DisplayExtendedSizes()
{
	int monitorW = 0, monitorH = 0;
	DisplayTargetSize(monitorW, monitorH);
	std::vector<POINT> sizes;
	for (float factor : {1.25f, 1.5f, 2.0f}) {
		const int w = static_cast<int>(monitorW * factor) / 8 * 8, h = static_cast<int>(monitorH * factor) / 8 * 8;
		if (w <= 4096 && h <= 2304) sizes.push_back({w, h});
	}
	return sizes;
}

std::vector<POINT> DisplayWindowsSizes()
{
	PrepareModes();
	std::vector<POINT> sizes;
	std::lock_guard<std::mutex> lock(g_modesMx);
	for (const DEVMODEA &m : g_modes) {
		const POINT p = {static_cast<LONG>(m.dmPelsWidth), static_cast<LONG>(m.dmPelsHeight)};
		if (std::find_if(sizes.begin(), sizes.end(), [&](const POINT &q) { return q.x == p.x && q.y == p.y; }) == sizes.end()) sizes.push_back(p);
	}
	return sizes;
}

void DisplayTargetSize(int &w, int &h)
{
	const RECT monitor = TargetMonitor();
	w = monitor.right - monitor.left;
	h = monitor.bottom - monitor.top;
	if (!g_monitorSwitched) return;
	const std::string device = TargetDevice();
	DEVMODEA home = {};
	home.dmSize = sizeof(home);
	const auto enumerate = g_origEnum ? g_origEnum : &EnumDisplaySettingsA;
	if (enumerate(device.empty() ? nullptr : device.c_str(), ENUM_REGISTRY_SETTINGS, &home) && home.dmPelsWidth && home.dmPelsHeight) {
		w = static_cast<int>(home.dmPelsWidth);
		h = static_cast<int>(home.dmPelsHeight);
	}
}

bool DisplayMonitorHasMode(int w, int h)
{
	return !Windowed() && MonitorHasMode(w, h);
}

void DisplayRestoreMonitor()
{
	if (!g_monitorSwitched) return;
	g_monitorSwitched = false;
	const std::string device = TargetDevice();
	ChangeDisplaySettingsExA(device.empty() ? nullptr : device.c_str(), nullptr, nullptr, 0, nullptr);
	LogI("display", "the monitor is back to its own resolution");
}

std::vector<std::string> DisplayMonitorLabels()
{
	std::vector<std::string> labels;
	const std::vector<MonitorEntry> list = Monitors();
	for (size_t i = 0; i < list.size(); i++) {
		std::string name = FriendlyName(list[i].device);
		if (name.empty() || name == "Generic PnP Monitor") name = Tr("Monitor") + " " + std::to_string(i + 1);
		labels.push_back(std::to_string(i + 1) + " - " + name + " " + std::to_string(list[i].rect.right - list[i].rect.left) + "x" +
			std::to_string(list[i].rect.bottom - list[i].rect.top) + (list[i].primary ? " (" + Tr("main") + ")" : ""));
	}
	if (labels.empty()) labels.push_back("1");
	return labels;
}
