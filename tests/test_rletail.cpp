// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "mods/rletail.h"

namespace {

const std::string kSha = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

const unsigned char *Bytes(const std::string &s)
{
	return reinterpret_cast<const unsigned char *>(s.data());
}

void RoundTrip()
{
	const std::string h = RleTailMakeHeader(410048673, kSha);
	CHECK_EQ(h.size(), kRleTailHeaderSize);
	RleTailHeader out;
	CHECK(RleTailParse(Bytes(h), h.size(), out));
	CHECK_EQ(out.baseSize, static_cast<uint64_t>(410048673));
	CHECK_EQ(out.baseSha256, kSha);
	const std::string upper = RleTailMakeHeader(5, "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF");
	CHECK(RleTailParse(Bytes(upper), upper.size(), out));
	CHECK_EQ(out.baseSha256, kSha);
	const std::string big = RleTailMakeHeader(0x123456789ull, kSha);
	CHECK(RleTailParse(Bytes(big), big.size(), out));
	CHECK_EQ(out.baseSize, static_cast<uint64_t>(0x123456789ull));
}

void Rejects()
{
	RleTailHeader out;
	CHECK(RleTailMakeHeader(1, "abc").empty());
	CHECK(RleTailMakeHeader(1, std::string(64, 'z')).empty());
	std::string h = RleTailMakeHeader(100, kSha);
	CHECK(!RleTailParse(Bytes(h), h.size() - 1, out));
	CHECK(!RleTailParse(nullptr, 0, out));
	std::string bad = h;
	bad[0] = 'X';
	CHECK(!RleTailParse(Bytes(bad), bad.size(), out));
	bad = h;
	bad[8] = 2;
	CHECK(!RleTailParse(Bytes(bad), bad.size(), out));
	bad = h;
	bad[12] = 57;
	CHECK(!RleTailParse(Bytes(bad), bad.size(), out));
	const std::string zero = RleTailMakeHeader(0, kSha);
	CHECK(!RleTailParse(Bytes(zero), zero.size(), out));
}

void Cache()
{
	RleCacheSource a;
	a.baseSize = 410048673;
	a.baseTime = 1000;
	a.tailPath = "C:/Game/mods/Caesar/RLE.MMP.TAIL";
	a.tailSize = 60114799;
	a.tailTime = 2000;
	RleCacheSource b = a;
	b.tailPath = "c:\\game\\mods\\caesar\\rle.mmp.tail";
	CHECK_EQ(RleCacheKey(a), RleCacheKey(b));
	b.tailTime = 2001;
	CHECK(RleCacheKey(a) != RleCacheKey(b));
	b = a;
	b.baseTime = 999;
	CHECK(RleCacheKey(a) != RleCacheKey(b));
	CHECK_EQ(RleCombinedSize(a), static_cast<uint64_t>(410048673 + 60114799 - kRleTailHeaderSize));
	a.tailSize = 10;
	CHECK_EQ(RleCombinedSize(a), static_cast<uint64_t>(410048673));
}

}

// An offset of the game's rle.mmp with a tail: below the game's own size it is the game's, then the tail's bytes after its header, and nothing past its end.
void TheTailsBytesAreWhereTheGameLooksForThem()
{
	const uint64_t base = 410048673, file = kRleTailHeaderSize + 1000;
	CHECK(RleTailFileOffset(base, file, base - 1) == static_cast<int64_t>(-1));
	CHECK(RleTailFileOffset(base, file, 0) == static_cast<int64_t>(-1));
	CHECK(RleTailFileOffset(base, file, base) == static_cast<int64_t>(kRleTailHeaderSize));
	CHECK(RleTailFileOffset(base, file, base + 999) == static_cast<int64_t>(kRleTailHeaderSize + 999));
	CHECK(RleTailFileOffset(base, file, base + 1000) == static_cast<int64_t>(-1));
	CHECK(RleTailFileOffset(base, kRleTailHeaderSize, base) == static_cast<int64_t>(-1));
	CHECK(RleTailFileOffset(base, 10, base) == static_cast<int64_t>(-1));
	// past 4 GB, which the game cannot ask for, is still answered
	CHECK(RleTailFileOffset(0x100000000ull, file, 0x100000000ull + 5) == static_cast<int64_t>(kRleTailHeaderSize + 5));
}

int main()
{
	TheTailsBytesAreWhereTheGameLooksForThem();
	RoundTrip();
	Rejects();
	Cache();
	return test::Summary("rletail");
}
