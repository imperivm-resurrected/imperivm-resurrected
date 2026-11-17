// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "mods/rletail.h"

#include <cstring>

namespace {
const char kMagic[8] = {'I', 'M', 'P', 'R', 'T', 'A', 'I', 'L'};
const uint32_t kVersion = 1;

uint32_t Read32(const unsigned char *p)
{
	return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint64_t Read64(const unsigned char *p)
{
	return static_cast<uint64_t>(Read32(p)) | static_cast<uint64_t>(Read32(p + 4)) << 32;
}

void Put32(std::string &out, uint32_t v)
{
	for (int i = 0; i < 4; i++) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void Put64(std::string &out, uint64_t v)
{
	Put32(out, static_cast<uint32_t>(v));
	Put32(out, static_cast<uint32_t>(v >> 32));
}

int HexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}
}

bool RleTailParse(const unsigned char *data, size_t size, RleTailHeader &out)
{
	if (!data || size < kRleTailHeaderSize) return false;
	if (memcmp(data, kMagic, sizeof(kMagic)) != 0) return false;
	if (Read32(data + 8) != kVersion || Read32(data + 12) != kRleTailHeaderSize) return false;
	out.baseSize = Read64(data + 16);
	if (!out.baseSize) return false;
	static const char *digits = "0123456789abcdef";
	out.baseSha256.clear();
	for (int i = 0; i < 32; i++) {
		out.baseSha256 += digits[data[24 + i] >> 4];
		out.baseSha256 += digits[data[24 + i] & 15];
	}
	return true;
}

std::string RleTailMakeHeader(uint64_t baseSize, const std::string &baseSha256)
{
	if (baseSha256.size() != 64) return "";
	std::string out(kMagic, sizeof(kMagic));
	Put32(out, kVersion);
	Put32(out, static_cast<uint32_t>(kRleTailHeaderSize));
	Put64(out, baseSize);
	for (size_t i = 0; i < 64; i += 2) {
		const int hi = HexValue(baseSha256[i]), lo = HexValue(baseSha256[i + 1]);
		if (hi < 0 || lo < 0) return "";
		out += static_cast<char>(hi << 4 | lo);
	}
	return out;
}

int64_t RleTailFileOffset(uint64_t baseSize, uint64_t tailFileSize, uint64_t offset)
{
	if (tailFileSize < kRleTailHeaderSize || offset < baseSize) return -1;
	const uint64_t inside = offset - baseSize;
	if (inside >= tailFileSize - kRleTailHeaderSize) return -1;
	return static_cast<int64_t>(kRleTailHeaderSize + inside);
}

std::string RleCacheKey(const RleCacheSource &s)
{
	std::string path;
	for (char c : s.tailPath) path += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c == '/' ? '\\' : c;
	return "base " + std::to_string(s.baseSize) + " " + std::to_string(s.baseTime) + "\ntail " + path + " " + std::to_string(s.tailSize) + " " +
		std::to_string(s.tailTime) + "\n";
}

uint64_t RleCombinedSize(const RleCacheSource &s)
{
	return s.tailSize < kRleTailHeaderSize ? s.baseSize : s.baseSize + s.tailSize - kRleTailHeaderSize;
}
