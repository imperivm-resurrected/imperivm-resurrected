// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"
#include "game/civs.h"

#include <string>
#include <vector>

int main()
{
	Civ c;
	c.index = 10;
	for (const char *prefix : {"D", "d", "0", "2", "9", "Ga", "Gal", "Dac", "DaC", "Dac2", "A123"}) CHECK(CivSetPrefix(c, prefix));
	for (const char *prefix : {"", "dacia", "dAc", "dac", "2D", "22", "D_ac", "D-", " D", "D ", "É", "D\n"}) CHECK(!CivSetPrefix(c, prefix));
	CHECK(!CivSetPrefix(c, std::string("D\0a", 3)));
	CHECK(CivSetPrefix(c, "d"));
	CHECK_EQ(c.letterUp, std::string("D"));
	CHECK_EQ(c.letterLow, std::string("d"));
	CHECK(CivSetPrefix(c, "Dac"));
	CHECK_EQ(c.letterUp, std::string("Dac"));
	CHECK_EQ(c.letterLow, std::string("dac"));
	CHECK(c.ownLetter);
	Civ first = c;
	first.modFolder = "first-pack";
	CHECK(CivSetPrefix(c, "DaC"));
	CHECK_EQ(c.letterUp, std::string("DaC"));
	CHECK_EQ(CivPrefixClash(c, {&first}), first.index);
	CHECK_EQ(CivPrefixClash(c, {}), -1);
	CHECK(CivSetPrefix(c, "Gac"));
	CHECK(c.ownLetter); // Sharing the first character with the base is not inheritance.
	CHECK_EQ(CivPrefixClash(c, {&first}), -1);
	CHECK(CivSetPrefix(c, "Ga"));
	first = c;
	CHECK(CivSetPrefix(c, "Gal"));
	CHECK_EQ(CivPrefixClash(c, {&first}), -1);
	for (int b = 0; b < 8; b++) {
		c.base = b;
		CHECK(CivSetPrefix(c, std::string(1, kGameCivPrefixes[b])));
		CHECK(!c.ownLetter);
		first = c;
		CHECK_EQ(CivPrefixClash(c, {&first}), -1); // Variants may inherit their own base.
		c.base = (b + 1) % 8;
		CHECK(CivSetPrefix(c, std::string(1, kGameCivPrefixes[b])));
		CHECK(c.ownLetter);
		CHECK_EQ(CivPrefixClash(c, {}), b); // Reserved even without a loaded mod owner.
	}
	c.base = 0;
	CHECK(CivSetPrefix(c, "2"));
	first = c;
	CHECK_EQ(CivPrefixClash(c, {&first}), first.index);
	CHECK_EQ(c.letterUp, std::string("2"));
	CHECK_EQ(c.letterLow, std::string("2"));
	return test::Summary("civprefix");
}
