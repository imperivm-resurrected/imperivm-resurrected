// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include "core/imagefit.h"

static bool Near(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }

static void Scale()
{
	CHECK(Near(ImageScale(1920, 1080, 1366, 768, kScaleSharp), 1920.0f / 1366));
	CHECK(Near(ImageScale(1920, 1080, 1366, 768, kScaleSmooth), 1920.0f / 1366));
	CHECK(Near(ImageScale(1920, 1080, 1366, 768, kScaleExact), 1.0f));
	CHECK(Near(ImageScale(3840, 2160, 1920, 1080, kScaleExact), 2.0f));
	CHECK(Near(ImageScale(3840, 2160, 1280, 768, kScaleExact), 2.0f));
	// Larger than the area (extended view): shrunk to fit in every mode.
	CHECK(Near(ImageScale(1920, 1080, 2400, 1350, kScaleExact), 0.8f));
	CHECK(Near(ImageScale(1920, 1080, 2400, 1350, kScaleSharp), 0.8f));
	CHECK(Near(ImageScale(0, 1080, 1920, 1080, kScaleSharp), 1.0f));
}

static void Margin()
{
	CHECK_EQ(ImageMargin(1920, 1366, 1.0f), 277);
	CHECK_EQ(ImageMargin(1080, 768, 1.0f), 156);
	CHECK_EQ(ImageMargin(2160, 768, 2.0f), 312);
	CHECK_EQ(ImageMargin(1921, 1920, 1.0f), 0);
	CHECK_EQ(ImageMargin(1080, 1080, 1.0f), 0);
}

static void Sharpness()
{
	CHECK(Near(ImageSharpness(1.4f, kScaleSharp), 2.0f));
	CHECK(Near(ImageSharpness(2.8f, kScaleSharp), 3.0f));
	CHECK(Near(ImageSharpness(2.0f, kScaleSharp), 2.0f));
	CHECK(Near(ImageSharpness(1.4f, kScaleSmooth), 0.0f));
	CHECK(Near(ImageSharpness(2.8f, kScaleSmooth), 0.0f));
	CHECK(Near(ImageSharpness(2.0f, kScaleExact), 2.0f));
	CHECK(Near(ImageSharpness(1.0f, kScaleSharp), 0.0f));
	CHECK(Near(ImageSharpness(0.5f, kScaleSmooth), -2.0f));
}

int main()
{
	Scale();
	Margin();
	Sharpness();
	return test::Summary("imagefit");
}
