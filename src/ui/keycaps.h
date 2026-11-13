// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>

#include <string>
#include <vector>

#include "game/controls.h"
#include "ui/uikit.h"

namespace ui {

void ArrowGlyph(Painter &p, int cx, int cy, unsigned short vk, COLORREF c);
bool IsArrow(unsigned short vk);
std::vector<std::string> CapTexts(ControlKey k);
int CapWidth(Painter &p, const std::string &text);
void DrawCap(Painter &p, int x, int y, int w, int h, const std::string &text, unsigned short vk, COLORREF edge, COLORREF fg,
	COLORREF face = pal::edge);
void DrawKey(Painter &p, int x, int y, int w, int h, ControlKey k, COLORREF edge, COLORREF fg, COLORREF face = pal::edge);
std::vector<ControlKey> KnownKeys(const std::vector<ControlKey> &keys);
int KeysWidth(Painter &p, const std::vector<ControlKey> &keys);
void DrawKeys(Painter &p, int x, int y, int h, const std::vector<ControlKey> &keys);

}
