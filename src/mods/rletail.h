// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

const char kRleTailFile[] = "rle.mmp.tail";
const size_t kRleTailHeaderSize = 56;

struct RleTailHeader {
	uint64_t baseSize = 0;
	std::string baseSha256;
};

bool RleTailParse(const unsigned char *data, size_t size, RleTailHeader &out);
std::string RleTailMakeHeader(uint64_t baseSize, const std::string &baseSha256);

struct RleCacheSource {
	uint64_t baseSize = 0;
	uint64_t baseTime = 0;
	std::string tailPath;
	uint64_t tailSize = 0;
	uint64_t tailTime = 0;
};

// Where byte `offset` of the game's rle.mmp-with-a-tail is in the tail file: the tail goes after the game's rle.mmp (baseSize bytes), and its bytes start after
// the header in its file. -1 when the offset is in the game's own part, or past the end of the tail.
int64_t RleTailFileOffset(uint64_t baseSize, uint64_t tailFileSize, uint64_t offset);

std::string RleCacheKey(const RleCacheSource &s);
uint64_t RleCombinedSize(const RleCacheSource &s);
