// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/artwalk.h"

#include <cstring>

namespace {
// Node layout of the game's std::map: three links, then the value; the "nil" flag of a node is the byte after its colour, past the value.
const unsigned kNilOffset = 0x15;
// The std::string of VS2003 inside a by-name node: characters inline (or behind a pointer when the capacity is 16 or more) at +0x10, length +0x20, capacity +0x24.
const unsigned kCharsOffset = 0x10, kLengthOffset = 0x20, kCapacityOffset = 0x24;

template <typename T> T Read(const void *base, unsigned offset)
{
	T value;
	memcpy(&value, static_cast<const char *>(base) + offset, sizeof(T));
	return value;
}

const uint32_t *Link(const uint32_t *node, unsigned index)
{
	return reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(node[index]));
}
}

unsigned ArtWalkFileCache(const uint32_t *head, ArtRawEntry *out, unsigned capacity)
{
	unsigned n = 0;
	if (!head) return 0;
	const uint32_t *stack[160];
	int sp = 0;
	const uint32_t *root = Link(head, 1);
	if (root && root != head && !Read<char>(root, kNilOffset)) stack[sp++] = root;
	while (sp > 0) {
		const uint32_t *node = stack[--sp];
		for (unsigned side = 0; side < 3; side += 2) {
			const uint32_t *child = Link(node, side);
			if (child && child != head && !Read<char>(child, kNilOffset) && sp < 158) stack[sp++] = child;
		}
		const uint32_t *entry = Link(node, 3);
		const char *byName = reinterpret_cast<const char *>(Link(node, 4));
		if (!entry || !byName || n >= capacity) continue;
		const unsigned length = Read<uint32_t>(byName, kLengthOffset), reserve = Read<uint32_t>(byName, kCapacityOffset);
		const char *text = reserve < 16 ? byName + kCharsOffset : reinterpret_cast<const char *>(static_cast<uintptr_t>(Read<uint32_t>(byName, kCharsOffset)));
		if (!text || length >= kArtNameChars) continue;
		memcpy(out[n].name, text, length);
		out[n].name[length] = 0;
		out[n].refs = static_cast<int>(entry[1]);
		n++;
	}
	return n;
}
