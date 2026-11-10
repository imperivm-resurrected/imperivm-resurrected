// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/framepace.h"

int64_t FrameInterval(int64_t freq, int fps)
{
	return fps > 0 && freq > 0 ? (freq + fps / 2) / fps : 0;
}

int64_t FramePaceWait(int64_t &deadline, int64_t now, int64_t interval)
{
	if (interval <= 0) {
		deadline = 0;
		return 0;
	}
	if (deadline == 0 || now - deadline >= interval) deadline = now;
	const int64_t wait = deadline > now ? deadline - now : 0;
	deadline += interval;
	return wait;
}
