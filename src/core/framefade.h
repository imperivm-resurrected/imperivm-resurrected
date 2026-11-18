// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// A cross-fade of the game's picture: the renderer keeps showing the picture of the moment of Hold, while the game changes under it,
// and after Release it fades into the new one. A hold lets go on its own after kFrameFadeHoldMs.
const unsigned kFrameFadeHoldMs = 3000;

struct FrameFade {
	unsigned seq = 0;
	bool holding = false;
	unsigned long start = 0, ms = 0;
};

// Asks for a hold; the number to wait for with FrameFadeTaken.
unsigned FrameFadeHold();
// The renderer has the picture of hold `seq`.
bool FrameFadeTaken(unsigned seq);
void FrameFadeRelease(unsigned ms);

// The renderer's side.
FrameFade FrameFadeState();
void FrameFadeMarkTaken(unsigned seq);
