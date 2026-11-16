// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include "core/log.h"
#include "core/version.h"
#include "mods/updater.h"
#include <wincodec.h>
#include <mutex>

#include "ui/modui_internal.h"

using namespace modui_impl;

namespace modui_impl {
namespace {
void BlendPixel(uint32_t *buf, int w, int h, int x, int y, uint32_t color, unsigned a)
{
	if (x < 0 || x >= w || y < 0 || y >= h || !a) return;
	uint32_t &dst = buf[static_cast<size_t>(y) * w + x];
	const uint32_t rb = ((color & 0xff00ff) * a + (dst & 0xff00ff) * (255 - a)) >> 8;
	const uint32_t g = ((color & 0x00ff00) * a + (dst & 0x00ff00) * (255 - a)) >> 8;
	dst = 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
}

void LoadDiscordIcon()
{
	g_icon.loaded = true;
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) *(slash + 1) = 0;
	const std::wstring file = W(std::string(path) + "imperivm-resurrected\\discord.png");
	IWICImagingFactory *factory = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICFormatConverter *conv = nullptr;
	const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	UINT w = 0, h = 0;
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
		SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 1024 && h <= 1024 &&
		SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
		SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
		g_icon.base.resize(static_cast<size_t>(w) * h);
		if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(g_icon.base.size() * 4), reinterpret_cast<BYTE *>(g_icon.base.data())))) {
			g_icon.baseW = static_cast<int>(w);
			g_icon.baseH = static_cast<int>(h);
			LogI("discord", "icon from imperivm-resurrected\\discord.png (%ux%u)", w, h);
		}
		else {
			g_icon.base.clear();
		}
	}
	if (conv) conv->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (factory) factory->Release();
	if (com) CoUninitialize();
	if (!g_icon.base.empty()) return;
	const int n = 128;
	Canvas c;
	if (!c.Ensure(n, n)) return;
	HDC dc = c.dc;
	RECT all = {0, 0, n, n};
	HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
	FillRect(dc, &all, key);
	DeleteObject(key);
	Painter pa{dc, 1.0f, 0, 0, nullptr};
	DrawDiscordIcon(pa, 4, 4, n - 8);
	GdiFlush();
	g_icon.base.resize(static_cast<size_t>(n) * n);
	for (int i = 0; i < n * n; i++) {
		const uint32_t px = c.bits[i] & 0xffffff;
		g_icon.base[i] = px == 0xff00ff ? 0 : (0xff000000u | px);
	}
	g_icon.baseW = g_icon.baseH = n;
}

const std::vector<uint32_t> &ScaledIcon(int size)
{
	if (g_icon.scaledSize == size) return g_icon.scaled;
	g_icon.scaledSize = size;
	g_icon.scaled.assign(static_cast<size_t>(size) * size, 0);
	if (g_icon.base.empty()) return g_icon.scaled;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			const int x0 = x * g_icon.baseW / size, x1 = std::max(x0 + 1, (x + 1) * g_icon.baseW / size);
			const int y0 = y * g_icon.baseH / size, y1 = std::max(y0 + 1, (y + 1) * g_icon.baseH / size);
			unsigned a = 0, r = 0, g = 0, b = 0, count = 0;
			for (int yy = y0; yy < y1 && yy < g_icon.baseH; yy++) {
				for (int xx = x0; xx < x1 && xx < g_icon.baseW; xx++) {
					const uint32_t px = g_icon.base[static_cast<size_t>(yy) * g_icon.baseW + xx];
					const unsigned pa = px >> 24;
					a += pa;
					r += ((px >> 16) & 0xff) * pa;
					g += ((px >> 8) & 0xff) * pa;
					b += (px & 0xff) * pa;
					count++;
				}
			}
			if (!count || !a) continue;
			g_icon.scaled[static_cast<size_t>(y) * size + x] = ((a / count) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
		}
	}
	return g_icon.scaled;
}
}
}

namespace modui_impl {
RECT DiscordIconRect(int w, int h)
{
	const int size = std::max(36, std::min(72, h / 20));
	const int margin = std::max(12, size / 3);
	return {margin, h - margin - size, margin + size, h - margin};
}

void DrawDiscordCorner(uint32_t *buf, int w, int h)
{
	if (!g_icon.loaded) LoadDiscordIcon();
	const RECT r = DiscordIconRect(w, h);
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_icon.rect = r;
	}
	const int size = r.right - r.left;
	const std::vector<uint32_t> &icon = ScaledIcon(size);
	const bool hover = g_hover.x >= r.left && g_hover.x < r.right && g_hover.y >= r.top && g_hover.y < r.bottom;
	const int cy = (r.top + r.bottom) / 2;
	const uint32_t tint = hover ? 0x5865F2u : PixelOf(pal::text);
	const int shadow = std::max(1, size / 24);
	for (int pass = 0; pass < 2; pass++) {
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				const unsigned alpha = icon[static_cast<size_t>(y) * size + x] >> 24;
				if (pass == 0) BlendPixel(buf, w, h, r.left + x + shadow, r.top + y + shadow, 0x000000u, alpha * 130 / 255);
				else BlendPixel(buf, w, h, r.left + x, r.top + y, tint, alpha * (hover ? 255 : 235) / 255);
			}
		}
	}
	if (hover) {
		const float s = std::max(0.6f, std::min(1.6f, h / 1080.0f * 1.2f));
		Card card;
		card.text = "Join our Discord";
		card.accent = RGB(88, 101, 242);
		card.atX = r.right + size / 3;
		card.atCenterY = cy;
		DrawCard(g_icon.canvas, buf, w, h, card, s, 235);
	}
}
}

namespace modui_impl {
namespace {
Canvas g_versionCanvas;
}
}

namespace modui_impl {
void DrawVersionCorner(uint32_t *buf, int w, int h)
{
	const float s = std::max(0.6f, std::min(1.6f, h / 1080.0f * 1.2f));
	EnsureFonts(s);
	const std::wstring ws = W(std::string("Imperivm Resurrected  ") + ModVersionText());
	HDC measure = CreateCompatibleDC(nullptr);
	HGDIOBJ old = SelectObject(measure, g_fonts[3]);
	SIZE sz = {};
	GetTextExtentPoint32W(measure, ws.c_str(), static_cast<int>(ws.size()), &sz);
	SelectObject(measure, old);
	DeleteDC(measure);
	const int pad = 2, cw = sz.cx + pad * 2, ch = sz.cy + pad * 2;
	if (!g_versionCanvas.Ensure(cw, ch)) return;
	HDC dc = g_versionCanvas.dc;
	RECT all = {0, 0, cw, ch};
	HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
	FillRect(dc, &all, black);
	DeleteObject(black);
	SelectObject(dc, g_fonts[3]);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	RECT tr = {pad, pad, cw, ch};
	DrawTextW(dc, ws.c_str(), static_cast<int>(ws.size()), &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
	GdiFlush();
	TEXTMETRICW tm = {};
	GetTextMetricsW(dc, &tm);
	int inkRight = 0;
	for (int y = 0; y < ch; y++) {
		for (int x = cw - 1; x > inkRight; x--) {
			if ((g_versionCanvas.bits[static_cast<size_t>(y) * cw + x] >> 8) & 0xff) {
				inkRight = x;
				break;
			}
		}
	}
	const int size = std::max(36, std::min(72, h / 20));
	const int margin = std::max(8, size / 5);
	const int left = w - margin - (inkRight + 1), top = h - margin - (pad + tm.tmAscent);
	const int shadow = std::max(1, static_cast<int>(s + 0.5f));
	for (int pass = 0; pass < 2; pass++) {
		for (int y = 0; y < ch; y++) {
			for (int x = 0; x < cw; x++) {
				const unsigned a = (g_versionCanvas.bits[static_cast<size_t>(y) * cw + x] >> 8) & 0xff;
				if (!a) continue;
				if (pass == 0) BlendPixel(buf, w, h, left + x + shadow, top + y + shadow, 0x000000u, a * 190 / 255);
				else BlendPixel(buf, w, h, left + x, top + y, PixelOf(pal::textDim), a);
			}
		}
	}
	UpdaterMenuReached();
}
}
