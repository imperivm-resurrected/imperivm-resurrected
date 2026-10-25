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

#include "MinHook.h"
#include "hooks/hookcount.h"
#include "core/config.h"
#include "core/framefreeze.h"
#include "game/controls.h"
#include "ui/controlsui.h"
#include "core/guard.h"
#include "core/log.h"
#include "hooks/displaymode.h"
#include "game/autosave.h"
#include "ui/modui.h"
#include "ui/mpui.h"
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

template <class T> void SafeRelease(T *&p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

DWORD WINAPI RenderThread(LPVOID)
{
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
	std::vector<uint32_t> local;

	RECT rc;
	DisplayRealClientRect(g_hwnd, &rc);
	bbW = rc.right - rc.left;
	bbH = rc.bottom - rc.top;

	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = bbW;
	sd.BufferDesc.Height = bbH;
	sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &sd, &swap, &dev, &featureLevel, &ctx);
	if (FAILED(hr)) {
		LogE("render", "D3D11CreateDeviceAndSwapChain failed (0x%08x); back to GDI", hr);
		g_failed = true;
		return 0;
	}
	IDXGIFactory *factory = nullptr;
	if (SUCCEEDED(swap->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory)))) {
		factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
		factory->Release();
	}

	ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *err = nullptr;
	if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, &err)) ||
		FAILED(D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, &err))) {
		LogE("render", "error compiling shaders: %s", err ? static_cast<const char *>(err->GetBufferPointer()) : "?");
		g_failed = true;
		return 0;
	}
	dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
	dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
	SafeRelease(vsBlob);
	SafeRelease(psBlob);

	D3D11_SAMPLER_DESC smp = {};
	smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	dev->CreateSamplerState(&smp, &sampler);

	D3D11_BUFFER_DESC cbd = {};
	cbd.ByteWidth = 32;
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	dev->CreateBuffer(&cbd, nullptr, &cb);

	LogI("render", "Direct3D 11 ready (backbuffer %dx%d)", bbW, bbH);
	LogGpu(dev, featureLevel);
	LogOverlayStatus("at Direct3D creation");

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
		bool newFrame = false;
		const bool video = MediaVideoActive();
		if (video) {
			int vw = 0, vh = 0;
			if (MediaTakeVideoFrame(local, vw, vh)) {
				fw = vw;
				fh = vh;
				newFrame = true;
			}
		}
		else {
			{
				std::lock_guard<std::mutex> lock(g_frameLock);
				if (g_frameDirty || wasVideo) {
					fw = g_frameW;
					fh = g_frameH;
					local = g_frame;
					g_frameDirty = false;
					newFrame = true;
				}
			}
			if (newFrame) ControlsDrawBarKeys(local.data(), fw, fh);
			else if (ControlsBarKeysChanged()) {
				{
					std::lock_guard<std::mutex> lock(g_frameLock);
					if (g_frameW > 0 && !g_frame.empty()) {
						fw = g_frameW;
						fh = g_frameH;
						local = g_frame;
						newFrame = true;
					}
				}
				if (newFrame) ControlsDrawBarKeys(local.data(), fw, fh);
			}
		}
		wasVideo = video;
		if (MediaMovieShowing()) {
			if (!movieBlack) {
				const float black[4] = {0, 0, 0, 1};
				ctx->ClearRenderTargetView(rtv, black);
				swap->Present(0, 0);
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
		bool overlayTick = false;
		if (now - secondStart >= 1000) {
			fps = framesThisSecond;
			framesThisSecond = 0;
			secondStart = now;
			overlayTick = true;
		}
		const bool overlay = GetConfig().showPerf || GetConfig().showNet;
		if (newFrame && !MpUiNeedsDraw()) MpUiObserveFrame(local.data(), fw, fh);
		const bool needSaves = SavesUiNeedsDraw(), needMods = ModUiNeedsDraw(), needMp = MpUiNeedsDraw(), needControls = ControlsUiNeedsDraw();
		const bool ui = !video && (needSaves || needMods || needMp || needControls);
		if (ui && (newFrame || now - lastUiDraw >= 33)) {
			if (!newFrame) {
				{
					std::lock_guard<std::mutex> lock(g_frameLock);
					fw = g_frameW;
					fh = g_frameH;
					local = g_frame;
				}
				ControlsDrawBarKeys(local.data(), fw, fh);
			}
			Guard("interface drawing", [&] {
				MpUiDraw(local.data(), fw, fh);
				SavesUiDraw(local.data(), fw, fh);
				ModUiDraw(local.data(), fw, fh);
				ControlsUiDraw(local.data(), fw, fh);
			});
			if (overlay) {
				DrawPerfOverlay(local.data(), fw, fh, fps, bbW, bbH);
			}
			lastUiDraw = now;
			newFrame = true;
		}
		else if (!video && !ui && overlay && (newFrame || overlayTick || uiWasOn || OverlayNeedsRedraw())) {
			if (!newFrame) {
				{
					std::lock_guard<std::mutex> lock(g_frameLock);
					local = g_frame;
				}
				ControlsDrawBarKeys(local.data(), fw, fh);
			}
			DrawPerfOverlay(local.data(), fw, fh, fps, bbW, bbH);
			newFrame = true;
		}
		else if (!video && !ui && ((!overlay && overlayWasOn) || uiWasOn)) {
			{
				std::lock_guard<std::mutex> lock(g_frameLock);
				local = g_frame;
			}
			newFrame = true;
			ControlsDrawBarKeys(local.data(), fw, fh);
		}
		overlayWasOn = overlay;
		uiWasOn = ui;

		DisplayRealClientRect(g_hwnd, &rc);
		int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
		if (cw > 0 && ch > 0 && (cw != bbW || ch != bbH || !rtv)) {
			SafeRelease(rtv);
			if (cw != bbW || ch != bbH) {
				swap->ResizeBuffers(1, cw, ch, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
				bbW = cw;
				bbH = ch;
			}
			ID3D11Texture2D *back = nullptr;
			swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back));
			dev->CreateRenderTargetView(back, nullptr, &rtv);
			SafeRelease(back);
		}
		if (fw != texW || fh != texH || !tex) {
			SafeRelease(srv);
			SafeRelease(tex);
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
			dev->CreateTexture2D(&td, nullptr, &tex);
			dev->CreateShaderResourceView(tex, nullptr, &srv);
			texW = fw;
			texH = fh;
		}
		D3D11_MAPPED_SUBRESOURCE map;
		if (newFrame && SUCCEEDED(ctx->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			for (int y = 0; y < fh; y++) {
				memcpy(static_cast<uint8_t *>(map.pData) + static_cast<size_t>(y) * map.RowPitch,
					&local[static_cast<size_t>(y) * fw], static_cast<size_t>(fw) * 4);
			}
			ctx->Unmap(tex, 0);
		}

		float sx = static_cast<float>(bbW) / fw, sy = static_cast<float>(bbH) / fh;
		float s = sx < sy ? sx : sy;
		float w = fw * s / bbW * 2.0f, h = fh * s / bbH * 2.0f;
		if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			float *r = static_cast<float *>(map.pData);
			r[0] = -w / 2;
			r[1] = h / 2;
			r[2] = w;
			r[3] = h;
			r[4] = static_cast<float>(fw);
			r[5] = static_cast<float>(fh);
			r[6] = s > 1.0f ? static_cast<float>(static_cast<int>(s)) : s < 1.0f ? -1.0f / s : 0.0f;
			r[7] = GetConfig().brightness / 100.0f;
			ctx->Unmap(cb, 0);
		}

		const float black[4] = {0, 0, 0, 1};
		ctx->ClearRenderTargetView(rtv, black);
		D3D11_VIEWPORT vp = {0, 0, static_cast<float>(bbW), static_cast<float>(bbH), 0, 1};
		ctx->RSSetViewports(1, &vp);
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		ctx->VSSetShader(vs, nullptr, 0);
		ctx->VSSetConstantBuffers(0, 1, &cb);
		ctx->PSSetConstantBuffers(0, 1, &cb);
		ctx->PSSetShader(ps, nullptr, 0);
		ctx->PSSetShaderResources(0, 1, &srv);
		ctx->PSSetSamplers(0, 1, &sampler);
		ctx->Draw(4, 0);
		swap->Present(vsync ? 1 : 0, 0);
	}
	return 0;
}

}

bool RenderCopyFrame(std::vector<uint32_t> &out, int &w, int &h)
{
	std::lock_guard<std::mutex> lock(g_frameLock);
	if (g_frame.empty() || g_frameW <= 0 || g_frameH <= 0) {
		return false;
	}
	out = g_frame;
	w = g_frameW;
	h = g_frameH;
	return true;
}

namespace {

int WINAPI HookSetDIBitsToDevice(HDC hdc, int xDest, int yDest, DWORD w, DWORD h, int xSrc, int ySrc,
	UINT startScan, UINT cLines, const VOID *bits, const BITMAPINFO *bmi, UINT colorUse)
{
	AutosaveTick();
	MediaFrameTick();
	TweaksFrameTick();
	CheatsFrameTick();
	MpUiGameThreadTick();
	RejoinGameThreadTick();
	SpectatorGameThreadTick();
	ZoomFogNoteImage();
	NetDropTick();
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
	if (!target ||
		HookCreate(target, reinterpret_cast<void *>(&HookSetDIBitsToDevice),
			reinterpret_cast<void **>(&g_origSetDIBitsToDevice)) != MH_OK ||
		HookEnable(target) != MH_OK) {
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
