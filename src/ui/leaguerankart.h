// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "core/league.h"
#include "ui/leagueemblem.h"

// The emblem of a rank from the PNGs that winmm.dll carries (tools/rank-emblems, src/app/winmm.rc), `size` pixels square and
// premultiplied like the drawn pictures. Empty for LeagueTier::None, and when the PNG cannot be read: the caller then draws the old one.
LeagueImage LeagueRankArt(LeagueTier tier, int size);
