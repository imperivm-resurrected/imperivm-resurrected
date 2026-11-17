// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstdint>

// One entity of the game's entity cache: the name it was loaded under (the entity= of a class, "Units/RHastatus/RHastatus.ent.xml") and the entity.
const unsigned kEntityNameChars = 256;
struct EntityRawEntry {
	char name[kEntityNameChars];
	uint32_t entity;
};

// Reads the by-name map of the game's entity cache (a red-black tree of the VS2003 std::map<std::string, entity *>: left, parent, right, the string at +0xc, the
// entity after it, then the colour and the "nil" flag) from its head node, and copies up to capacity entries. The game is a 32-bit process, so the words are
// 32 bits. No fault handling here (the caller wraps it).
unsigned EntityWalkCache(const uint32_t *head, EntityRawEntry *out, unsigned capacity);
