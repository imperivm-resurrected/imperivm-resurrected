// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "hooks/hookcount.h"
#include "core/config.h"
#include "core/framefade.h"
#include "core/framefreeze.h"
#include "core/framepace.h"
#include "core/imagefit.h"
#include "game/controls.h"
#include "ui/controlsui.h"
#include "core/guard.h"
#include "core/log.h"
#include "hooks/displaymode.h"
#include "game/allycolors.h"
#include "game/seasons.h"
#include "game/autosave.h"
#include "ui/modui.h"
#include "ui/conquestui.h"
#include "ui/mpui.h"
#include "ui/diplomacyui.h"
#include "ui/gamemenuui.h"
#include "ui/leaguedropui.h"
#include "ui/mainmenuui.h"
#include "ui/optionsui.h"
#include "ui/profileui.h"
#include "ui/battlesui.h"
#include "ui/creditsui.h"
#include "ui/chatui.h"
#include "ui/helpui.h"
#include "ui/objectivesui.h"
#include "net/leagueclient.h"
#include "net/netdrop.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "ui/savesui.h"
#include "hooks/media.h"
#include "app/overlay.h"
#include "game/cheats.h"
#include "game/loadcurtain.h"
#include "game/mapfog.h"
#include "app/render.h"
#include "hooks/tweaks.h"

void LogOverlayStatus(const char *when)
{
	HMODULE ov = GetModuleHandleW(L"GameOverlayRenderer.dll");
	LogI("steam", "overlay: (%s) %s", when, ov ? "loaded" : "NOT loaded");
}

namespace {

typedef int(WINAPI *SetDIBitsToDevice_t)(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT,
	const VOID *, const BITMAPINFO *, UINT);
SetDIBitsToDevice_t g_origSetDIBitsToDevice = nullptr;

std::mutex g_frameLock;
std::vector<uint32_t> g_frame;
int g_frameW = 0, g_frameH = 0;
bool g_frameDirty = false;

HWND g_hwnd = nullptr;
HANDLE g_wake = nullptr;
std::atomic<bool> g_threadStarted{false};
std::atomic<bool> g_running{true};
std::atomic<bool> g_failed{false};

uint32_t Expand(uint32_t v, uint32_t mask)
{
	if (!mask) {
		return 0;
	}
	unsigned long shift = 0;
	_BitScanForward(&shift, mask);
	uint32_t m = mask >> shift;
	int bits = 0;
	while (m & 1) {
		bits++;
		m >>= 1;
	}
	uint32_t c = (v & mask) >> shift;
	if (bits >= 8) {
		return c >> (bits - 8);
	}
	const uint32_t max = (1u << bits) - 1;
	return (c * 255 + max / 2) / max;
}

uint32_t g_lut[65536];
uint32_t g_lutMasks[3] = {0, 0, 0};

void BuildLut(uint32_t r, uint32_t g, uint32_t b)
{
	if (g_lutMasks[0] == r && g_lutMasks[1] == g && g_lutMasks[2] == b) {
		return;
	}
	for (uint32_t p = 0; p < 65536; p++) {
		g_lut[p] = 0xff000000u | (Expand(p, r) << 16) | (Expand(p, g) << 8) | Expand(p, b);
	}
	g_lutMasks[0] = r;
	g_lutMasks[1] = g;
	g_lutMasks[2] = b;
	LogD("render", "color format R=%04x G=%04x B=%04x", r, g, b);
}

void BlitToFrame(const BITMAPINFO *bmi, const void *bits, int xDst, int yDst, int w, int h,
	int xSrc, int ySrc)
{
	const BITMAPINFOHEADER &hdr = bmi->bmiHeader;
	const int dibW = hdr.biWidth;
	const int dibH = hdr.biHeight < 0 ? -hdr.biHeight : hdr.biHeight;
	const bool topDown = hdr.biHeight < 0;
	const int stride = ((dibW * 16 + 31) / 32) * 4;

	uint32_t rMask = 0x7c00, gMask = 0x03e0, bMask = 0x001f;
	if (hdr.biCompression == BI_BITFIELDS) {
		const DWORD *masks = reinterpret_cast<const DWORD *>(bmi->bmiColors);
		rMask = masks[0];
		gMask = masks[1];
		bMask = masks[2];
	}
	BuildLut(rMask, gMask, bMask);

	const int topRow = dibH - ySrc - h;
	for (int y = 0; y < h; y++) {
		const int dy = yDst + y;
		const int sy = topRow + y;
		if (dy < 0 || dy >= g_frameH || sy < 0 || sy >= dibH) {
			continue;
		}
		const int memRow = topDown ? sy : (dibH - 1 - sy);
		const uint16_t *src = reinterpret_cast<const uint16_t *>(
			static_cast<const uint8_t *>(bits) + static_cast<size_t>(memRow) * stride);
		uint32_t *dst = &g_frame[static_cast<size_t>(dy) * g_frameW];
		for (int x = 0; x < w; x++) {
			const int dx = xDst + x;
			const int sx = xSrc + x;
			if (dx < 0 || dx >= g_frameW || sx < 0 || sx >= dibW) {
				continue;
			}
			dst[dx] = g_lut[src[sx]];
		}
	}
}

const char kShader[] = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer Params : register(b0) { float4 rect; float4 info; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
	float2 uv = float2(id & 1, id >> 1);
	VSOut o;
	o.pos = float4(rect.x + uv.x * rect.z, rect.y - uv.y * rect.w, 0, 1);
	o.uv = uv;
	return o;
}
float4 PS(VSOut i) : SV_Target {
	float2 uv = i.uv;
	float4 c;
	if (info.z < 0) {
		// Shrinking: one pixel covers up to two texels, so four bilinear taps spread over them average all of
		// them; a single tap would skip some and leave text and edges jagged.
		float2 o = -info.z * 0.25 / info.xy;
		c = (tex.Sample(smp, uv - o) + tex.Sample(smp, uv + o) + tex.Sample(smp, uv + float2(o.x, -o.y)) + tex.Sample(smp, uv + float2(-o.x, o.y))) * 0.25;
	}
	else {
		if (info.z > 0) {
			float2 texel = uv * info.xy;
			float2 base = floor(texel);
			float2 d = texel - base - 0.5;
			float range = 0.5 - 0.5 / info.z;
			uv = (base + (d - clamp(d, -range, range)) * info.z + 0.5) / info.xy;
		}
		c = tex.Sample(smp, uv);
	}
	c.rgb = saturate(c.rgb * info.w);
	return c;
}
)";

const char *FeatureLevelName(D3D_FEATURE_LEVEL level)
{
	switch (level) {
	case D3D_FEATURE_LEVEL_11_1: return "11_1";
	case D3D_FEATURE_LEVEL_11_0: return "11_0";
	case D3D_FEATURE_LEVEL_10_1: return "10_1";
	case D3D_FEATURE_LEVEL_10_0: return "10_0";
	default: return "older";
	}
}

void LogGpu(ID3D11Device *dev, D3D_FEATURE_LEVEL level)
{
	IDXGIDevice *dxgi = nullptr;
	IDXGIAdapter *adapter = nullptr;
	if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
		DXGI_ADAPTER_DESC d = {};
		adapter->GetDesc(&d);
		char name[256] = {};
		WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
		LARGE_INTEGER umd = {};
		char driver[48] = "unknown";
		if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
			sprintf_s(driver, "%u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart), HIWORD(umd.LowPart), LOWORD(umd.LowPart));
		}
		LogI("render", "GPU: %s, %u MB of video memory, driver %s, vendor %04x, feature level %s", name, static_cast<unsigned>(d.DedicatedVideoMemory >> 20), driver, d.VendorId, FeatureLevelName(level));
	}
	if (adapter) adapter->Release();
	if (dxgi) dxgi->Release();
}

// The FPS limit only spaces out the images this thread shows; the game runs and draws at its own pace.
void WaitTicks(int64_t ticks, int64_t freq, HANDLE timer)
{
	if (ticks <= 0) return;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	const int64_t end = now.QuadPart + ticks;
	LARGE_INTEGER due;
	due.QuadPart = -(ticks * 10000000 / freq);
	if (timer && due.QuadPart < 0 && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 100);
	else if (ticks * 1000 / freq > 1) Sleep(static_cast<DWORD>(ticks * 1000 / freq - 1));
	while (QueryPerformanceCounter(&now) && now.QuadPart < end) Sleep(0);
}

template <class T> void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

struct Gpu {
	ID3D11Device *dev = nullptr;
	ID3D11DeviceContext *ctx = nullptr;
	IDXGISwapChain *swap = nullptr;
	ID3D11RenderTargetView *rtv = nullptr;
	ID3D11Texture2D *tex = nullptr;
	ID3D11ShaderResourceView *srv = nullptr;
	ID3D11VertexShader *vs = nullptr;
	ID3D11PixelShader *ps = nullptr;
	ID3D11SamplerState *sampler = nullptr;
	ID3D11Buffer *cb = nullptr;
	int texW = 0, texH = 0, bbW = 0, bbH = 0;
};

bool CreateGpu(Gpu &g)
{
	RECT rc;
	DisplayRealClientRect(g_hwnd, &rc);
	g.bbW = rc.right - rc.left;
	g.bbH = rc.bottom - rc.top;

	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = g.bbW;
	sd.BufferDesc.Height = g.bbH;
	sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &sd, &g.swap, &g.dev, &featureLevel, &g.ctx);
	if (FAILED(hr)) {
		LogE("render", "D3D11CreateDeviceAndSwapChain failed (0x%08x); back to GDI", hr);
		return false;
	}
	IDXGIFactory *factory = nullptr;
	if (SUCCEEDED(g.swap->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory)))) {
		factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
		factory->Release();
	}

	ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *err = nullptr;
	if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, &err)) ||
		FAILED(D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, &err))) {
		LogE("render", "error compiling shaders: %s", err ? static_cast<const char *>(err->GetBufferPointer()) : "?");
		return false;
	}
	g.dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g.vs);
	g.dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g.ps);
	SafeRelease(vsBlob);
	SafeRelease(psBlob);

	D3D11_SAMPLER_DESC smp = {};
	smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	g.dev->CreateSamplerState(&smp, &g.sampler);

	D3D11_BUFFER_DESC cbd = {};
	cbd.ByteWidth = 32;
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	g.dev->CreateBuffer(&cbd, nullptr, &g.cb);

	LogI("render", "Direct3D 11 ready (backbuffer %dx%d)", g.bbW, g.bbH);
	LogGpu(g.dev, featureLevel);
	LogOverlayStatus("at Direct3D creation");
	return true;
}

void FitBackBuffer(Gpu &g)
{
	RECT rc;
	DisplayRealClientRect(g_hwnd, &rc);
	const int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
	if (cw <= 0 || ch <= 0 || (cw == g.bbW && ch == g.bbH && g.rtv)) return;
	SafeRelease(g.rtv);
	if (cw != g.bbW || ch != g.bbH) {
		// DXGI refuses the resize while a view of the back buffer is still bound to the context.
		g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
		const HRESULT hr = g.swap->ResizeBuffers(1, cw, ch, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
		if (SUCCEEDED(hr)) {
			g.bbW = cw;
			g.bbH = ch;
		}
		else {
			// The old buffer stays and Windows stretches it to the window; the next pass tries again.
			static bool logged = false;
			if (!logged) LogW("render", "the back buffer could not be resized to %dx%d (0x%08x)", cw, ch, static_cast<unsigned>(hr));
			logged = true;
		}
	}
	ID3D11Texture2D *back = nullptr;
	if (SUCCEEDED(g.swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back))) && back) {
		g.dev->CreateRenderTargetView(back, nullptr, &g.rtv);
	}
	SafeRelease(back);
}

void FitTexture(Gpu &g, int fw, int fh)
{
	if (fw == g.texW && fh == g.texH && g.tex) return;
	SafeRelease(g.srv);
	SafeRelease(g.tex);
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = fw;
	td.Height = fh;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DYNAMIC;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	g.dev->CreateTexture2D(&td, nullptr, &g.tex);
	g.dev->CreateShaderResourceView(g.tex, nullptr, &g.srv);
	g.texW = fw;
	g.texH = fh;
}

void UploadImage(Gpu &g, const std::vector<uint32_t> &image, int fw, int fh)
{
	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(g.ctx->Map(g.tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
	for (int y = 0; y < fh; y++) {
		memcpy(static_cast<uint8_t *>(map.pData) + static_cast<size_t>(y) * map.RowPitch,
			&image[static_cast<size_t>(y) * fw], static_cast<size_t>(fw) * 4);
	}
	g.ctx->Unmap(g.tex, 0);
}

// The cursor is mapped with the same scale and margins (displaymode.cpp), so the two must not differ.
void PlaceImage(Gpu &g, int fw, int fh)
{
	const int scaling = GetConfig().imageScaling;
	const float s = ImageScale(g.bbW, g.bbH, fw, fh, scaling);
	const float w = fw * s / g.bbW * 2.0f, h = fh * s / g.bbH * 2.0f;
	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(g.ctx->Map(g.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
	float *r = static_cast<float *>(map.pData);
	r[0] = ImageMargin(g.bbW, fw, s) * 2.0f / g.bbW - 1.0f;
	r[1] = 1.0f - ImageMargin(g.bbH, fh, s) * 2.0f / g.bbH;
	r[2] = w;
	r[3] = h;
	r[4] = static_cast<float>(fw);
	r[5] = static_cast<float>(fh);
	r[6] = ImageSharpness(s, scaling);
	r[7] = GetConfig().brightness / 100.0f;
	g.ctx->Unmap(g.cb, 0);
}

void DrawImage(Gpu &g)
{
	if (!g.rtv) return;
	const float black[4] = {0, 0, 0, 1};
	g.ctx->ClearRenderTargetView(g.rtv, black);
	D3D11_VIEWPORT vp = {0, 0, static_cast<float>(g.bbW), static_cast<float>(g.bbH), 0, 1};
	g.ctx->RSSetViewports(1, &vp);
	g.ctx->OMSetRenderTargets(1, &g.rtv, nullptr);
	g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	g.ctx->VSSetShader(g.vs, nullptr, 0);
	g.ctx->VSSetConstantBuffers(0, 1, &g.cb);
	g.ctx->PSSetConstantBuffers(0, 1, &g.cb);
	g.ctx->PSSetShader(g.ps, nullptr, 0);
	g.ctx->PSSetShaderResources(0, 1, &g.srv);
	g.ctx->PSSetSamplers(0, 1, &g.sampler);
	g.ctx->Draw(4, 0);
}

void CopyGameFrame(std::vector<uint32_t> &image, int &fw, int &fh)
{
	{
		std::lock_guard<std::mutex> lock(g_frameLock);
		fw = g_frameW;
		fh = g_frameH;
		image = g_frame;
	}
	ControlsDrawBarKeys(image.data(), fw, fh);
}

// The old picture, weighed by alpha (0..256), over the new one.
void BlendOver(std::vector<uint32_t> &image, const std::vector<uint32_t> &old, int alpha)
{
	const uint32_t a = static_cast<uint32_t>(alpha), b = 256 - a;
	for (size_t i = 0; i < image.size() && i < old.size(); i++) {
		const uint32_t n = image[i], o = old[i];
		const uint32_t rb = (((o & 0xff00ff) * a + (n & 0xff00ff) * b) >> 8) & 0xff00ff;
		const uint32_t g = (((o & 0x00ff00) * a + (n & 0x00ff00) * b) >> 8) & 0x00ff00;
		image[i] = 0xff000000u | rb | g;
	}
}

// A cross-fade asked for (core/framefade.h): the picture of the moment of the hold, kept while the game changes and then faded out.
struct Fade {
	unsigned seq = 0;
	std::vector<uint32_t> picture;
	int w = 0, h = 0;

	// The old picture's weight now, 0 when there is none.
	int Alpha(DWORD now)
	{
		const FrameFade f = FrameFadeState();
		if (f.seq != seq) {
			seq = f.seq;
			CopyGameFrame(picture, w, h);
			FrameFadeMarkTaken(f.seq);
		}
		if (picture.empty()) return 0;
		const DWORD passed = now - static_cast<DWORD>(f.start);
		int alpha = 0;
		if (f.holding) alpha = passed < kFrameFadeHoldMs ? 256 : 0;
		else if (f.ms && passed < f.ms) alpha = 256 - static_cast<int>(passed * 256 / f.ms);
		if (alpha <= 0) picture.clear();
		return alpha;
	}
};

// A video frame, the game's image when it changed or a video just ended, or the same image again when the letters of the
// command bar changed. False when there is nothing new.
bool TakeNewFrame(bool video, bool wasVideo, std::vector<uint32_t> &image, int &fw, int &fh)
{
	if (video) {
		int vw = 0, vh = 0;
		if (!MediaTakeVideoFrame(image, vw, vh)) return false;
		fw = vw;
		fh = vh;
		return true;
	}
	bool taken = false;
	{
		std::lock_guard<std::mutex> lock(g_frameLock);
		if (g_frameDirty || wasVideo) {
			fw = g_frameW;
			fh = g_frameH;
			image = g_frame;
			g_frameDirty = false;
			taken = true;
		}
	}
	if (!taken && ControlsBarKeysChanged()) {
		std::lock_guard<std::mutex> lock(g_frameLock);
		if (g_frameW > 0 && !g_frame.empty()) {
			fw = g_frameW;
			fh = g_frameH;
			image = g_frame;
			taken = true;
		}
	}
	if (taken) ControlsDrawBarKeys(image.data(), fw, fh);
	return taken;
}

struct UiNeeds {
	bool saves, mods, mp, controls, options, gameMenu, drop, diplomacy, conquest, profile, battles, credits, chat, help, objectives, mainMenu;
	// Something moves on its own and needs every pass, not the 30 a second of a still screen.
	bool animating = false;

	bool Any() const
	{
		return saves || mods || mp || controls || options || gameMenu || drop || diplomacy || conquest || profile || battles || credits || chat ||
			help || objectives || mainMenu;
	}
};

UiNeeds QueryUiNeeds()
{
	UiNeeds n;
	n.saves = SavesUiNeedsDraw();
	n.mods = ModUiNeedsDraw();
	n.mp = MpUiNeedsDraw();
	n.controls = ControlsUiNeedsDraw();
	n.options = OptionsUiNeedsDraw();
	n.gameMenu = GameMenuUiNeedsDraw();
	n.drop = LeagueDropUiNeedsDraw();
	n.diplomacy = DiplomacyUiNeedsDraw();
	n.conquest = ConquestUiNeedsDraw();
	n.profile = ProfileUiNeedsDraw();
	n.battles = BattlesUiNeedsDraw();
	n.credits = CreditsUiNeedsDraw();
	n.chat = ChatUiNeedsDraw();
	n.help = HelpUiNeedsDraw();
	n.objectives = ObjectivesUiNeedsDraw();
	n.mainMenu = MainMenuUiNeedsDraw();
	n.animating = n.mainMenu && MainMenuUiAnimating();
	return n;
}

void DrawUi(const UiNeeds &n, uint32_t *buf, int w, int h)
{
	Guard("interface drawing", [&] {
		if (n.mainMenu) MainMenuUiDraw(buf, w, h);
		if (n.chat) ChatUiDraw(buf, w, h);
		if (n.conquest) ConquestUiDraw(buf, w, h);
		if (n.battles) BattlesUiDraw(buf, w, h);
		MpUiDraw(buf, w, h);
		SavesUiDraw(buf, w, h);
		if (n.options) OptionsUiDraw(buf, w, h);
		if (n.diplomacy) DiplomacyUiDraw(buf, w, h);
		if (n.drop) LeagueDropUiDraw(buf, w, h);
		if (n.gameMenu) GameMenuUiDraw(buf, w, h);
		if (n.profile) ProfileUiDraw(buf, w, h);
		if (n.credits) CreditsUiDraw(buf, w, h);
		if (n.help) HelpUiDraw(buf, w, h);
		if (n.objectives) ObjectivesUiDraw(buf, w, h);
		ModUiDraw(buf, w, h);
		ControlsUiDraw(buf, w, h);
	});
}

DWORD WINAPI RenderThread(LPVOID)
{
	Gpu gpu;
	if (!CreateGpu(gpu)) {
		g_failed = true;
		return 0;
	}

	// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION: Windows 10 1803 and later; older ones fall back to Sleep.
	HANDLE paceTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
	LARGE_INTEGER paceFreq;
	QueryPerformanceFrequency(&paceFreq);
	int64_t paceDeadline = 0;
	std::vector<uint32_t> local;
	bool haveFrame = false;
	int fw = 0, fh = 0;
	int fps = 0, framesThisSecond = 0;
	DWORD secondStart = GetTickCount();
	bool overlayWasOn = false;
	bool uiWasOn = false;
	DWORD lastUiDraw = 0;
	bool movieBlack = false;
	bool wasVideo = false;
	while (g_running) {
		const bool vsync = GetConfig().vsync;
		if (!vsync) {
			WaitForSingleObject(g_wake, 16);
		}
		if (!g_running) {
			break;
		}
		const bool video = MediaVideoActive();
		bool newFrame = TakeNewFrame(video, wasVideo, local, fw, fh);
		wasVideo = video;
		if (MediaMovieShowing()) {
			if (!movieBlack) {
				// A movie can already be on screen on the first pass, before the target exists.
				FitBackBuffer(gpu);
				if (gpu.rtv) {
					const float black[4] = {0, 0, 0, 1};
					gpu.ctx->ClearRenderTargetView(gpu.rtv, black);
					gpu.swap->Present(0, 0);
				}
				movieBlack = true;
			}
			Sleep(15);
			continue;
		}
		movieBlack = false;
		if (!video && LoadCurtainUp()) {
			Sleep(5);
			continue;
		}
		if (!newFrame && !haveFrame) {
			Sleep(5);
			continue;
		}
		haveFrame = true;

		if (newFrame) {
			framesThisSecond++;
		}
		DWORD now = GetTickCount();
		static Fade fade;
		const int fadeAlpha = video ? 0 : fade.Alpha(now);
		if (fadeAlpha > 0) {
			if (!newFrame) CopyGameFrame(local, fw, fh);
			if (fade.w == fw && fade.h == fh) BlendOver(local, fade.picture, fadeAlpha);
			newFrame = true;
		}
		bool overlayTick = false;
		if (now - secondStart >= 1000) {
			fps = framesThisSecond;
			framesThisSecond = 0;
			secondStart = now;
			overlayTick = true;
		}
		const bool overlay = GetConfig().showPerf || GetConfig().showNet;
		const UiNeeds needs = QueryUiNeeds();
		if (newFrame && !needs.mp) MpUiObserveFrame(local.data(), fw, fh);
		const bool ui = !video && needs.Any();
		// The credits and the sliding submenu move on their own: they are drawn on every pass, at the pace of the screen.
		if (ui && (newFrame || needs.credits || needs.animating || now - lastUiDraw >= 33)) {
			if (!newFrame) CopyGameFrame(local, fw, fh);
			DrawUi(needs, local.data(), fw, fh);
			if (overlay) {
				DrawPerfOverlay(local.data(), fw, fh, fps, gpu.bbW, gpu.bbH);
			}
			lastUiDraw = now;
			newFrame = true;
		}
		else if (!video && !ui && overlay && (newFrame || overlayTick || uiWasOn || OverlayNeedsRedraw())) {
			if (!newFrame) CopyGameFrame(local, fw, fh);
			DrawPerfOverlay(local.data(), fw, fh, fps, gpu.bbW, gpu.bbH);
			newFrame = true;
		}
		else if (!video && !ui && ((!overlay && overlayWasOn) || uiWasOn)) {
			CopyGameFrame(local, fw, fh);
			newFrame = true;
		}
		overlayWasOn = overlay;
		uiWasOn = ui;

		FitBackBuffer(gpu);
		FitTexture(gpu, fw, fh);
		if (newFrame) UploadImage(gpu, local, fw, fh);
		PlaceImage(gpu, fw, fh);
		DrawImage(gpu);
		// With vertical sync the monitor sets the pace and the limit is left out.
		const int limit = vsync ? 0 : GetConfig().fpsLimit;
		if (limit > 0) {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			WaitTicks(FramePaceWait(paceDeadline, now.QuadPart, FrameInterval(paceFreq.QuadPart, limit)), paceFreq.QuadPart, paceTimer);
		}
		else {
			paceDeadline = 0;
		}
		gpu.swap->Present(vsync ? 1 : 0, 0);
	}
	if (paceTimer) CloseHandle(paceTimer);
	return 0;
}

void GameThreadTicks()
{
	AutosaveTick();
	MediaFrameTick();
	TweaksFrameTick();
	CheatsFrameTick();
	MpUiGameThreadTick();
	OptionsUiGameTick();
	GameMenuUiGameTick();
	LeagueDropUiGameTick();
	MainMenuUiGameTick();
	AllyColorsGameTick();
	SeasonsGameTick();
	DiplomacyUiGameTick();
	ConquestUiGameTick();
	ProfileUiGameTick();
	BattlesUiGameTick();
	CreditsUiGameTick();
	ChatUiGameTick();
	HelpUiGameTick();
	ObjectivesUiGameTick();
	RejoinGameThreadTick();
	SpectatorGameThreadTick();
	ZoomFogNoteImage();
	LeagueGameThreadTick();
}

int WINAPI HookSetDIBitsToDevice(HDC hdc, int xDest, int yDest, DWORD w, DWORD h, int xSrc, int ySrc,
	UINT startScan, UINT cLines, const VOID *bits, const BITMAPINFO *bmi, UINT colorUse)
{
	GameThreadTicks();
	HWND hwnd = WindowFromDC(hdc);
	const BITMAPINFOHEADER &hdr = bmi->bmiHeader;
	bool usable = !g_failed && hwnd && hdr.biBitCount == 16 &&
		(hdr.biCompression == BI_RGB || hdr.biCompression == BI_BITFIELDS) && startScan == 0;

	if (!usable) {
		return g_origSetDIBitsToDevice(hdc, xDest, yDest, w, h, xSrc, ySrc, startScan, cLines, bits, bmi, colorUse);
	}

	DisplayAttachWindow(hwnd);
	int cw = 0, ch = 0;
	DisplayGetLogicalSize(hwnd, cw, ch);
	{
		std::lock_guard<std::mutex> lock(g_frameLock);
		if (cw > 0 && ch > 0 && (cw != g_frameW || ch != g_frameH)) {
			g_frameW = cw;
			g_frameH = ch;
			g_frame.assign(static_cast<size_t>(cw) * ch, 0xff000000u);
		}
		const bool frozen = FrameFrozen();
		if (!frozen) BlitToFrame(bmi, bits, xDest, yDest, static_cast<int>(w), static_cast<int>(h), xSrc, ySrc);
		g_frameDirty = true;
	}
	OverlayNoteFrame();

	if (!g_threadStarted.exchange(true)) {
		g_hwnd = hwnd;
		g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		CloseHandle(CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr));
	}
	SetEvent(g_wake);
	return static_cast<int>(h);
}

}

bool InstallRenderHooks()
{
	if (!GetConfig().renderEnabled) {
		LogI("render", "disabled in the configuration");
		return true;
	}
	HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
	void *target = gdi ? reinterpret_cast<void *>(GetProcAddress(gdi, "SetDIBitsToDevice")) : nullptr;
	if (!target || !HookInstall(target, &HookSetDIBitsToDevice, &g_origSetDIBitsToDevice)) {
		LogE("render", "could not intercept SetDIBitsToDevice");
		return false;
	}
	LogI("render", "SetDIBitsToDevice intercepted");
	return true;
}

void ShutdownRender()
{
	g_running = false;
	if (g_wake) {
		SetEvent(g_wake);
	}
}
