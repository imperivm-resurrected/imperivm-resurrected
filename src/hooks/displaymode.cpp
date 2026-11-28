// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "addr/media.h"
#include "addr/engine.h"
#include "hooks/hookcount.h"
#include "hooks/audio.h"
#include "core/config.h"
#include "core/imagefit.h"
#include "hooks/displaymode.h"
#include "hooks/respick.h"
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
bool (*g_windowCursorActive)() = nullptr;
bool g_windowCursorClipped = false;
RECT g_windowCursorArea = {};
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
bool (*g_resizeCanvas)(int, int) = nullptr;
bool g_dragging = false, g_holdResize = false, g_syncing = false, g_placing = false, g_firstWindow = true;
constexpr UINT_PTR kResizeTimer = 0x42415736;

const LONG kWindowedStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

int DesktopW() { return g_origMetrics ? g_origMetrics(SM_CXSCREEN) : GetSystemMetrics(SM_CXSCREEN); }
int DesktopH() { return g_origMetrics ? g_origMetrics(SM_CYSCREEN) : GetSystemMetrics(SM_CYSCREEN); }

struct MonitorEntry {
	RECT rect;
	RECT work;
	bool primary;
	std::wstring device;
};

BOOL CALLBACK EnumMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
	MONITORINFOEXW mi = {};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(monitor, &mi)) reinterpret_cast<std::vector<MonitorEntry> *>(param)->push_back({mi.rcMonitor, mi.rcWork, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0, mi.szDevice});
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

// A negative index is the monitor chosen in the settings; one that is no longer connected is the main one.
int MonitorIndex(int index, size_t count)
{
	if (index < 0) index = GetConfig().monitor;
	return index >= 0 && index < static_cast<int>(count) ? index : 0;
}

MonitorEntry MonitorAt(int index)
{
	const std::vector<MonitorEntry> list = Monitors();
	if (list.empty()) {
		const RECT desktop = {0, 0, DesktopW(), DesktopH()};
		return MonitorEntry{desktop, desktop, true, std::wstring()};
	}
	return list[MonitorIndex(index, list.size())];
}

bool IsTargetMonitor(int index)
{
	const size_t count = Monitors().size();
	return MonitorIndex(index, count) == MonitorIndex(-1, count);
}

RECT TargetMonitor()
{
	return MonitorAt(-1).rect;
}

bool Enabled();

std::string DeviceName(const MonitorEntry &monitor)
{
	char name[64] = {};
	WideCharToMultiByte(CP_ACP, 0, monitor.device.c_str(), -1, name, sizeof(name), nullptr, nullptr);
	return name;
}

// The game asks for the display modes one at a time and every answer needs the target monitor: its name is kept for a second, or
// until the monitor setting or the monitors change.
const DWORD kTargetDeviceKeptMs = 1000;

struct TargetDeviceCache {
	std::mutex mx;
	std::string device;
	int monitor = 0;
	DWORD at = 0;
	bool valid = false;
};
TargetDeviceCache g_target;

void ForgetTargetDevice()
{
	std::lock_guard<std::mutex> lock(g_target.mx);
	g_target.valid = false;
}

std::string TargetDevice()
{
	if (!Enabled()) return std::string();
	const int monitor = GetConfig().monitor;
	const DWORD now = GetTickCount();
	{
		std::lock_guard<std::mutex> lock(g_target.mx);
		if (g_target.valid && g_target.monitor == monitor && now - g_target.at < kTargetDeviceKeptMs) return g_target.device;
	}
	const std::string device = DeviceName(MonitorAt(-1));
	std::lock_guard<std::mutex> lock(g_target.mx);
	g_target.device = device;
	g_target.monitor = monitor;
	g_target.at = now;
	g_target.valid = true;
	return device;
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

// The same scale and margins the presenter draws the image with (render.cpp).
void Mapping(float &s, float &offX, float &offY)
{
	const RECT rc = PhysClient();
	const int pw = rc.right - rc.left, ph = rc.bottom - rc.top;
	s = ImageScale(pw, ph, g_logW, g_logH, GetConfig().imageScaling);
	offX = static_cast<float>(ImageMargin(pw, g_logW, s));
	offY = static_cast<float>(ImageMargin(ph, g_logH, s));
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

RECT WindowFrame(const MonitorEntry &monitor)
{
	RECT frame = {};
	using GetDpi = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
	using Adjust = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
	static HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	static auto getDpi = shcore ? reinterpret_cast<GetDpi>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
	static auto adjust = reinterpret_cast<Adjust>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
	const DWORD ex = g_gameWnd ? GetWindowLongW(g_gameWnd, GWL_EXSTYLE) : 0;
	UINT x = 0, y = 0;
	if (!getDpi || !adjust || FAILED(getDpi(MonitorFromRect(&monitor.rect, MONITOR_DEFAULTTONEAREST), 0, &x, &y)) ||
		!adjust(&frame, kWindowedStyle, FALSE, ex, x)) AdjustWindowRectEx(&frame, kWindowedStyle, FALSE, ex);
	return frame;
}

void QueueResize(HWND hwnd)
{
	if (hwnd && Windowed() && g_resizeCanvas && g_faking && !g_dragging && !g_holdResize && !g_syncing && !g_placing && !IsIconic(hwnd))
		SetTimer(hwnd, kResizeTimer, 140, nullptr);
}

void PlaceWindow()
{
	if (!g_gameWnd || !g_faking || g_syncing || g_placing) {
		return;
	}
	g_placing = true;
	if (Windowed() && IsZoomed(g_gameWnd)) {
		g_origSetWindowLong(g_gameWnd, GWL_STYLE, kWindowedStyle | WS_MAXIMIZE);
	}
	else if (Windowed()) {
		g_origSetWindowLong(g_gameWnd, GWL_STYLE, kWindowedStyle);
		// Restore the physical client size even when the canvas has to use a different size.
		const MonitorEntry monitor = MonitorAt(-1);
		const RECT frame = WindowFrame(monitor);
		const ResSize saved = {GetConfig().windowClientW, GetConfig().windowClientH};
		const int clientW = g_firstWindow && saved.w > 0 ? saved.w : g_logW;
		const int clientH = g_firstWindow && saved.h > 0 ? saved.h : g_logH;
		g_firstWindow = false;
		// Centred in the part of the monitor the taskbar leaves, and never larger than it.
		const RECT wa = monitor.work;
		const int w = (std::min)(clientW + frame.right - frame.left, wa.right - wa.left);
		const int h = (std::min)(clientH + frame.bottom - frame.top, wa.bottom - wa.top);
		g_origSetWindowPos(g_gameWnd, HWND_NOTOPMOST, wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
			SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
	else {
		const RECT mr = TargetMonitor();
		const LONG style = GetWindowLongA(g_gameWnd, GWL_STYLE);
		const LONG fullscreen = (style & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE)) | WS_POPUP;
		if (style != fullscreen) g_origSetWindowLong(g_gameWnd, GWL_STYLE, fullscreen);
		g_origSetWindowPos(g_gameWnd, nullptr, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
			SWP_NOZORDER | SWP_NOACTIVATE | (style != fullscreen ? SWP_FRAMECHANGED : 0));
	}
	g_placing = false;
	QueueResize(g_gameWnd);
	LogGeometry(Windowed() ? "windowed" : "full screen");
}

bool MonitorHasMode(int w, int h);
bool SwitchMonitorTo(int w, int h);

bool FakeMode(int w, int h, DWORD bpp, bool testOnly)
{
	if (!Enabled() || !FrameSizeSupported({w, h})) {
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
		if (!g_syncing && !DisplayModeAllowed(w, h)) {
			return false;
		}
		if (testOnly) {
			return true;
		}
		DisplayRestoreMonitor();
	}
	// A new resolution chosen with the window maximized gives the window that size.
	const bool changed = !g_faking || w != g_logW || h != g_logH;
	if (!g_syncing && changed && g_gameWnd && Windowed() && IsZoomed(g_gameWnd)) ShowWindow(g_gameWnd, SW_RESTORE);
	g_faking = true;
	g_logW = w;
	g_logH = h;
	g_logBpp = bpp ? bpp : 16;
	LogD("display", "the game requests %dx%d %lubpp -> drawn at that size on the monitor at %dx%d", w, h, g_logBpp, DesktopW(), DesktopH());
	if (changed) PlaceWindow();
	return true;
}

std::mutex g_modesMx;
std::string g_modeDevice;
std::vector<DEVMODEA> g_modes;
std::map<std::string, std::vector<POINT>> g_otherSizes;
bool g_modesReady = false;
bool g_realHas16 = false;

void PrepareModes();

bool RealHas16()
{
	std::lock_guard<std::mutex> lock(g_modesMx);
	return g_realHas16;
}

LONG WINAPI HookChangeDisplaySettingsA(DEVMODEA *mode, DWORD flags)
{
	if (!mode) {
		if (g_syncing) return DISP_CHANGE_SUCCESSFUL;
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
	if (Enabled() && (!FrameSizeSupported({w, h}) || Windowed())) return DISP_CHANGE_BADMODE;
	LogD("display", "%dx%d -> real change", w, h);
	g_faking = false;
	PrepareModes();
	DEVMODEA real = *mode;
	if ((real.dmFields & DM_DISPLAYFREQUENCY) && real.dmDisplayFrequency == 0) real.dmFields &= ~static_cast<DWORD>(DM_DISPLAYFREQUENCY);
	if (real.dmBitsPerPel == 16 && !RealHas16()) {
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
	if (!FrameSizeSupported({w, h})) return false;
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
			// A television lists 1080i next to 1080p; the interlaced one only if it is all there is.
			const bool interlaced = (m.dmDisplayFlags & DM_INTERLACED) != 0, bestInterlaced = (best.dmDisplayFlags & DM_INTERLACED) != 0;
			if (!best.dmPelsWidth || (bestInterlaced && !interlaced) || (interlaced == bestInterlaced && m.dmDisplayFrequency > best.dmDisplayFrequency)) best = m;
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
	ForgetTargetDevice();
	LogI("display", "the monitor is switched to %dx%d at %lu Hz while the game uses it", w, h, best.dmDisplayFrequency);
	return true;
}

BOOL CopyMode(DWORD index, DEVMODEA *mode, bool as16)
{
	if (!mode) return FALSE;
	const WORD size = mode->dmSize;
	if (size != sizeof(DEVMODEA) || mode->dmDriverExtra != 0) {
		std::string listed;
		{
			std::lock_guard<std::mutex> lock(g_modesMx);
			listed = g_modeDevice;
		}
		const BOOL ok = g_origEnum(listed.empty() ? nullptr : listed.c_str(), index, mode);
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
		DWORD n = 0;
		bool has16 = false;
		{
			std::lock_guard<std::mutex> lock(g_modesMx);
			n = static_cast<DWORD>(g_modes.size());
			has16 = g_realHas16;
		}
		if (modeNum < n) {
			return CopyMode(modeNum, mode, false);
		}
		if (!has16 && modeNum < 2 * n) return CopyMode(modeNum - n, mode, true);
		if (!g_faking || modeNum != (has16 ? n : 2 * n) || !mode || mode->dmSize != sizeof(*mode) || mode->dmDriverExtra) return FALSE;
		DEVMODEA canvas = {};
		canvas.dmSize = sizeof(canvas);
		canvas.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		canvas.dmBitsPerPel = g_logBpp;
		canvas.dmPelsWidth = g_logW;
		canvas.dmPelsHeight = g_logH;
		*mode = canvas;
		return TRUE;
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
	PrepareModes();
	std::lock_guard<std::mutex> lock(g_modesMx);
	return static_cast<int>(g_modes.size()) * (g_realHas16 ? 1 : 2) + 1;
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
	const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
	if (g_faking && hwnd == g_gameWnd && !(flags & SWP_NOSIZE) && caller >= addr::kGameTextBegin && caller < addr::kGameTextEnd) {
		return TRUE;
	}
	if (Windowed() && hwnd == g_gameWnd && after == HWND_TOPMOST) {
		after = HWND_NOTOPMOST;
	}
	return g_origSetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

BOOL WINAPI HookMoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint)
{
	const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
	if (g_faking && hwnd == g_gameWnd && caller >= addr::kGameTextBegin && caller < addr::kGameTextEnd) {
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

void ReleaseWindowCursor()
{
	if (!g_windowCursorClipped) return;
	g_origClipCursor(nullptr);
	g_windowCursorClipped = false;
}

// Where the game's picture is on the screen now: the client area, or the scaled image inside it.
RECT GameImageOnScreen()
{
	RECT r = {};
	g_origGetClientRect(g_gameWnd, &r);
	MapWindowPoints(g_gameWnd, nullptr, reinterpret_cast<POINT *>(&r), 2);
	if (!Scaling()) return r;
	float s, ox, oy;
	Mapping(s, ox, oy);
	return {static_cast<LONG>(r.left + ox), static_cast<LONG>(r.top + oy), static_cast<LONG>(r.left + ox + g_logW * s), static_cast<LONG>(r.top + oy + g_logH * s)};
}

// The game shuts the cursor in the window it had when it started, and the mod moves the window to its monitor right after: kept as
// asked, the cursor stayed shut in the other monitor, with no game in it. The clip follows the window as it is now, holds only while
// the game is in front (another window brought forward lets the cursor go), and coming back to the game puts the cursor in it.
void TrapCursor(const RECT *asked, bool active)
{
	// A window keeps its own clip, which follows the cursor over the window (DisplayRefreshWindowCursor).
	if (Windowed()) {
		DisplayRefreshWindowCursor();
		return;
	}
	if (!g_gameWnd || !GetConfig().trapMouse || (!active && GetForegroundWindow() != g_gameWnd) || IsIconic(g_gameWnd)) {
		g_origClipCursor(nullptr);
		return;
	}
	const RECT image = GameImageOnScreen();
	RECT p = image;
	if (!asked || !IntersectRect(&p, asked, &image)) p = image;
	POINT at = {};
	// Out of the game (on another monitor at the start, or before Alt+Tab): from the middle, not stuck at the nearest edge, which scrolls.
	if (g_origGetCursorPos(&at) && !PtInRect(&p, at)) {
		SetCursorPos((p.left + p.right) / 2, (p.top + p.bottom) / 2);
		LogI("display", "the cursor, at %ld,%ld, out of the game in front: to its middle", at.x, at.y);
	}
	static int logged = 0;
	if (logged++ < 5) {
		LogD("display", "mouse limited to %ld,%ld-%ld,%ld (the game asked for %s)", p.left, p.top, p.right, p.bottom, asked ? "an area" : "its window");
	}
	g_origClipCursor(&p);
}

BOOL WINAPI HookClipCursor(const RECT *rc)
{
	g_gameClips = rc != nullptr;
	if (Windowed()) {
		DisplayRefreshWindowCursor();
		return TRUE;
	}
	if (!rc || !g_gameWnd) return g_origClipCursor(rc);
	TrapCursor(rc, false);
	return TRUE;
}

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_TIMER && wp == kResizeTimer) {
		KillTimer(hwnd, kResizeTimer);
		if (Windowed() && g_resizeCanvas && g_faking && !g_dragging && !g_holdResize && !g_syncing && !g_placing && !IsIconic(hwnd)) {
			const RECT rc = PhysClient();
			const ResSize canvas = WindowFrameForClient({rc.right, rc.bottom});
			bool ok = canvas.w > 0;
			if (ok && (canvas.w != g_logW || canvas.h != g_logH)) {
				const int oldW = g_logW, oldH = g_logH;
				const DWORD oldBpp = g_logBpp;
				g_syncing = true;
				ok = g_resizeCanvas(canvas.w, canvas.h);
				g_syncing = false;
				if (!ok) {
					g_logW = oldW;
					g_logH = oldH;
					g_logBpp = oldBpp;
				} else {
					LogGeometry("resized client");
				}
			}
			if (ok) SaveWindowClient(rc.right, rc.bottom);
		}
		return 0;
	}
	if (msg == WM_ENTERSIZEMOVE) {
		g_dragging = true;
		ReleaseWindowCursor();
		KillTimer(hwnd, kResizeTimer);
	}
	if (msg == WM_EXITSIZEMOVE) g_dragging = false;
	if (msg == WM_DPICHANGED && g_faking && Windowed() && lp && !g_placing) {
		const RECT &r = *reinterpret_cast<const RECT *>(lp);
		g_origSetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (msg == WM_EXITSIZEMOVE || msg == WM_SIZE || msg == WM_DPICHANGED) QueueResize(hwnd);
	if (msg == WM_LBUTTONDOWN) LogClick(lp);
	if ((msg == WM_ACTIVATEAPP && !wp) || (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) || msg == WM_DESTROY)
		ReleaseWindowCursor();
	if (msg == WM_DESTROY) {
		KillTimer(hwnd, kResizeTimer);
		DisplayRestoreMonitor();
	}
	if (msg == WM_SIZE || msg == WM_MOVE || msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED) {
		if (!g_dragging && msg != WM_MOVE) LogGeometry("window changed");
		// The window moved to its monitor or changed size: the clip goes with it (or is let go while the game is not in front).
		if (!Windowed() && g_gameClips) TrapCursor(nullptr, false);
	}
	// In front again (Alt+Tab, a click, the start on the other monitor): the cursor is shut in the game, from its middle if it was
	// out; behind another window, it is free.
	if (msg == WM_ACTIVATEAPP && g_gameClips && !Windowed()) {
		if (wp) TrapCursor(nullptr, true);
		else g_origClipCursor(nullptr);
	}
	if (msg == WM_DISPLAYCHANGE) {
		ForgetTargetDevice();
		std::lock_guard<std::mutex> lock(g_modesMx);
		g_modesReady = false;
		g_otherSizes.clear();
	}
	if (Scaling() && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL) {
		int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
		PhysToLog(x, y);
		lp = MAKELPARAM(x, y);
	}
	const LRESULT result = g_windowHandler ? g_windowHandler(hwnd, msg, wp, lp) : CallWindowProcA(g_gameWndProc, hwnd, msg, wp, lp);
	if (msg == WM_MOUSEMOVE || msg == WM_KEYUP || msg == WM_EXITSIZEMOVE || msg == WM_SIZE || msg == WM_MOVE || msg == WM_DPICHANGED)
		DisplayRefreshWindowCursor();
	return result;
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

template <class Fn> bool Hook(const char *name, Fn *detour, Fn **orig)
{
	HMODULE m = GetModuleHandleW(L"user32.dll");
	void *target = m ? reinterpret_cast<void *>(GetProcAddress(m, name)) : nullptr;
	if (target && HookInstall(target, detour, orig)) {
		return true;
	}
	LogE("display", "could not intercept %s", name);
	return false;
}

// The real function for a hook that failed or was not tried: the mod calls its g_orig* pointers directly.
template <class Fn> void RealIfMissing(const char *name, Fn **orig)
{
	HMODULE m = GetModuleHandleW(L"user32.dll");
	if (!*orig && m) *orig = reinterpret_cast<Fn *>(GetProcAddress(m, name));
}

}

bool DisplayFakeMode(int w, int h, DWORD bpp, bool testOnly)
{
	return FakeMode(w, h, bpp, testOnly);
}

bool InstallDisplayModeHooks(bool knownExe)
{
	bool ok = Hook("GetSystemMetrics", &HookGetSystemMetrics, &g_origMetrics) && Hook("GetClientRect", &HookGetClientRect, &g_origGetClientRect);
	if (ok && GetConfig().renderEnabled) {
		ok = Hook("ChangeDisplaySettingsA", &HookChangeDisplaySettingsA, &g_origChange) && Hook("EnumDisplaySettingsA", &HookEnumDisplaySettingsA, &g_origEnum) &&
			Hook("SetWindowLongA", &HookSetWindowLongA, &g_origSetWindowLong);
	}
	if (ok && Enabled()) {
		ok = Hook("SetWindowPos", &HookSetWindowPos, &g_origSetWindowPos) && Hook("MoveWindow", &HookMoveWindow, &g_origMoveWindow) &&
			Hook("GetCursorPos", &HookGetCursorPos, &g_origGetCursorPos) && Hook("ClipCursor", &HookClipCursor, &g_origClipCursor);
		if (ok && knownExe && !HookInstall(addr::kCurrentDisplayModeIndex, &HookCurrentModeIndex, &g_origCurrentModeIndex)) {
			LogE("display", "could not intercept the game's current display mode");
			ok = false;
		}
	}
	RealIfMissing("GetSystemMetrics", &g_origMetrics);
	RealIfMissing("GetClientRect", &g_origGetClientRect);
	RealIfMissing("ChangeDisplaySettingsA", &g_origChange);
	RealIfMissing("EnumDisplaySettingsA", &g_origEnum);
	RealIfMissing("SetWindowLongA", &g_origSetWindowLong);
	RealIfMissing("SetWindowPos", &g_origSetWindowPos);
	RealIfMissing("MoveWindow", &g_origMoveWindow);
	RealIfMissing("GetCursorPos", &g_origGetCursorPos);
	RealIfMissing("ClipCursor", &g_origClipCursor);
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

HWND DisplayGameWindow()
{
	return g_gameWnd;
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
		// The size the game is at is set again for the new mode or monitor: it may need that monitor switched,
		// or no longer fit and be shrunk.
		if (!g_faking || !FakeMode(g_logW, g_logH, g_logBpp, false)) DisplayRestoreMonitor();
		PlaceWindow();
	}
	if (Windowed()) {
		DisplayRefreshWindowCursor();
	}
	else {
		ReleaseWindowCursor();
		TrapCursor(nullptr, false);
	}
}

void DisplaySetWindowHandler(DisplayWindowHandler handler)
{
	g_windowHandler = handler;
}

void DisplaySetResizeHandler(bool (*apply)(int, int))
{
	g_resizeCanvas = apply;
}

void DisplayHoldResize(bool hold)
{
	g_holdResize = hold;
	if (hold) ReleaseWindowCursor();
	else QueueResize(g_gameWnd);
}

void DisplaySetWindowCursorCheck(bool (*active)())
{
	g_windowCursorActive = active;
}

void DisplayRefreshWindowCursor()
{
	if (!Windowed() || !g_gameWnd || !GetConfig().trapMouse || g_dragging || g_holdResize || IsIconic(g_gameWnd) ||
		GetForegroundWindow() != g_gameWnd || !g_windowCursorActive || !g_windowCursorActive()) {
		ReleaseWindowCursor();
		return;
	}
	RECT area = PhysClient();
	if (IsRectEmpty(&area)) {
		ReleaseWindowCursor();
		return;
	}
	MapWindowPoints(g_gameWnd, nullptr, reinterpret_cast<POINT *>(&area), 2);
	if (g_windowCursorClipped && EqualRect(&area, &g_windowCursorArea)) return;
	POINT cursor;
	if (!g_windowCursorClipped && (!g_origGetCursorPos(&cursor) || !PtInRect(&area, cursor))) return;
	if (g_origClipCursor(&area)) {
		g_windowCursorArea = area;
		g_windowCursorClipped = true;
	}
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
	if (!FrameSizeSupported({w, h})) return false;
	if (w <= monitorW && h <= monitorH) return true;
	if (!GetConfig().extendedView) return false;
	return w <= 2ll * monitorW && h <= 2ll * monitorH;
}

std::vector<POINT> DisplayExtendedSizes(int monitor)
{
	int monitorW = 0, monitorH = 0;
	DisplayMonitorSize(monitor, monitorW, monitorH);
	std::vector<POINT> sizes;
	for (float factor : {1.25f, 1.5f, 2.0f}) {
		const int w = static_cast<int>(monitorW * factor) / 8 * 8, h = static_cast<int>(monitorH * factor) / 8 * 8;
		if (FrameSizeSupported({w, h})) sizes.push_back({w, h});
	}
	return sizes;
}

std::vector<POINT> DisplayWindowsSizes(int monitor)
{
	std::vector<POINT> sizes;
	const auto add = [&](DWORD w, DWORD h) {
		const POINT p = {static_cast<LONG>(w), static_cast<LONG>(h)};
		if (std::find_if(sizes.begin(), sizes.end(), [&](const POINT &q) { return q.x == p.x && q.y == p.y; }) == sizes.end()) sizes.push_back(p);
	};
	if (!Enabled() || IsTargetMonitor(monitor)) {
		PrepareModes();
		std::lock_guard<std::mutex> lock(g_modesMx);
		for (const DEVMODEA &m : g_modes) add(m.dmPelsWidth, m.dmPelsHeight);
		return sizes;
	}
	// Another monitor, while the player looks at it in the options: the game keeps enumerating the chosen one.
	const std::string device = DeviceName(MonitorAt(monitor));
	{
		std::lock_guard<std::mutex> lock(g_modesMx);
		const auto known = g_otherSizes.find(device);
		if (known != g_otherSizes.end()) return known->second;
	}
	const auto enumerate = g_origEnum ? g_origEnum : &EnumDisplaySettingsA;
	for (DWORD n = 0;; n++) {
		DEVMODEA dm = {};
		dm.dmSize = sizeof(dm);
		if (!enumerate(device.empty() ? nullptr : device.c_str(), n, &dm)) break;
		add(dm.dmPelsWidth, dm.dmPelsHeight);
	}
	std::lock_guard<std::mutex> lock(g_modesMx);
	g_otherSizes[device] = sizes;
	return sizes;
}

void DisplayWindowArea(int monitor, int &w, int &h)
{
	const MonitorEntry target = MonitorAt(monitor);
	const RECT wa = target.work, frame = WindowFrame(target);
	w = (wa.right - wa.left) - (frame.right - frame.left);
	h = (wa.bottom - wa.top) - (frame.bottom - frame.top);
}

void DisplayMonitorSize(int monitor, int &w, int &h)
{
	if (IsTargetMonitor(monitor)) {
		DisplayTargetSize(w, h);
		return;
	}
	const RECT r = MonitorAt(monitor).rect;
	w = r.right - r.left;
	h = r.bottom - r.top;
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
	ForgetTargetDevice();
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
