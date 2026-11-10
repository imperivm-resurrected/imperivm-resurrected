// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/roomname.h"

namespace {

// Length of the UTF-8 character that starts at s[i], or 0 when it is not a valid one.
size_t CharLength(const std::string &s, size_t i)
{
	const unsigned char c = static_cast<unsigned char>(s[i]);
	size_t n = 0;
	unsigned min = 0;
	if (c < 0x80) return 1;
	if (c >= 0xc2 && c <= 0xdf) {
		n = 2;
		min = 0x80;
	}
	else if (c >= 0xe0 && c <= 0xef) {
		n = 3;
		min = 0x800;
	}
	else if (c >= 0xf0 && c <= 0xf4) {
		n = 4;
		min = 0x10000;
	}
	else {
		return 0;
	}
	if (i + n > s.size()) return 0;
	unsigned cp = c & (0x7f >> n);
	for (size_t k = 1; k < n; k++) {
		const unsigned char cc = static_cast<unsigned char>(s[i + k]);
		if ((cc & 0xc0) != 0x80) return 0;
		cp = (cp << 6) | (cc & 0x3f);
	}
	if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
	return n;
}

void TrimSpaces(std::string &s)
{
	while (!s.empty() && s.back() == ' ') s.pop_back();
	size_t lead = 0;
	while (lead < s.size() && s[lead] == ' ') lead++;
	s.erase(0, lead);
}

}

std::string RoomNameClean(const std::string &name, size_t maxBytes)
{
	std::string clean;
	for (size_t i = 0; i < name.size();) {
		const size_t n = CharLength(name, i);
		if (!n) {
			i++;
			continue;
		}
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (n == 1 && (c < 0x20 || c == 0x7f)) {
			if (c == '\t' || c == '\n' || c == '\r') clean += ' ';
		}
		else {
			clean.append(name, i, n);
		}
		i += n;
	}
	TrimSpaces(clean);
	if (clean.size() > maxBytes) {
		size_t cut = 0;
		for (size_t i = 0; i < clean.size();) {
			const size_t n = CharLength(clean, i);
			if (i + n > maxBytes) break;
			i += n;
			cut = i;
		}
		clean.resize(cut);
		TrimSpaces(clean);
	}
	return clean;
}
