// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
// The reader of the game's entity cache against a tree made with the layout of the game's VS2003 std::map<std::string, entity *>. The game is a 32-bit process, so
// the tree can only be built with addresses below 4 GB: on a 32-bit build every address is, on Linux the memory is asked with MAP_32BIT, and where neither
// holds the test says so and passes.
#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/entitywalk.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace {
bool g_fits = true;

void *Alloc(size_t size)
{
	void *p = nullptr;
#ifdef __linux__
	p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (p == MAP_FAILED) p = nullptr;
	(void)size;
#else
	p = calloc(1, size);
#endif
	if (!p || (reinterpret_cast<uintptr_t>(p) >> 16 >> 16) != 0) g_fits = false;
	return p;
}

uint32_t Word(const void *p)
{
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// A node: left, parent, right, the string (28 bytes), the entity, colour, nil.
char *Node(char *left, char *right, const std::string &name, uint32_t entity, bool nil)
{
	char *n = static_cast<char *>(Alloc(0x30));
	const uint32_t l = Word(left), r = Word(right);
	memcpy(n, &l, 4);
	memcpy(n + 8, &r, 4);
	if (!nil) {
		const uint32_t length = static_cast<uint32_t>(name.size());
		memcpy(n + 0x20, &length, 4);
		if (name.size() < 16) {
			const uint32_t reserve = 15;
			memcpy(n + 0x24, &reserve, 4);
			memcpy(n + 0x10, name.c_str(), name.size() + 1);
		}
		else {
			const uint32_t reserve = length + 8;
			memcpy(n + 0x24, &reserve, 4);
			char *text = static_cast<char *>(Alloc(name.size() + 1));
			memcpy(text, name.c_str(), name.size() + 1);
			const uint32_t pointer = Word(text);
			memcpy(n + 0x10, &pointer, 4);
		}
		memcpy(n + 0x28, &entity, 4);
	}
	n[0x2d] = nil ? 1 : 0;
	return n;
}

void ReadsEveryEntityOfTheTree()
{
	const std::string longName = "Units/RHastatus/RHastatus.ent.xml";
	char *nil = Node(nullptr, nullptr, "", 0, true);
	char *a = Node(nil, nil, "Units/A/A.ent.xml", 0x1000, false);
	char *b = Node(nil, nil, longName, 0x2000, false);
	char *root = Node(a, b, "MapObjects/Tr1.ent.xml", 0x3000, false);
	char *head = Node(a, b, "", 0, true);
	const uint32_t rootWord = Word(root);
	memcpy(head + 4, &rootWord, 4);
	if (!g_fits) {
		std::printf("entitywalk: the tree needs addresses below 4 GB, skipped\n");
		return;
	}
	EntityRawEntry out[8];
	const unsigned n = EntityWalkCache(reinterpret_cast<const uint32_t *>(head), out, 8);
	CHECK_EQ(n, 3u);
	bool seenA = false, seenB = false, seenRoot = false;
	for (unsigned i = 0; i < n; i++) {
		const std::string name = out[i].name;
		if (name == "Units/A/A.ent.xml") seenA = out[i].entity == 0x1000;
		if (name == longName) seenB = out[i].entity == 0x2000;
		if (name == "MapObjects/Tr1.ent.xml") seenRoot = out[i].entity == 0x3000;
	}
	CHECK(seenA);
	CHECK(seenB);
	CHECK(seenRoot);
	CHECK_EQ(EntityWalkCache(reinterpret_cast<const uint32_t *>(head), out, 1), 1u);
	char *emptyHead = Node(nullptr, nullptr, "", 0, true);
	const uint32_t self = Word(emptyHead);
	memcpy(emptyHead + 4, &self, 4);
	CHECK_EQ(EntityWalkCache(reinterpret_cast<const uint32_t *>(emptyHead), out, 8), 0u);
	CHECK_EQ(EntityWalkCache(nullptr, out, 8), 0u);
}
}

int main()
{
	ReadsEveryEntityOfTheTree();
	return test::Summary("entitywalk");
}
