// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <string>

#include "mods/mods.h"

enum ProfileCodeProblem { kProfileCodeOk = 0, kProfileCodeNotACode = 1, kProfileCodeDamaged = 2, kProfileCodeNewer = 3 };

std::string EncodeProfileCode(const ModsSharedProfile &sp);

bool DecodeProfileCode(const std::string &text, ModsSharedProfile &out, int *problem = nullptr);
