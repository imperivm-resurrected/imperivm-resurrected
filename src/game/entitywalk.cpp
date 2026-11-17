// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/entitywalk.h"

#include <cstring>

namespace {
// The std::string key sits at +0xc (28 bytes), so the value follows at +0x28, the colour at +0x2c and the "nil" flag after it.
const unsigned kNilOffset = 0x2d, kValueIndex = 10;
// The string: characters inline (or behind a pointer when the capacity is 16 or more) at +0x10, length +0x20, capacity +0x24.
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

unsigned EntityWalkCache(const uint32_t *head, EntityRawEntry *out, unsigned capacity)
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
		if (n >= capacity) continue;
		const char *base = reinterpret_cast<const char *>(node);
		const unsigned length = Read<uint32_t>(base, kLengthOffset), reserve = Read<uint32_t>(base, kCapacityOffset);
		const char *text = reserve < 16 ? base + kCharsOffset : reinterpret_cast<const char *>(static_cast<uintptr_t>(Read<uint32_t>(base, kCharsOffset)));
		if (!text || length >= kEntityNameChars) continue;
		memcpy(out[n].name, text, length);
		out[n].name[length] = 0;
		out[n].entity = node[kValueIndex];
		n++;
	}
	return n;
}
