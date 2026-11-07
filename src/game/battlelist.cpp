// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/battlelist.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

uint32_t U32(const unsigned char *p) { return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24); }

bool EndsWithNoCase(const std::string &s, const char *tail)
{
	const size_t n = strlen(tail);
	return s.size() >= n && _stricmp(s.c_str() + s.size() - n, tail) == 0;
}

std::string Trim(const std::string &s)
{
	size_t a = 0, z = s.size();
	while (a < z && isspace(static_cast<unsigned char>(s[a]))) a++;
	while (z > a && isspace(static_cast<unsigned char>(s[z - 1]))) z--;
	return s.substr(a, z - a);
}

}

bool DecodeBattleBmp(const unsigned char *data, size_t size, BattleImage &out)
{
	if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') return false;
	const uint32_t offset = U32(data + 10), header = U32(data + 14);
	if (header < 40 || 14 + static_cast<size_t>(header) > size) return false;
	const int w = static_cast<int>(U32(data + 18)), rawH = static_cast<int>(U32(data + 22));
	const int bpp = data[28] | data[29] << 8;
	const uint32_t compression = U32(data + 30);
	if (w <= 0 || rawH == 0 || w > 8192 || std::abs(rawH) > 8192 || (compression != 0 && !(compression == 3 && (bpp == 16 || bpp == 32)))) return false;
	if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return false;
	// 16-bit pictures are 5-5-5 unless their masks say 5-6-5.
	const bool green6 = bpp == 16 && compression == 3 && 14 + static_cast<size_t>(header) + 12 <= size && U32(data + 14 + 40 + 4) == 0x7e0;
	const int h = std::abs(rawH);
	const size_t stride = (static_cast<size_t>(w) * bpp / 8 + 3) & ~static_cast<size_t>(3);
	if (offset > size || stride * h > size - offset) return false;
	uint32_t palette[256] = {};
	if (bpp == 8) {
		uint32_t colors = U32(data + 46);
		if (!colors || colors > 256) colors = 256;
		const size_t at = 14 + header;
		if (at + colors * 4 > offset) return false;
		for (uint32_t i = 0; i < colors; i++) palette[i] = 0xff000000u | (U32(data + at + i * 4) & 0xffffff);
	}
	out.w = w;
	out.h = h;
	out.bgra.assign(static_cast<size_t>(w) * h, 0);
	for (int y = 0; y < h; y++) {
		const unsigned char *row = data + offset + stride * (rawH > 0 ? h - 1 - y : y);
		uint32_t *dst = &out.bgra[static_cast<size_t>(y) * w];
		for (int x = 0; x < w; x++) {
			if (bpp == 8) dst[x] = palette[row[x]];
			else if (bpp == 16) {
				const unsigned v = row[x * 2] | row[x * 2 + 1] << 8;
				const unsigned r = green6 ? v >> 11 & 31 : v >> 10 & 31, g = green6 ? (v >> 5 & 63) * 255 / 63 : (v >> 5 & 31) * 255 / 31, b = v & 31;
				dst[x] = 0xff000000u | (r * 255 / 31) << 16 | g << 8 | b * 255 / 31;
			}
			else if (bpp == 24) dst[x] = 0xff000000u | row[x * 3] | row[x * 3 + 1] << 8 | row[x * 3 + 2] << 16;
			else dst[x] = 0xff000000u | (U32(row + x * 4) & 0xffffff);
		}
	}
	return true;
}

BattleTitle SplitBattleTitle(const std::string &title)
{
	BattleTitle t;
	t.name = Trim(title);
	// A hyphen or an en dash (UTF-8 or the game's 1252) between spaces, followed by a short tail with a number in it.
	static const char *const kDashes[] = {" - ", " \xe2\x80\x93 ", " \x96 "};
	size_t best = std::string::npos, len = 0;
	for (const char *dash : kDashes) {
		const size_t at = t.name.rfind(dash);
		if (at != std::string::npos && (best == std::string::npos || at > best)) {
			best = at;
			len = strlen(dash);
		}
	}
	if (best == std::string::npos || best == 0) return t;
	const std::string tail = Trim(t.name.substr(best + len));
	bool digit = false;
	for (char c : tail) digit = digit || isdigit(static_cast<unsigned char>(c));
	if (!digit || tail.size() > 16) return t;
	t.year = tail;
	t.name = Trim(t.name.substr(0, best));
	return t;
}

std::string BattleIntroPattern(const std::string &adventurePath)
{
	std::string path = adventurePath;
	for (char &c : path) {
		if (c == '\\') c = '/';
	}
	const size_t slash = path.find_last_of('/');
	if (slash == std::string::npos) return "";
	std::string dir = path.substr(0, slash), file = path.substr(slash + 1);
	if (EndsWithNoCase(file, ".bfhp")) file.resize(file.size() - 5);
	const char kind = EndsWithNoCase(dir, "GreatBattles") ? 'B' : EndsWithNoCase(dir, "GreatChallenges") ? 'L' : 0;
	const size_t under = file.find_last_of('_');
	if (!kind || under == std::string::npos || under + 1 >= file.size()) return "";
	return std::string("*_Intro_Great_") + kind + "_" + file.substr(under + 1) + ".avi";
}

BattleGame BattleGameOf(const std::string &adventurePath)
{
	const size_t slash = adventurePath.find_last_of("/\\");
	const std::string file = slash == std::string::npos ? adventurePath : adventurePath.substr(slash + 1);
	// The whole prefix with its spaced dash, so that "Imperivm II - " is never taken for "Imperivm I".
	if (_strnicmp(file.c_str(), "Imperivm I - ", 13) == 0) return kBattleGameImperivm1;
	if (_strnicmp(file.c_str(), "Imperivm II - ", 14) == 0) return kBattleGameImperivm2;
	return kBattleGameOther;
}

std::string BattleTitleWithoutGame(const std::string &title)
{
	const std::string t = Trim(title);
	for (const char *prefix : {"Imperivm I - ", "Imperivm II - "}) {
		const size_t n = strlen(prefix);
		if (t.size() > n && _strnicmp(t.c_str(), prefix, n) == 0) return Trim(t.substr(n));
	}
	return t;
}

std::string BattleDescriptionText(const std::string &raw)
{
	std::string out;
	out.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); i++) {
		if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'n') {
			out += '\n';
			i++;
		}
		else if (raw[i] != '\r') {
			out += raw[i];
		}
	}
	return Trim(out);
}
