// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include "ui/uikit.h"

// The medal of an adventure or a conquest won: a coin of bronze, silver or gold for the hardest difficulty won (1 to 3), with the laurel
// wreath struck on it. One not won yet shows the wreath alone, faint.
void PaintMedal(ui::Painter &p, int x, int y, int size, int level);
