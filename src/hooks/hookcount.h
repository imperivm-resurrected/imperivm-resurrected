// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

#include "MinHook.h"

MH_STATUS HookCreate(LPVOID target, LPVOID detour, LPVOID *original);

MH_STATUS HookEnable(LPVOID target);

// HookCreate and then HookEnable; false if either failed, which LogHookSummary already counts.
bool HookInstall(LPVOID target, LPVOID detour, LPVOID *original);

// The detour and the pointer to the original must have the same type, calling convention included.
template <class Fn> bool HookInstall(LPVOID target, Fn *detour, Fn **original)
{
	return HookInstall(target, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID *>(original));
}

template <class Fn> bool HookInstall(uintptr_t address, Fn *detour, Fn **original)
{
	return HookInstall(reinterpret_cast<LPVOID>(address), reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID *>(original));
}

void HookBatchBegin();
void HookBatchEnd();

void LogHookSummary();
