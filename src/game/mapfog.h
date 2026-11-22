// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
void InstallZoomFog(bool knownExe);
long long ZoomFogGameThreadTick();
void ZoomFogNoteImage();
bool ZoomPixelToWorld(int px, int py, int *world);
bool ZoomBitmapSize(int *width, int *height);
// A terrain point, already projected to the isometric view, as a pixel of the big map's bitmap.
bool ZoomProjectedToPixel(int x, int y, int *pixel);
