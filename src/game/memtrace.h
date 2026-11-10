// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>

// Who allocated the large blocks (64 KB and more) that are alive: the game's malloc, realloc and calloc (and the heap functions of Windows for the
// blocks of 128 KB and more) remember, for each large block, which
// game it was made in (the count of LoadGameData calls) and a few return addresses of the code that asked for it. The census (memcensus.cpp)
// asks about every large block it finds in a heap and the report groups them, so a block that outlives its game shows up with the game that
// made it and the code that made it. Only blocks of 64 KB or more are followed: they are few per game, and the hooks only compare a size
// for the rest. Reads and notes only; nothing of the game changes.
const size_t kMemTraceLarge = 0x10000;

void InstallMemTrace(bool knownExe);
// A game's data is being loaded: the blocks made from here on belong to the next game number.
void MemTraceNewGame();
// The census starts a walk of the heaps: forget the groups of the last one.
void MemTraceBegin();
// The census found a block of kMemTraceLarge or more in use, in the heap with that number (called with a heap locked: it does not allocate).
void MemTraceNote(const void *block, size_t size, unsigned heap);
// Writes what the walk found: how many large blocks are alive from each game, and the code that made the biggest groups.
void MemTraceReport(const char *when);
