// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// How the game's image is enlarged to the window: Sharp keeps the pixels crisp with soft edges between them,
// Smooth blends them, Pixel-exact only enlarges by whole steps and leaves a border.
enum ImageScaling { kScaleSharp = 0, kScaleSmooth = 1, kScaleExact = 2 };

// The scale of an image of imgW x imgH drawn in an area of outW x outH. An image larger than the area is
// always shrunk to fit, whatever the scaling.
float ImageScale(int outW, int outH, int imgW, int imgH, int scaling);

// The margin before the image, in whole pixels, so the pixels of a Pixel-exact image land on the screen's.
int ImageMargin(int out, int img, float scale);

// What the presenter's shader is told: the whole step above the scale for a sharp enlargement, 0 for a plain
// one, and minus the shrink factor for a smaller image.
float ImageSharpness(float scale, int scaling);
