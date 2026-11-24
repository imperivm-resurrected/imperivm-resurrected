// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "ui/medal.h"

#include "ui/patronui.h"

using namespace ui;

void PaintMedal(Painter &p, int x, int y, int size, int level)
{
	static const COLORREF kRim[3] = {RGB(112, 70, 38), RGB(118, 122, 130), RGB(150, 110, 36)};
	static const COLORREF kFace[3] = {RGB(186, 124, 70), RGB(204, 208, 214), RGB(230, 188, 92)};
	static const COLORREF kStrike[3] = {RGB(84, 50, 24), RGB(72, 76, 84), RGB(104, 72, 14)};
	const int wreath = size * 78 / 100;
	if (level < 1 || level > 3) {
		PaintWreath(p, x + (size - wreath) / 2, y + (size - wreath) / 2, wreath, pal::hairline);
		return;
	}
	p.Ellipse(x, y, size, size, kRim[level - 1]);
	p.Ellipse(x + 2, y + 2, size - 4, size - 4, kFace[level - 1]);
	PaintWreath(p, x + (size - wreath) / 2, y + (size - wreath) / 2, wreath, kStrike[level - 1]);
}
