// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <vector>

#include "ui/uikit.h"

namespace ui {

inline POINT PointOf(LPARAM lp)
{
	return {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
}

inline bool KeyHeld(int vk)
{
	return (GetKeyState(vk) & 0x8000) != 0;
}

// The hits are added as they are painted, so the last one under the point is the one on top.
inline const Hit *TopHit(const std::vector<Hit> &hits, POINT pt)
{
	for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
		if (PtInRect(&it->r, pt)) return &*it;
	}
	return nullptr;
}

}
