// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/roomcolors.h"

#include <windows.h>

#include <mutex>

namespace {

const DWORD kFreshMs = 120000;
// Squared distance in RGB, red and blue weighed less than green as the eye does; about a fifth of the way across the cube.
const int kMinDistance = 3 * 60 * 60;

std::mutex g_lock;
uint32_t g_rgb[kRoomColorSlots] = {};
DWORD g_setAt = 0;
bool g_any = false;

}

void RoomColorsSet(const uint32_t rgb[kRoomColorSlots])
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_any = false;
	for (int i = 0; i < kRoomColorSlots; i++) {
		g_rgb[i] = rgb[i] & 0xffffff;
		g_any = g_any || g_rgb[i];
	}
	g_setAt = GetTickCount();
}

bool RoomColorsTake(uint32_t rgb[kRoomColorSlots])
{
	std::lock_guard<std::mutex> lock(g_lock);
	const bool fresh = g_any && GetTickCount() - g_setAt < kFreshMs;
	for (int i = 0; i < kRoomColorSlots; i++) rgb[i] = fresh ? g_rgb[i] : 0;
	g_any = false;
	return fresh;
}

bool RoomColorsTooClose(uint32_t a, uint32_t b)
{
	const int dr = static_cast<int>((a >> 16) & 0xff) - static_cast<int>((b >> 16) & 0xff);
	const int dg = static_cast<int>((a >> 8) & 0xff) - static_cast<int>((b >> 8) & 0xff);
	const int db = static_cast<int>(a & 0xff) - static_cast<int>(b & 0xff);
	return 2 * dr * dr + 4 * dg * dg + 3 * db * db < kMinDistance;
}
