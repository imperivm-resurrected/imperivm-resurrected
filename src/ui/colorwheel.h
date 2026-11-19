// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

#include "ui/uikit.h"

// A colour picker: a wheel of hue (around) and saturation (from the centre) and a strip of brightness beside it. Colours are 0xRRGGBB.
namespace colorwheel {

// Hue, saturation and value, each 0..1, and back.
void ToHsv(uint32_t rgb, double &h, double &s, double &v);
uint32_t FromHsv(double h, double s, double v);
COLORREF Ref(uint32_t rgb);

constexpr int kWheel = 180, kStrip = 22, kGap = 14;
constexpr int kWidth = kWheel + kGap + kStrip;

// The wheel at (x, y) and the strip at its right, kWidth x kWheel, with `around` behind the wheel's corners; the hits are
// `wheelAction` and `stripAction` with `arg`.
void Paint(ui::Painter &p, int x, int y, uint32_t color, COLORREF around, int wheelAction, int stripAction, int arg);
// The colour a click or a drag at (mx, my) picks in the strip (the brightness) or the wheel (hue and saturation, with the brightness of
// `current`, kept from black), whose hit is `hit`.
uint32_t Pick(bool strip, const RECT &hit, int mx, int my, uint32_t current);

}
