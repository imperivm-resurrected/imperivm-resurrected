// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <dshow.h>

#include <algorithm>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "hooks/dmshim.h"
#include "hooks/displaymode.h"
#include "core/log.h"
#include "hooks/media.h"
#include "addr/state.h"
#include "addr/media.h"

namespace {
typedef HRESULT(WINAPI *CoCreateInstance_t)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
typedef HRESULT(STDMETHODCALLTYPE *Run_t)(IMediaControl *self);
typedef HRESULT(STDMETHODCALLTYPE *OpenFile_t)(void *self, LPCWSTR file, DWORD flags);

CoCreateInstance_t g_origCoCreate = nullptr;
Run_t g_origRun = nullptr;
bool g_runHooked = false;
OpenFile_t g_origOpenFile = nullptr;
bool g_openHooked = false;
std::mutex g_dirsLock;
std::vector<std::wstring> g_modDirs;
std::set<std::wstring> g_reportedMovies;
std::atomic<HWND> g_movieGame{nullptr};
std::atomic<DWORD> g_movieSince{0};

long MovieVolume()
{
	const int pct = GetConfig().movieMute ? 0 : GetConfig().movieVolume;
	if (pct <= 0) return -10000;
	return std::max(-10000L, static_cast<long>(2000.0 * std::log10(pct / 100.0)));
}

RECT GameScreenRect(HWND game)
{
	RECT r = {};
	if (GetConfig().windowed) {
		DisplayRealClientRect(game, &r);
		POINT tl = {0, 0};
		ClientToScreen(game, &tl);
		OffsetRect(&r, tl.x, tl.y);
		return r;
	}
	MONITORINFO mi = {sizeof(mi)};
	GetMonitorInfoA(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &mi);
	return mi.rcMonitor;
}

void FitVideo(IMediaControl *control)
{

	IVideoWindow *window = nullptr;
	IBasicVideo *video = nullptr;
	if (SUCCEEDED(control->QueryInterface(IID_IVideoWindow, reinterpret_cast<void **>(&window))) &&
		SUCCEEDED(control->QueryInterface(IID_IBasicVideo, reinterpret_cast<void **>(&video)))) {
		OAHWND owner = 0;
		long vw = 0, vh = 0;
		if (SUCCEEDED(window->get_Owner(&owner)) && owner && SUCCEEDED(video->GetVideoSize(&vw, &vh)) && vw > 0 && vh > 0) {
			const HWND game = reinterpret_cast<HWND>(owner);
			const RECT area = GameScreenRect(game);
			const long cw = area.right - area.left, ch = area.bottom - area.top;
			long w = cw, h = cw * vh / vw;
			if (h > ch) {
				h = ch;
				w = ch * vw / vh;
			}
			window->put_Owner(0);
			window->put_WindowStyle(WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
			window->put_WindowStyleEx(WS_EX_TOPMOST);
			window->put_MessageDrain(owner);
			window->put_BorderColor(RGB(0, 0, 0));
			window->SetWindowPosition(area.left, area.top, cw, ch);
			video->SetDestinationPosition((cw - w) / 2, (ch - h) / 2, w, h);
			g_movieGame = game;
			g_movieSince = GetTickCount();
			LogD("video", "%ldx%ld in its window (%ldx%ld inside %ldx%ld)", vw, vh, w, h, cw, ch);
		}
	}
	if (video) video->Release();
	if (window) window->Release();
	IBasicAudio *audio = nullptr;
	if (SUCCEEDED(control->QueryInterface(IID_IBasicAudio, reinterpret_cast<void **>(&audio)))) {
		audio->put_Volume(MovieVolume());
		audio->Release();
	}
}

MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE SampleCB(double time, IMediaSample *sample) = 0;
	virtual HRESULT STDMETHODCALLTYPE BufferCB(double time, BYTE *buffer, long len) = 0;
};
MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL oneShot) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *type) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *type) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL buffer) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *size, long *buffer) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **sample) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB *cb, long which) = 0;
};
const CLSID kClsidSampleGrabber = {0xC1F400A0, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
const CLSID kClsidNullRenderer = {0xC1F400A4, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};

std::mutex g_videoLock;
std::vector<uint32_t> g_videoFrame;
int g_videoW = 0, g_videoH = 0;
DWORD g_videoTime = 0;
bool g_videoNew = false;

struct FrameGrabber : ISampleGrabberCB {
	int w = 0, h = 0;
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
	{
		if (iid == IID_IUnknown || iid == __uuidof(ISampleGrabberCB)) {
			*out = static_cast<ISampleGrabberCB *>(this);
			return S_OK;
		}
		*out = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
	ULONG STDMETHODCALLTYPE Release() override { return 1; }
	HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample *) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE *buffer, long len) override
	{
		if (w <= 0 || h <= 0 || len < w * h * 4) return S_OK;
		std::lock_guard<std::mutex> lock(g_videoLock);
		g_videoFrame.resize(static_cast<size_t>(w) * h);
		const uint32_t *src = reinterpret_cast<const uint32_t *>(buffer);
		for (int y = 0; y < h; y++) {
			memcpy(&g_videoFrame[static_cast<size_t>(y) * w], src + static_cast<size_t>(h - 1 - y) * w, static_cast<size_t>(w) * 4);
		}
		g_videoW = w;
		g_videoH = h;
		g_videoTime = GetTickCount();
		g_videoNew = true;
		return S_OK;
	}
};
FrameGrabber g_grabber;

IPin *FirstPin(IBaseFilter *f, PIN_DIRECTION want)
{
	IEnumPins *pins = nullptr;
	IPin *found = nullptr;
	if (SUCCEEDED(f->EnumPins(&pins))) {
		IPin *p = nullptr;
		while (!found && pins->Next(1, &p, nullptr) == S_OK) {
			PIN_DIRECTION dir;
			if (SUCCEEDED(p->QueryDirection(&dir)) && dir == want) found = p;
			else p->Release();
		}
		pins->Release();
	}
	return found;
}

bool ReplaceVideoRenderer(IMediaControl *control)
{
	IGraphBuilder *graph = nullptr;
	if (FAILED(control->QueryInterface(IID_IGraphBuilder, reinterpret_cast<void **>(&graph)))) return false;
	bool ok = false;
	IBaseFilter *renderer = nullptr;
	IEnumFilters *filters = nullptr;
	if (SUCCEEDED(graph->EnumFilters(&filters))) {
		IBaseFilter *f = nullptr;
		while (!renderer && filters->Next(1, &f, nullptr) == S_OK) {
			IVideoWindow *vw = nullptr;
			if (SUCCEEDED(f->QueryInterface(IID_IVideoWindow, reinterpret_cast<void **>(&vw)))) {
				vw->Release();
				renderer = f;
			}
			else {
				f->Release();
			}
		}
		filters->Release();
	}
	IPin *upstream = nullptr;
	if (renderer) {
		IPin *in = FirstPin(renderer, PINDIR_INPUT);
		if (in) {
			in->ConnectedTo(&upstream);
			in->Release();
		}
	}
	IBaseFilter *grabberFilter = nullptr, *nullRenderer = nullptr;
	ISampleGrabber *grabber = nullptr;
	if (upstream &&
		SUCCEEDED(CoCreateInstance(kClsidSampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
			reinterpret_cast<void **>(&grabberFilter))) &&
		SUCCEEDED(CoCreateInstance(kClsidNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter,
			reinterpret_cast<void **>(&nullRenderer))) &&
		SUCCEEDED(grabberFilter->QueryInterface(__uuidof(ISampleGrabber), reinterpret_cast<void **>(&grabber)))) {
		AM_MEDIA_TYPE mt = {};
		mt.majortype = MEDIATYPE_Video;
		mt.subtype = MEDIASUBTYPE_RGB32;
		mt.formattype = FORMAT_VideoInfo;
		grabber->SetMediaType(&mt);
		const HRESULT removed = graph->RemoveFilter(renderer);
		if (SUCCEEDED(removed) && SUCCEEDED(graph->AddFilter(grabberFilter, L"Imperivm grabber")) &&
			SUCCEEDED(graph->AddFilter(nullRenderer, L"Imperivm null"))) {
			IPin *gin = FirstPin(grabberFilter, PINDIR_INPUT), *gout = FirstPin(grabberFilter, PINDIR_OUTPUT);
			IPin *nin = FirstPin(nullRenderer, PINDIR_INPUT);
			if (gin && gout && nin && SUCCEEDED(graph->Connect(upstream, gin)) && SUCCEEDED(graph->Connect(gout, nin))) {
				AM_MEDIA_TYPE got = {};
				if (SUCCEEDED(grabber->GetConnectedMediaType(&got)) && got.formattype == FORMAT_VideoInfo && got.pbFormat) {
					const VIDEOINFOHEADER *vih = reinterpret_cast<const VIDEOINFOHEADER *>(got.pbFormat);
					g_grabber.w = vih->bmiHeader.biWidth;
					g_grabber.h = std::abs(vih->bmiHeader.biHeight);
				}
				if (got.pbFormat) CoTaskMemFree(got.pbFormat);
				if (got.pUnk) got.pUnk->Release();
				grabber->SetBufferSamples(FALSE);
				grabber->SetOneShot(FALSE);
				grabber->SetCallback(&g_grabber, 1);
				ok = g_grabber.w > 0 && g_grabber.h > 0;
			}
			if (gin) gin->Release();
			if (gout) gout->Release();
			if (nin) nin->Release();
		}
		LogAt(ok ? kLogDebug : kLogWarn, "video", "Windows renderer %s (%dx%d, remove=0x%08lx)", ok ? "replaced by the mod's" : "NOT replaced",
			g_grabber.w, g_grabber.h, static_cast<unsigned long>(removed));
	}
	if (grabber) grabber->Release();
	if (grabberFilter) grabberFilter->Release();
	if (nullRenderer) nullRenderer->Release();
	if (upstream) upstream->Release();
	if (renderer) renderer->Release();
	graph->Release();
	return ok;
}

HRESULT STDMETHODCALLTYPE HookRun(IMediaControl *self)
{
	if (!ReplaceVideoRenderer(self)) FitVideo(self);
	IBasicAudio *audio = nullptr;
	if (SUCCEEDED(self->QueryInterface(IID_IBasicAudio, reinterpret_cast<void **>(&audio)))) {
		audio->put_Volume(MovieVolume());
		audio->Release();
	}
	return g_origRun(self);
}

const CLSID kClsidAMMultiMediaStream = {0x49c47ce5, 0x9ba4, 0x11d0, {0x82, 0x12, 0x00, 0xc0, 0x4f, 0xc3, 0x2c, 0x45}};
const IID kIidAMMultiMediaStream = {0xbebe595c, 0x9a6f, 0x11d0, {0x8f, 0xde, 0x00, 0xc0, 0x4f, 0xd9, 0x18, 0x9d}};
const unsigned kOpenFileSlot = 16;

std::string Narrow(const std::wstring &w)
{
	if (w.empty()) return "";
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
	return out;
}

bool IsRelative(const wchar_t *p)
{
	return p[0] && p[0] != L'\\' && p[0] != L'/' && !(p[1] == L':');
}

HRESULT STDMETHODCALLTYPE HookOpenFile(void *self, LPCWSTR file, DWORD flags)
{
	if (file && IsRelative(file)) {
		std::wstring rel = file;
		std::replace(rel.begin(), rel.end(), L'/', L'\\');
		std::vector<std::wstring> dirs;
		{
			std::lock_guard<std::mutex> lock(g_dirsLock);
			dirs = g_modDirs;
		}
		for (const std::wstring &dir : dirs) {
			const std::wstring full = dir + L"\\" + rel;
			const DWORD a = GetFileAttributesW(full.c_str());
			if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) continue;
			bool first = false;
			{
				std::lock_guard<std::mutex> lock(g_dirsLock);
				first = g_reportedMovies.insert(rel).second;
			}
			if (first) LogI("video", "%s from %s", Narrow(rel).c_str(), Narrow(dir).c_str());
			return g_origOpenFile(self, full.c_str(), flags);
		}
	}
	return g_origOpenFile(self, file, flags);
}

void HookOpenFileOf(IUnknown *created)
{
	if (g_openHooked || !created) return;
	g_openHooked = true;
	void *stream = nullptr;
	if (FAILED(created->QueryInterface(kIidAMMultiMediaStream, &stream)) || !stream) {
		LogW("video", "the cutscene player has no IAMMultiMediaStream: cutscenes of content packs are not found");
		return;
	}
	void *open = (*reinterpret_cast<void ***>(stream))[kOpenFileSlot];
	if (HookCreate(open, reinterpret_cast<void *>(&HookOpenFile), reinterpret_cast<void **>(&g_origOpenFile)) == MH_OK && HookEnable(open) == MH_OK) {
		LogD("video", "cutscenes are looked up in the active content packs first");
	}
	else {
		LogW("video", "could not hook the cutscene player: cutscenes of content packs are not found");
	}
	static_cast<IUnknown *>(stream)->Release();
}

void HookGraphRun()
{
	if (g_runHooked) return;
	g_runHooked = true;
	IMediaControl *control = nullptr;
	if (FAILED(g_origCoCreate(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IMediaControl,
			reinterpret_cast<void **>(&control))) ||
		!control) {
		LogE("video", "could not create a DirectShow graph to hook it");
		return;
	}
	void *run = (*reinterpret_cast<void ***>(control))[7];
	if (HookCreate(run, reinterpret_cast<void *>(&HookRun), reinterpret_cast<void **>(&g_origRun)) == MH_OK &&
		HookEnable(run) == MH_OK) {
		LogI("video", "player hooked (full screen and volume)");
	}
	control->Release();
}

HRESULT WINAPI HookCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID *out)
{
	if (out && DmShimCreate(clsid, out)) return S_OK;
	if (!g_runHooked && (IsEqualCLSID(clsid, kClsidAMMultiMediaStream) || IsEqualCLSID(clsid, CLSID_FilterGraph))) {
		HookGraphRun();
	}
	const HRESULT hr = g_origCoCreate(clsid, outer, ctx, iid, out);
	if (SUCCEEDED(hr) && out && *out && !outer && IsEqualCLSID(clsid, kClsidAMMultiMediaStream)) HookOpenFileOf(static_cast<IUnknown *>(*out));
	return hr;
}

const unsigned kIsPausedSlot = 22;
const unsigned short kTypeMusic = 1, kTypeUI = 6;

typedef void(__fastcall *SetVolume_t)(void *channel, void *edx, long volume);
typedef void(__fastcall *ThisOnly_t)(void *self);
typedef int(__fastcall *IsPaused_t)(void *game);

bool g_muted = false;
std::vector<void *> g_pausedPaths;
bool g_musicMuted = false;
std::vector<void *> g_pausedMusic;

}

void MediaInstall(bool knownExe)
{
	HMODULE ole = GetModuleHandleA("ole32.dll");
	if (!ole) ole = LoadLibraryA("ole32.dll");
	void *target = ole ? reinterpret_cast<void *>(GetProcAddress(ole, "CoCreateInstance")) : nullptr;
	if (target && HookCreate(target, reinterpret_cast<void *>(&HookCoCreateInstance), reinterpret_cast<void **>(&g_origCoCreate)) ==
					  MH_OK &&
		HookEnable(target) == MH_OK) {
		LogI("video", "ready to adjust the cutscenes");
	}
	else {
		LogE("video", "could not intercept CoCreateInstance (no videos or modern audio)");
	}
	(void)knownExe;
}

void MediaSetModDirs(const std::vector<std::string> &dirs)
{
	std::vector<std::wstring> wide;
	for (const std::string &d : dirs) {
		const int n = MultiByteToWideChar(CP_ACP, 0, d.c_str(), -1, nullptr, 0);
		if (n <= 1) continue;
		std::wstring w(n - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, d.c_str(), -1, &w[0], n);
		wide.push_back(w);
	}
	std::lock_guard<std::mutex> lock(g_dirsLock);
	g_modDirs = wide;
}

void MediaFrameTick()
{
	char *mgr = *reinterpret_cast<char **>(addr::kSoundManager);
	void *game = *reinterpret_cast<void **>(addr::kGame);
	if (!mgr) return;
	const bool paused = game && reinterpret_cast<IsPaused_t>((*reinterpret_cast<void ***>(game))[kIsPausedSlot])(game) != 0;
	if (paused && !g_muted) {
		int count = 0;
		bool realPause = true;
		for (int i = 0; i < 0x40; i++) {
			char *ch = *reinterpret_cast<char **>(mgr + 0x18 + i * 4);
			if (!ch || !*reinterpret_cast<void **>(ch + 8)) continue;
			const unsigned short type = *reinterpret_cast<unsigned short *>(ch + 0xc);
			if (type == kTypeMusic || type == kTypeUI) continue;
			void *path = *reinterpret_cast<void **>(ch + 4);
			if (DmShimPausePath(path, true)) {
				g_pausedPaths.push_back(path);
			}
			else {
				reinterpret_cast<SetVolume_t>(addr::kChannelSetVolume)(ch, nullptr, -9600);
				realPause = false;
			}
			count++;
		}
		g_muted = true;
		LogD("game", "pause: %d sounds %s", count, realPause ? "paused" : "muted");
	}
	else if (!paused && g_muted) {
		for (void *path : g_pausedPaths) DmShimPausePath(path, false);
		g_pausedPaths.clear();
		reinterpret_cast<ThisOnly_t>(addr::kUpdateChannelVolumes)(mgr);
		g_muted = false;
	}
}

void MediaPauseMusic(bool pause)
{
	char *mgr = *reinterpret_cast<char **>(addr::kSoundManager);
	if (!mgr || pause == g_musicMuted) return;
	if (pause) {
		for (int i = 0; i < 0x40; i++) {
			char *ch = *reinterpret_cast<char **>(mgr + 0x18 + i * 4);
			if (!ch || !*reinterpret_cast<void **>(ch + 8) || *reinterpret_cast<unsigned short *>(ch + 0xc) != kTypeMusic) continue;
			void *path = *reinterpret_cast<void **>(ch + 4);
			if (DmShimPausePath(path, true)) g_pausedMusic.push_back(path);
			else reinterpret_cast<SetVolume_t>(addr::kChannelSetVolume)(ch, nullptr, -9600);
		}
	}
	else {
		for (void *path : g_pausedMusic) DmShimPausePath(path, false);
		g_pausedMusic.clear();
		reinterpret_cast<ThisOnly_t>(addr::kUpdateChannelVolumes)(mgr);
	}
	g_musicMuted = pause;
}

bool MediaMovieShowing()
{
	if (!g_movieGame) return false;
	struct Find {
		static BOOL CALLBACK Proc(HWND hwnd, LPARAM lp)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			char cls[32] = {};
			GetClassNameA(hwnd, cls, sizeof(cls));
			if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && strcmp(cls, "VideoRenderer") == 0) {
				*reinterpret_cast<bool *>(lp) = true;
				return FALSE;
			}
			return TRUE;
		}
	};
	bool found = false;
	EnumWindows(&Find::Proc, reinterpret_cast<LPARAM>(&found));
	if (!found && GetTickCount() - g_movieSince > 2000) g_movieGame = nullptr;
	return found;
}

bool MediaVideoActive()
{
	std::lock_guard<std::mutex> lock(g_videoLock);
	return g_videoTime && GetTickCount() - g_videoTime < 400;
}

bool MediaTakeVideoFrame(std::vector<uint32_t> &out, int &w, int &h)
{
	std::lock_guard<std::mutex> lock(g_videoLock);
	if (!g_videoNew) return false;
	g_videoNew = false;
	out = g_videoFrame;
	w = g_videoW;
	h = g_videoH;
	return true;
}
