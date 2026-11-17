// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
// The reader of the game's file cache against a tree made with the layout of the game's VS2003 std::map and std::string. The game is a 32-bit process, so the
// tree can only be built with addresses below 4 GB: on a 32-bit build every address is, on Linux the memory is asked with MAP_32BIT, and where neither holds
// the test says so and passes.
#include "check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/artwalk.h"

#ifdef __linux__
#include <sys/mman.h>
#endif

namespace {
bool g_fits = true;

// Zeroed memory whose address fits in 32 bits (the words of the game's tree), or any if it cannot be had (then g_fits is false).
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

// A node of the by-entry map: left, parent, right, key (the entry), value (the by-name node), colour, nil.
uint32_t *Node(uint32_t *left, uint32_t *right, uint32_t *entry, char *byName, bool nil)
{
	uint32_t *n = static_cast<uint32_t *>(Alloc(0x18));
	n[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(left));
	n[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(right));
	n[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry));
	n[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(byName));
	reinterpret_cast<char *>(n)[0x15] = nil ? 1 : 0;
	return n;
}

// A node of the by-name map with the name in the string: inline under 16 characters (capacity 15), behind a pointer from 16.
char *NameNode(const std::string &name)
{
	char *n = static_cast<char *>(Alloc(0x30));
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
		const uint32_t pointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
		memcpy(n + 0x10, &pointer, 4);
	}
	return n;
}

uint32_t *Entry(int refs)
{
	uint32_t *e = static_cast<uint32_t *>(Alloc(8));
	e[1] = static_cast<uint32_t>(refs);
	return e;
}

void ReadsEveryEntryOfTheTree()
{
	const std::string longName = "gameres/cmdbar/actions/a very long name of a command button.bmp";
	uint32_t *nil = Node(nullptr, nullptr, nullptr, nullptr, true);
	uint32_t *leftLeaf = Node(nil, nil, Entry(0), NameNode("gameres/icons/a.bmp"), false);
	uint32_t *rightLeaf = Node(nil, nil, Entry(3), NameNode(longName), false);
	uint32_t *root = Node(leftLeaf, rightLeaf, Entry(1), NameNode("UI/Icons/B.bmp"), false);
	uint32_t *head = Node(leftLeaf, rightLeaf, nullptr, nullptr, true);
	head[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root));
	if (!g_fits) {
		std::printf("artwalk: the tree needs addresses below 4 GB, skipped\n");
		return;
	}
	ArtRawEntry out[8];
	const unsigned n = ArtWalkFileCache(head, out, 8);
	CHECK_EQ(n, 3u);
	bool a = false, b = false, c = false;
	for (unsigned i = 0; i < n; i++) {
		const std::string name = out[i].name;
		if (name == "gameres/icons/a.bmp") a = out[i].refs == 0;
		if (name == "UI/Icons/B.bmp") b = out[i].refs == 1;
		if (name == longName) c = out[i].refs == 3;
	}
	CHECK(a);
	CHECK(b);
	CHECK(c);
	// The capacity is respected, an empty cache has no entries, and a missing head is nothing.
	CHECK_EQ(ArtWalkFileCache(head, out, 2), 2u);
	uint32_t *emptyHead = Node(nullptr, nullptr, nullptr, nullptr, true);
	emptyHead[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(emptyHead));
	CHECK_EQ(ArtWalkFileCache(emptyHead, out, 8), 0u);
	CHECK_EQ(ArtWalkFileCache(nullptr, out, 8), 0u);
}
}

int main()
{
	ReadsEveryEntryOfTheTree();
	return test::Summary("artwalk");
}
