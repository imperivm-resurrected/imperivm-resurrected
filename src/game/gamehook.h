// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

#include "hooks/hookcount.h"

// Hooks the game's function at `target` with `detour` and enables the hook; `original` gets the trampoline. False when either step
// failed (hookcount counts it for the start-up summary); the caller logs what is lost.
template <class Detour, class Original> bool HookFunction(uintptr_t target, Detour detour, Original *original)
{
	void *at = reinterpret_cast<void *>(target);
	return HookCreate(at, reinterpret_cast<void *>(detour), reinterpret_cast<void **>(original)) == MH_OK && HookEnable(at) == MH_OK;
}
