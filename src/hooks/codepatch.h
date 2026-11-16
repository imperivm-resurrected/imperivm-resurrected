// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>

// Writes n bytes over code, lifting its protection while it does, and flushes the instruction cache.
bool WriteCode(void *at, const void *bytes, size_t n);

// WriteCode at site, only if its n bytes are still the expected ones.
bool PatchCode(uintptr_t site, const void *expected, const void *patch, size_t n);

// The 5 bytes of a call (E8 rel32) placed at site that goes to target.
void CallBytes(uint8_t *out, uintptr_t site, uintptr_t target);
