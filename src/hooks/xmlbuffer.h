// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once

// The game's XML reader leaks the buffer it reads each file into: 8 KB per class file, 64 KB per entity, and the same for every other XML file it
// loads. One byte of its code is changed so that the buffer is freed on the usual exit too (see addr/engine.h). It matters most when the game
// reads its data again (a ruleset switched without restarting): every switch left the 848 class buffers behind.
void InstallXmlBufferFix(bool knownExe);
