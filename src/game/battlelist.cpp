// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/battlelist.h"

#include <algorithm>
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

// "adventures/GreatBattles/1_Great_Battles_Zama.bfhp" gives "1_Great_Battles_Zama".
std::string AdventureFileName(const std::string &adventurePath)
{
	const size_t slash = adventurePath.find_last_of("/\\");
	std::string file = slash == std::string::npos ? adventurePath : adventurePath.substr(slash + 1);
	if (EndsWithNoCase(file, ".bfhp")) file.resize(file.size() - 5);
	return file;
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

BattleCampaignPack BattleCampaignPackOf(const std::vector<BattlePackFiles> &packs, BattleGame game)
{
	BattleCampaignPack out;
	if (game == kBattleGameOther) return out;
	for (const BattlePackFiles &pack : packs) {
		if (out.active) break;
		if (out.installed && !pack.active) continue;
		for (const std::string &file : pack.files) {
			const bool adventure = file.size() > 11 && (_strnicmp(file.c_str(), "adventures/", 11) == 0 || _strnicmp(file.c_str(), "adventures\\", 11) == 0) &&
				EndsWithNoCase(file, ".bfhp");
			if (!adventure || BattleGameOf(file) != game) continue;
			out.installed = true;
			out.active = pack.active;
			out.folder = pack.folder;
			out.adventure = file;
			break;
		}
	}
	return out;
}

std::vector<BattleCampaignCover> BattleCampaignCovers(BattleGame game)
{
	static const BattleCampaignCover kImperivm1[] = {
		{"imperivm1-the-gallic-war.jpg", "Imperivm I - La guerra de las Galias", "The Gallic War"},
	};
	static const BattleCampaignCover kImperivm2[] = {
		{"imperivm2-hannibals-campaigns.jpg", "Imperivm II - Hannibal's Campaigns", "Hannibal's Campaigns"},
		{"imperivm2-numantia.jpg", "Imperivm II - Numantia", "Numantia"},
		{"imperivm2-the-punic-wars.jpg", "Imperivm II - The Punic Wars", "The Punic Wars"},
	};
	if (game == kBattleGameImperivm1) return {std::begin(kImperivm1), std::end(kImperivm1)};
	if (game == kBattleGameImperivm2) return {std::begin(kImperivm2), std::end(kImperivm2)};
	return {};
}

std::string BattleCampaignCoverOf(const std::string &adventurePath)
{
	const std::string file = AdventureFileName(adventurePath);
	for (BattleGame game : {kBattleGameImperivm1, kBattleGameImperivm2}) {
		for (const BattleCampaignCover &c : BattleCampaignCovers(game)) {
			if (_stricmp(file.c_str(), c.adventure) == 0) return c.file;
		}
	}
	return "";
}

std::string BattleRemasteredCoverOf(const std::string &adventurePath)
{
	static const char *const kGreat[] = {
		"1_Great_Battles_Zama", "2_Great_Battles_Numantia", "3_Great_Battles_Alesia", "4_Great_Battles_Egypt",
		"5_Great_Battles_Britain", "6_Great_Battles_Danube", "1_Great_Losses_Rome", "2_Great_loses_Spain",
		"3_Great_Losses_Egypt", "4_Great_Loses_Gaul", "5_Great_Loses_German", "6_Great_loses_Boudicca",
	};
	const std::string file = AdventureFileName(adventurePath);
	for (const char *name : kGreat) {
		if (_stricmp(file.c_str(), name) == 0) return std::string(name) + ".jpg";
	}
	return "";
}

namespace {

std::string OneLine(const std::string &s)
{
	std::string out = Trim(s);
	for (char &c : out) {
		if (c == '\t' || c == '\r' || c == '\n') c = ' ';
	}
	return out;
}

bool SameAdventure(const BattleLaurel &l, const std::string &path, const std::string &title)
{
	if (!path.empty() && !l.path.empty()) return _stricmp(path.c_str(), l.path.c_str()) == 0;
	return !title.empty() && l.title == title;
}

}

std::vector<BattleLaurel> BattleLaurelsDecode(const std::string &text)
{
	std::vector<BattleLaurel> out;
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		std::string line = text.substr(at, end - at);
		at = end + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
		if (t2 == std::string::npos || t1 != 1 || line[0] < '1' || line[0] > '3') continue;
		BattleLaurel l;
		l.level = line[0] - '0';
		l.path = line.substr(t1 + 1, t2 - t1 - 1);
		l.title = line.substr(t2 + 1);
		if (!l.path.empty() || !l.title.empty()) out.push_back(l);
	}
	return out;
}

std::string BattleLaurelsEncode(const std::vector<BattleLaurel> &laurels)
{
	std::string out;
	for (const BattleLaurel &l : laurels) {
		if (l.level < 1 || l.level > 3) continue;
		out += std::to_string(l.level) + "\t" + OneLine(l.path) + "\t" + OneLine(l.title) + "\n";
	}
	return out;
}

bool BattleLaurelsAdd(std::vector<BattleLaurel> &laurels, const std::string &pathIn, const std::string &titleIn, int level)
{
	const std::string path = OneLine(pathIn), title = OneLine(titleIn);
	if (level < 1 || level > 3 || (path.empty() && title.empty())) return false;
	for (BattleLaurel &l : laurels) {
		if (!SameAdventure(l, path, title)) continue;
		const BattleLaurel before = l;
		l.level = std::max(l.level, level);
		if (l.path.empty()) l.path = path;
		if (l.title.empty()) l.title = title;
		return l.level != before.level || l.path != before.path || l.title != before.title;
	}
	BattleLaurel l;
	l.path = path;
	l.title = title;
	l.level = level;
	laurels.push_back(l);
	return true;
}

int BattleLaurelOf(const std::vector<BattleLaurel> &laurels, const std::string &pathIn, const std::string &titleIn)
{
	const std::string path = OneLine(pathIn), title = OneLine(titleIn);
	int best = 0;
	for (const BattleLaurel &l : laurels) {
		if (SameAdventure(l, path, title) || (!title.empty() && l.title == title)) best = std::max(best, l.level);
	}
	return best;
}

std::string ConquestKeyOf(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	if (EndsWithNoCase(name, ".bfhp")) name.resize(name.size() - 5);
	name = OneLine(name);
	for (char &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return name;
}

std::vector<ConquestLaurel> ConquestLaurelsDecode(const std::string &text)
{
	std::vector<ConquestLaurel> out;
	for (const BattleLaurel &l : BattleLaurelsDecode(text)) {
		if (!l.path.empty()) out.push_back({l.path, l.title, l.level});
	}
	return out;
}

std::string ConquestLaurelsEncode(const std::vector<ConquestLaurel> &laurels)
{
	std::vector<BattleLaurel> lines;
	for (const ConquestLaurel &l : laurels) lines.push_back({l.conquest, l.territory, l.level});
	return BattleLaurelsEncode(lines);
}

bool ConquestLaurelsAdd(std::vector<ConquestLaurel> &laurels, const std::string &conquestIn, const std::string &territoryIn, int level)
{
	const std::string conquest = OneLine(conquestIn), territory = OneLine(territoryIn);
	if (level < 1 || level > 3 || conquest.empty()) return false;
	for (ConquestLaurel &l : laurels) {
		if (l.conquest != conquest || _stricmp(l.territory.c_str(), territory.c_str()) != 0) continue;
		if (l.level >= level) return false;
		l.level = level;
		return true;
	}
	laurels.push_back({conquest, territory, level});
	return true;
}

int ConquestLaurelOf(const std::vector<ConquestLaurel> &laurels, const std::string &conquest, const std::string &territory)
{
	int best = 0;
	for (const ConquestLaurel &l : laurels) {
		if (l.conquest == conquest && _stricmp(l.territory.c_str(), territory.c_str()) == 0) best = std::max(best, l.level);
	}
	return best;
}

std::string BattlePicturePath(const std::string &adventureFile)
{
	if (!EndsWithNoCase(adventureFile, ".bfhp")) return "";
	return adventureFile.substr(0, adventureFile.size() - 5) + ".bmp";
}

int BattleSaveAdventure(const std::string &saveBaseName, const std::vector<std::string> &titles, std::string *name)
{
	int best = -1;
	size_t bestLen = 0;
	for (size_t i = 0; i < titles.size(); i++) {
		const std::string &t = titles[i];
		if (t.empty() || t.size() <= bestLen || saveBaseName.size() < t.size() + 2) continue;
		const size_t at = saveBaseName.size() - t.size();
		if (saveBaseName[at - 1] != '-' || _strnicmp(saveBaseName.c_str() + at, t.c_str(), t.size()) != 0) continue;
		best = static_cast<int>(i);
		bestLen = t.size();
	}
	if (name) *name = best >= 0 ? saveBaseName.substr(0, saveBaseName.size() - bestLen - 1) : std::string();
	return best;
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

BattleImage ShrinkBattleImage(BattleImage image, int maxWidth)
{
	if (maxWidth <= 0 || image.w <= maxWidth || image.h <= 0 || image.bgra.size() < static_cast<size_t>(image.w) * image.h) return image;
	const int sw = image.w, sh = image.h;
	const int dw = maxWidth, dh = std::max(1, static_cast<int>((static_cast<long long>(sh) * dw + sw / 2) / sw));
	BattleImage out;
	out.w = dw;
	out.h = dh;
	out.bgra.resize(static_cast<size_t>(dw) * dh);
	for (int y = 0; y < dh; ++y) {
		const int y0 = static_cast<int>(static_cast<long long>(y) * sh / dh);
		const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * sh / dh));
		for (int x = 0; x < dw; ++x) {
			const int x0 = static_cast<int>(static_cast<long long>(x) * sw / dw);
			const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * sw / dw));
			uint32_t sum[4] = {0, 0, 0, 0};
			for (int sy = y0; sy < y1; ++sy) {
				const uint32_t *row = image.bgra.data() + static_cast<size_t>(sy) * sw;
				for (int sx = x0; sx < x1; ++sx) {
					const uint32_t px = row[sx];
					for (int c = 0; c < 4; ++c) sum[c] += (px >> (c * 8)) & 0xffu;
				}
			}
			const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
			uint32_t px = 0;
			for (int c = 0; c < 4; ++c) px |= ((sum[c] + n / 2) / n) << (c * 8);
			out.bgra[static_cast<size_t>(y) * dw + x] = px;
		}
	}
	return out;
}
