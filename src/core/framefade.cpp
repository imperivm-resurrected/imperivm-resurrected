// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/framefade.h"

#include <windows.h>

#include <atomic>
#include <mutex>

namespace {

std::mutex g_lock;
FrameFade g_fade;
std::atomic<unsigned> g_taken{0};

}

unsigned FrameFadeHold()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_fade.seq++;
	g_fade.holding = true;
	g_fade.start = GetTickCount();
	g_fade.ms = 0;
	return g_fade.seq;
}

bool FrameFadeTaken(unsigned seq)
{
	return g_taken == seq;
}

void FrameFadeRelease(unsigned ms)
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_fade.holding = false;
	g_fade.start = GetTickCount();
	g_fade.ms = ms;
}

FrameFade FrameFadeState()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_fade;
}

void FrameFadeMarkTaken(unsigned seq)
{
	g_taken = seq;
}
