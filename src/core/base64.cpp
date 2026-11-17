// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "core/base64.h"

#include <cstdint>

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int ValueOf(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '-') return 62;
	if (c == '_') return 63;
	return -1;
}

}

std::string Base64UrlEncode(const std::string &data)
{
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 3 <= data.size(); i += 3) {
		const uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8 |
			static_cast<unsigned char>(data[i + 2]);
		out += kAlphabet[(v >> 18) & 63];
		out += kAlphabet[(v >> 12) & 63];
		out += kAlphabet[(v >> 6) & 63];
		out += kAlphabet[v & 63];
	}
	const size_t rest = data.size() - i;
	if (rest == 1) {
		const uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
		out += kAlphabet[(v >> 18) & 63];
		out += kAlphabet[(v >> 12) & 63];
	}
	else if (rest == 2) {
		const uint32_t v = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
		out += kAlphabet[(v >> 18) & 63];
		out += kAlphabet[(v >> 12) & 63];
		out += kAlphabet[(v >> 6) & 63];
	}
	return out;
}

bool Base64UrlDecode(const std::string &text, std::string &out)
{
	out.clear();
	size_t n = text.size();
	for (int pad = 0; pad < 2 && n > 0 && text[n - 1] == '='; pad++) n--;
	if (n % 4 == 1) return false;
	out.reserve(n / 4 * 3 + 2);
	uint32_t bits = 0;
	int count = 0;
	for (size_t i = 0; i < n; i++) {
		const int v = ValueOf(text[i]);
		if (v < 0) return false;
		bits = bits << 6 | static_cast<uint32_t>(v);
		count += 6;
		if (count >= 8) {
			count -= 8;
			out += static_cast<char>((bits >> count) & 0xff);
		}
	}
	if (count > 0 && (bits & ((1u << count) - 1)) != 0) return false;
	return true;
}
