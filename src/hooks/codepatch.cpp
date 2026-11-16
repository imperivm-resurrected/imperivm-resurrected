// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "hooks/codepatch.h"

#include <windows.h>

#include <cstring>

bool WriteCode(void *at, const void *bytes, size_t n)
{
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, bytes, n);
	VirtualProtect(at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	return true;
}

bool PatchCode(uintptr_t site, const void *expected, const void *patch, size_t n)
{
	void *at = reinterpret_cast<void *>(site);
	return memcmp(at, expected, n) == 0 && WriteCode(at, patch, n);
}

void CallBytes(uint8_t *out, uintptr_t site, uintptr_t target)
{
	const int32_t rel = static_cast<int32_t>(target - (site + 5));
	out[0] = 0xe8;
	memcpy(out + 1, &rel, sizeof(rel));
}
