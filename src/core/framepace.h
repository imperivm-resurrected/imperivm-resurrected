// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// The ticks between two images for a limit of fps images per second at freq ticks per second; 0 is no limit.
int64_t FrameInterval(int64_t freq, int fps);

// How long to wait before showing the next image, and moves deadline to the one after it. An image late by a
// whole interval starts the count again from now, so a stall is not followed by a burst to catch up.
int64_t FramePaceWait(int64_t &deadline, int64_t now, int64_t interval);
