// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "check.h"

#include <string>

#include "core/base64.h"

namespace {

std::string Decoded(const std::string &text)
{
	std::string out;
	return Base64UrlDecode(text, out) ? out : "(bad)";
}

void KnownValues()
{
	CHECK_EQ(Base64UrlEncode(""), std::string(""));
	CHECK_EQ(Base64UrlEncode("f"), std::string("Zg"));
	CHECK_EQ(Base64UrlEncode("fo"), std::string("Zm8"));
	CHECK_EQ(Base64UrlEncode("foo"), std::string("Zm9v"));
	CHECK_EQ(Base64UrlEncode("foob"), std::string("Zm9vYg"));
	CHECK_EQ(Base64UrlEncode("fooba"), std::string("Zm9vYmE"));
	CHECK_EQ(Base64UrlEncode("foobar"), std::string("Zm9vYmFy"));
	CHECK_EQ(Base64UrlEncode(std::string("\xfb\xff", 2)), std::string("-_8"));
	CHECK_EQ(Decoded("Zm9vYmFy"), std::string("foobar"));
	CHECK_EQ(Decoded("Zm9vYmE"), std::string("fooba"));
	CHECK_EQ(Decoded("-_8"), std::string("\xfb\xff", 2));
	CHECK_EQ(Decoded(""), std::string(""));
}

void Padding()
{
	CHECK_EQ(Decoded("Zg=="), std::string("f"));
	CHECK_EQ(Decoded("Zm8="), std::string("fo"));
	CHECK_EQ(Decoded("Zg="), std::string("f"));
	CHECK_EQ(Decoded("Zg==="), std::string("(bad)"));
	CHECK_EQ(Decoded("Z=g="), std::string("(bad)"));
}

void Garbage()
{
	CHECK_EQ(Decoded("Zm9v YmFy"), std::string("(bad)"));
	CHECK_EQ(Decoded("Zm9v+mFy"), std::string("(bad)"));
	CHECK_EQ(Decoded("Zm9v/mFy"), std::string("(bad)"));
	CHECK_EQ(Decoded("Zm9vY"), std::string("(bad)"));
	CHECK_EQ(Decoded("Zh"), std::string("(bad)"));
	CHECK_EQ(Decoded("\xc3\xa9"), std::string("(bad)"));
}

void EveryByte()
{
	std::string all;
	for (int i = 0; i < 256; i++) all += static_cast<char>(i);
	for (size_t len = 0; len <= all.size(); len += 37) {
		const std::string part = all.substr(0, len);
		const std::string text = Base64UrlEncode(part);
		CHECK(text.find_first_of("+/=") == std::string::npos);
		CHECK_EQ(Decoded(text), part);
	}
	CHECK_EQ(Decoded(Base64UrlEncode(all)), all);
}

}

int main()
{
	KnownValues();
	Padding();
	Garbage();
	EveryByte();
	return test::Summary("base64");
}
