// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
// Out of multiplayer, an order runs in the frame it is given instead of up to two 150 ms command turns later.
void InstallOrderDelay(bool knownExe);
