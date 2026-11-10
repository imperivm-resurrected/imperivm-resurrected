// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/imagefit.h"

#include <cmath>

float ImageScale(int outW, int outH, int imgW, int imgH, int scaling)
{
	if (outW <= 0 || outH <= 0 || imgW <= 0 || imgH <= 0) return 1.0f;
	const float sx = static_cast<float>(outW) / imgW, sy = static_cast<float>(outH) / imgH;
	const float fit = sx < sy ? sx : sy;
	return scaling == kScaleExact && fit >= 1.0f ? std::floor(fit) : fit;
}

int ImageMargin(int out, int img, float scale)
{
	return static_cast<int>(std::floor((out - img * scale) / 2));
}

float ImageSharpness(float scale, int scaling)
{
	if (scale < 1.0f) return -1.0f / scale;
	if (scale == 1.0f || scaling == kScaleSmooth) return 0.0f;
	// Each texel is drawn as the next whole number of pixels and squeezed back, so only the one pixel where
	// two texels meet is blended; with the whole number below, an image enlarged less than twice was blurred.
	return std::ceil(scale);
}
