// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// Straight (not premultiplied) BGRA pixels, top row first.
struct Picture {
	std::vector<uint32_t> px;
	int w = 0, h = 0;
};

// A file of a folder of imperivm-resurrected next to the game, where the release puts the repository's mod folder.
std::wstring ModArtFile(const wchar_t *folder, const wchar_t *name);
// A file of imperivm-resurrected\brand.
std::wstring BrandFile(const wchar_t *name);
// Decodes a PNG or JPEG with WIC: false when it is missing, unreadable or larger than 4096 pixels on a side.
bool LoadPicture(const std::wstring &file, Picture &out);

}
