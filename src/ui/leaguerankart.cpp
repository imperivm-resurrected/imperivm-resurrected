// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/leaguerankart.h"

#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/log.h"

namespace {

// The resource names of src/app/winmm.rc, by tier (Recruit first): the 256-pixel picture, then the 64 one.
const wchar_t *const kResources[kLeagueTierCount][2] = {
	{L"LEAGUE_RECRUIT_256", L"LEAGUE_RECRUIT_64"},
	{L"LEAGUE_LEGIONARY_256", L"LEAGUE_LEGIONARY_64"},
	{L"LEAGUE_SIGNIFER_256", L"LEAGUE_SIGNIFER_64"},
	{L"LEAGUE_CENTURION_256", L"LEAGUE_CENTURION_64"},
	{L"LEAGUE_TRIBUNE_256", L"LEAGUE_TRIBUNE_64"},
	{L"LEAGUE_LEGATE_256", L"LEAGUE_LEGATE_64"},
	{L"LEAGUE_PRAETORIAN_256", L"LEAGUE_PRAETORIAN_64"},
	{L"LEAGUE_IMPERATOR_256", L"LEAGUE_IMPERATOR_64"},
};
constexpr int kSmallSource = 64;

struct Source {
	bool tried = false;
	int w = 0, h = 0;
	std::vector<uint32_t> px;
};

std::mutex g_mx;
Source g_sources[kLeagueTierCount][2];
bool g_warned = false;

// The resources are in winmm.dll, not in the game's exe that GetModuleHandle(nullptr) gives.
HMODULE OwnModule()
{
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&OwnModule),
		&self);
	return self;
}

template <class T> void Release(T *&p)
{
	if (p) p->Release();
	p = nullptr;
}

// The PNG, premultiplied as it is decoded: scaling straight alpha would let the colour of the clear pixels around the art bleed into
// its edge as a dark fringe.
bool Decode(const wchar_t *name, Source &out)
{
	const HMODULE self = OwnModule();
	const HRSRC res = self ? FindResourceW(self, name, reinterpret_cast<LPCWSTR>(RT_RCDATA)) : nullptr;
	const HGLOBAL global = res ? LoadResource(self, res) : nullptr;
	const BYTE *data = global ? static_cast<const BYTE *>(LockResource(global)) : nullptr;
	const DWORD bytes = res ? SizeofResource(self, res) : 0;
	if (!data || !bytes) return false;
	IWICImagingFactory *factory = nullptr;
	IWICStream *stream = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICFormatConverter *conv = nullptr;
	// The game thread may already be in COM with another model, which still lets WIC work.
	const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	UINT w = 0, h = 0;
	bool ok = false;
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE *>(data), bytes)) &&
		SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
		SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 1024 && h <= 1024 &&
		SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
		SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
		out.px.resize(static_cast<size_t>(w) * h);
		ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.px.size() * 4), reinterpret_cast<BYTE *>(out.px.data())));
	}
	out.w = ok ? static_cast<int>(w) : 0;
	out.h = ok ? static_cast<int>(h) : 0;
	if (!ok) out.px.clear();
	Release(conv);
	Release(frame);
	Release(decoder);
	Release(stream);
	Release(factory);
	if (com) CoUninitialize();
	return ok;
}

LeagueImage Scaled(const Source &src, int size)
{
	LeagueImage img;
	if (src.w == size && src.h == size) {
		img.w = img.h = size;
		img.px = src.px;
		return img;
	}
	IWICImagingFactory *factory = nullptr;
	IWICBitmap *bitmap = nullptr;
	IWICBitmapScaler *scaler = nullptr;
	IWICFormatConverter *conv = nullptr;
	const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	const UINT side = static_cast<UINT>(size);
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateBitmapFromMemory(static_cast<UINT>(src.w), static_cast<UINT>(src.h), GUID_WICPixelFormat32bppPBGRA,
			static_cast<UINT>(src.w) * 4, static_cast<UINT>(src.px.size() * 4), reinterpret_cast<BYTE *>(const_cast<uint32_t *>(src.px.data())),
			&bitmap))) {
		// High-quality cubic needs Windows 10; Fant is the fallback.
		for (const WICBitmapInterpolationMode mode : {WICBitmapInterpolationModeHighQualityCubic, WICBitmapInterpolationModeFant}) {
			if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(bitmap, side, side, mode))) break;
			Release(scaler);
		}
	}
	// The scaler may hand out another pixel format than the one it was given.
	if (scaler && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
		SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
		img.px.resize(static_cast<size_t>(side) * side);
		if (SUCCEEDED(conv->CopyPixels(nullptr, side * 4, static_cast<UINT>(img.px.size() * 4), reinterpret_cast<BYTE *>(img.px.data())))) {
			img.w = img.h = size;
			// The cubic filter overshoots at hard edges and can leave a colour above its alpha, which AlphaBlend would wrap round.
			for (uint32_t &px : img.px) {
				const uint32_t a = px >> 24;
				px = (a << 24) | (std::min((px >> 16) & 0xFF, a) << 16) | (std::min((px >> 8) & 0xFF, a) << 8) | std::min(px & 0xFF, a);
			}
		}
		else {
			img.px.clear();
		}
	}
	Release(conv);
	Release(scaler);
	Release(bitmap);
	Release(factory);
	if (com) CoUninitialize();
	return img;
}

}

LeagueImage LeagueRankArt(LeagueTier tier, int size)
{
	const int index = static_cast<int>(tier) - 1;
	if (index < 0 || index >= kLeagueTierCount || size <= 0 || size > 2048) return {};
	const int which = size > kSmallSource ? 0 : 1;
	std::lock_guard<std::mutex> lock(g_mx);
	Source &src = g_sources[index][which];
	if (!src.tried) {
		src.tried = true;
		if (!Decode(kResources[index][which], src) && !g_warned) {
			g_warned = true;
			LogW("ui", "league: the %d px emblem of rank %d is missing or unreadable, the old drawing stands in", which ? kSmallSource : 256, index + 1);
		}
	}
	if (src.px.empty()) return {};
	LeagueImage img = Scaled(src, size);
	if (img.px.empty() && !g_warned) {
		g_warned = true;
		LogW("ui", "league: the emblem of rank %d could not be scaled to %d px, the old drawing stands in", index + 1, size);
	}
	return img;
}
