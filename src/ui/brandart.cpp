// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/brandart.h"

#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ui {

std::wstring BrandFile(const wchar_t *name)
{
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir(exe);
	return dir.substr(0, dir.find_last_of(L'\\') + 1) + L"imperivm-resurrected\\brand\\" + name;
}

bool LoadPicture(const std::wstring &file, Picture &out)
{
	IWICImagingFactory *factory = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICFormatConverter *conv = nullptr;
	// The game thread may already be in COM with another model, which still lets WIC work.
	const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
	UINT w = 0, h = 0;
	bool ok = false;
	if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
		SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
		SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096 &&
		SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
		SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
		out.px.resize(static_cast<size_t>(w) * h);
		ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.px.size() * 4), reinterpret_cast<BYTE *>(out.px.data())));
		out.w = ok ? static_cast<int>(w) : 0;
		out.h = ok ? static_cast<int>(h) : 0;
		if (!ok) out.px.clear();
	}
	if (conv) conv->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (factory) factory->Release();
	if (com) CoUninitialize();
	return ok;
}

}
