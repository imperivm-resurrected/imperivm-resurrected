// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "net/patrons.h"
#include "ui/uikit.h"

#include <string>

COLORREF PatronColor(PatronTier tier);
// The laurel of the honour roll, in a square of `size` logical pixels.
void DrawPatronLaurel(ui::Painter &p, int x, int y, int size, PatronTier tier);
// The name in the colour of the tier; on the top three a light sweeps across it now and then.
void DrawPatronName(ui::Painter &p, int x, int y, int w, int h, const std::string &name, PatronTier tier, UINT fmt);
