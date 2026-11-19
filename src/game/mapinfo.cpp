// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/mapinfo.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "core/hpfs.h"

namespace {
const char kGridMagic[4] = {'D', 'I', 'R', 'G'};
const size_t kGridHeader = 20;
const int kMaxSide = 1024;
const int kPlayerSlots = 8;

uint32_t Le32(const unsigned char *p)
{
	return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

std::string Text(const std::vector<unsigned char> &bytes) { return std::string(bytes.begin(), bytes.end()); }

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string Attr(const std::string &tag, const char *name)
{
	const std::string key = std::string(name) + "=\"";
	for (size_t at = tag.find(key); at != std::string::npos; at = tag.find(key, at + 1)) {
		if (at > 0 && !IsSpace(tag[at - 1]) && tag[at - 1] != '<') continue;
		const size_t from = at + key.size();
		const size_t to = tag.find('"', from);
		return to == std::string::npos ? std::string() : tag.substr(from, to - from);
	}
	return std::string();
}

bool Tag(const std::string &xml, const char *name, size_t &from, std::string &tag)
{
	const std::string open = std::string("<") + name;
	for (size_t at = xml.find(open, from); at != std::string::npos; at = xml.find(open, at + 1)) {
		const size_t after = at + open.size();
		if (after < xml.size() && !IsSpace(xml[after]) && xml[after] != '/' && xml[after] != '>') continue;
		const size_t end = xml.find('>', at);
		if (end == std::string::npos) return false;
		tag = xml.substr(at, end - at + 1);
		from = end + 1;
		return true;
	}
	return false;
}

int ToInt(const std::string &s, int fallback = 0)
{
	if (s.empty()) return fallback;
	char *end = nullptr;
	const long v = strtol(s.c_str(), &end, 10);
	return end == s.c_str() ? fallback : static_cast<int>(v);
}

bool ReadGrid(const HpfsFile &file, const std::string &path, int &cols, int &rows, int &units, std::vector<uint8_t> &cells)
{
	const HpfsEntry *entry = file.Find(path);
	std::vector<unsigned char> bytes;
	if (!entry || !file.Read(*entry, bytes, 8u << 20) || bytes.size() < kGridHeader) return false;
	if (memcmp(bytes.data(), kGridMagic, sizeof(kGridMagic)) != 0) return false;
	const uint32_t cell = Le32(bytes.data() + 4), bits = Le32(bytes.data() + 8), width = Le32(bytes.data() + 12), height = Le32(bytes.data() + 16);
	if (bits != 8 || cell == 0 || width % cell || height % cell) return false;
	cols = static_cast<int>(width / cell);
	rows = static_cast<int>(height / cell);
	if (cols < 1 || rows < 1 || cols > kMaxSide || rows > kMaxSide) return false;
	if (bytes.size() < kGridHeader + static_cast<size_t>(cols) * rows) return false;
	units = static_cast<int>(width);
	cells.assign(bytes.begin() + kGridHeader, bytes.begin() + kGridHeader + static_cast<size_t>(cols) * rows);
	return true;
}

const double kCameraYScale = 1.4142;

bool ContainsNoCase(const std::string &text, const char *word)
{
	const std::string w = word;
	for (size_t i = 0; i + w.size() <= text.size(); i++) {
		size_t k = 0;
		while (k < w.size() && tolower(static_cast<unsigned char>(text[i + k])) == tolower(static_cast<unsigned char>(w[k]))) k++;
		if (k == w.size()) return true;
	}
	return false;
}

int SettlementRank(const std::string &firstBuilding)
{
	if (ContainsNoCase(firstBuilding, "townhall") || ContainsNoCase(firstBuilding, "stronghold") || ContainsNoCase(firstBuilding, "fortress")) return 3;
	if (ContainsNoCase(firstBuilding, "village") || ContainsNoCase(firstBuilding, "outpost")) return 1;
	return 2;
}

bool SettlementPosition(const std::string &objects, int player, int &x, int &y)
{
	int bestRank = 0;
	size_t at = 0;
	for (;;) {
		const size_t open = objects.find("<settlement", at);
		if (open == std::string::npos) break;
		size_t close = objects.find("</settlement>", open);
		if (close == std::string::npos) close = objects.size();
		at = close;
		size_t from = open;
		std::string head;
		if (!Tag(objects, "settlement", from, head) || ToInt(Attr(head, "player"), -1) != player + 1) continue;
		const std::string first = Attr(head, "classoffirstbuilding");
		const int rank = SettlementRank(first);
		if (rank <= bestRank) continue;
		std::string tag;
		int fx = 0, fy = 0;
		bool have = false;
		while (from < close && Tag(objects, "scriptobj", from, tag)) {
			if (from > close) break;
			const int ox = ToInt(Attr(tag, "x"), -1), oy = ToInt(Attr(tag, "y"), -1);
			if (ox < 0 || oy < 0) continue;
			if (!have) {
				fx = ox;
				fy = oy;
				have = true;
			}
			if (!first.empty() && Attr(tag, "class") == first) {
				fx = ox;
				fy = oy;
				break;
			}
		}
		if (!have) continue;
		x = fx;
		y = fy;
		bestRank = rank;
	}
	return bestRank > 0;
}

void ReadPlayers(const HpfsFile &file, const std::string &mapObjects, MapInfo &out)
{
	bool anySettlement = false;
	std::vector<std::array<int, 3>> cameras;
	for (int slot = 0; slot < kPlayerSlots; slot++) {
		const HpfsEntry *entry = file.Find("player" + std::to_string(slot) + ".xml");
		std::vector<unsigned char> bytes;
		if (!entry || !file.Read(*entry, bytes, 1u << 20)) continue;
		const std::string xml = Text(bytes);
		size_t from = 0;
		std::string tag;
		if (!Tag(xml, "playerdata", from, tag)) continue;
		const std::string control = Attr(tag, "control");
		if (_stricmp(control.c_str(), "Disabled") == 0) continue;
		MapPlayer p;
		p.slot = slot;
		p.race = Attr(tag, "race");
		p.human = _stricmp(control.c_str(), "Computer") != 0;
		p.color = MapRgb555(static_cast<unsigned>(ToInt(Attr(tag, "color"))));
		if (SettlementPosition(mapObjects, slot, p.x, p.y)) {
			p.hasStart = true;
			anySettlement = true;
		}
		else {
			cameras.push_back({static_cast<int>(out.players.size()), ToInt(Attr(tag, "startx")), ToInt(Attr(tag, "starty"))});
		}
		out.players.push_back(p);
	}
	if (anySettlement) return;
	for (const auto &cam : cameras) {
		if (cam[1] <= 0 && cam[2] <= 0) continue;
		MapPlayer &p = out.players[cam[0]];
		p.x = cam[1];
		p.y = static_cast<int>(cam[2] * kCameraYScale + 0.5);
		p.hasStart = true;
	}
}
}

std::vector<MapLayerDef> MapLayerDefs(const std::string &xml)
{
	std::vector<MapLayerDef> out;
	size_t from = 0;
	std::string tag;
	while (Tag(xml, "layer", from, tag)) {
		MapLayerDef d;
		const std::string z = Attr(tag, "z");
		if (z.empty()) continue;
		d.z = ToInt(z, -1);
		if (d.z < 0 || d.z > 255) continue;
		d.type = ToInt(Attr(tag, "type"));
		d.tile = Attr(tag, "minimap");
		out.push_back(d);
	}
	return out;
}

std::string MapLayerTilePath(const MapLayerDef &layer, const std::string &season)
{
	std::string tile = layer.tile;
	const size_t at = tile.find("%season%");
	if (at != std::string::npos) tile.replace(at, 8, season.empty() ? std::string("spring") : season);
	for (char &ch : tile) ch = ch == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(ch)));
	return tile;
}

uint32_t MapRgb555(unsigned value)
{
	const unsigned r = (value >> 10) & 31, g = (value >> 5) & 31, b = value & 31;
	return ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
}

bool BmpAverageColor(const unsigned char *data, size_t size, uint32_t &rgb)
{
	if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') return false;
	const uint32_t offset = Le32(data + 10), width = Le32(data + 18);
	const int32_t rawHeight = static_cast<int32_t>(Le32(data + 22));
	const unsigned bits = data[28] | data[29] << 8;
	const uint32_t compression = Le32(data + 30);
	const uint32_t height = rawHeight < 0 ? static_cast<uint32_t>(-rawHeight) : static_cast<uint32_t>(rawHeight);
	if (bits != 24 || compression != 0 || width < 1 || height < 1 || width > 4096 || height > 4096) return false;
	const size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
	if (offset + stride * height > size) return false;
	uint64_t sum[3] = {};
	for (uint32_t y = 0; y < height; y++) {
		const unsigned char *row = data + offset + stride * y;
		for (uint32_t x = 0; x < width; x++) {
			sum[0] += row[x * 3 + 2];
			sum[1] += row[x * 3 + 1];
			sum[2] += row[x * 3];
		}
	}
	const uint64_t n = static_cast<uint64_t>(width) * height;
	rgb = static_cast<uint32_t>(sum[0] / n) << 16 | static_cast<uint32_t>(sum[1] / n) << 8 | static_cast<uint32_t>(sum[2] / n);
	return true;
}

bool MapInfoParse(const unsigned char *data, size_t size, MapInfo &out)
{
	out = MapInfo();
	HpfsFile file;
	if (!file.Open(data, size)) return false;
	std::vector<unsigned char> bytes;
	if (const HpfsEntry *game = file.Find("game.xml")) {
		if (file.Read(*game, bytes, 1u << 20)) {
			const std::string xml = Text(bytes);
			size_t from = 0;
			std::string tag;
			if (Tag(xml, "properties", from, tag)) {
				out.name = Attr(tag, "name");
				out.victory = Attr(tag, "victory_condition");
				out.season = Attr(tag, "season");
				out.singleOnly = ToInt(Attr(tag, "single_only")) != 0;
				std::string text = Attr(tag, "description");
				for (size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at + 1)) text.replace(at, 2, "\n");
				while (!text.empty() && (text[0] == '\n' || text[0] == ' ')) text.erase(0, 1);
				out.description = text;
				out.gameType = ToInt(Attr(tag, "game_type"));
			}
			from = 0;
			int startMap = 1;
			if (Tag(xml, "properties", from, tag)) startMap = std::max(1, ToInt(Attr(tag, "start_map"), 1));
			const std::string base = "Maps/" + std::to_string(startMap) + "/";
			std::string mapObjects;
			if (const HpfsEntry *objects = file.Find(base + "map.obj.xml")) {
				if (file.Read(*objects, bytes, 8u << 20)) mapObjects = Text(bytes);
			}
			ReadPlayers(file, mapObjects, out);
			int cols = 0, rows = 0, units = 0;
			std::vector<uint8_t> cells;
			if (ReadGrid(file, base + "Terrain.terrain.grid", cols, rows, units, cells)) {
				out.cols = cols;
				out.rows = rows;
				out.units = units;
				out.terrain = std::move(cells);
				int hc = 0, hr = 0, hu = 0;
				std::vector<uint8_t> heights;
				if (ReadGrid(file, base + "Terrain.height.grid", hc, hr, hu, heights) && hu == units && hc >= cols && hr >= rows) {
					out.heightCols = hc;
					out.heightRows = hr;
					out.height = std::move(heights);
				}
			}
		}
	}
	out.ok = !out.name.empty() || out.cols > 0 || !out.players.empty();
	return out.ok;
}

void MapInfoDropGrids(MapInfo &info)
{
	std::vector<uint8_t>().swap(info.terrain);
	std::vector<uint8_t>().swap(info.height);
}

MapImage MapRender(const MapInfo &info, const std::vector<MapLayerColor> &layers, int maxSide)
{
	MapImage image;
	if (info.cols < 1 || info.rows < 1 || info.terrain.size() < static_cast<size_t>(info.cols) * info.rows || maxSide < 8) return image;
	const int factor = std::max(1, (std::max(info.cols, info.rows) + maxSide - 1) / maxSide);
	image.w = info.cols / factor;
	image.h = info.rows / factor;
	if (image.w < 1 || image.h < 1) return MapImage();
	image.bgra.assign(static_cast<size_t>(image.w) * image.h, 0xff000000);
	const int ratio = info.heightCols > 0 ? info.heightCols / info.cols : 0;
	const bool relief = ratio >= 1 && info.height.size() >= static_cast<size_t>(info.heightCols) * info.heightRows;
	const auto heightAt = [&](int cx, int cy) {
		const int x = std::max(0, std::min(info.heightCols - 1, cx * ratio)), y = std::max(0, std::min(info.heightRows - 1, cy * ratio));
		return static_cast<int>(info.height[static_cast<size_t>(y) * info.heightCols + x]);
	};
	for (int oy = 0; oy < image.h; oy++) {
		for (int ox = 0; ox < image.w; ox++) {
			unsigned sum[3] = {};
			for (int dy = 0; dy < factor; dy++) {
				for (int dx = 0; dx < factor; dx++) {
					const int cx = ox * factor + dx, cy = oy * factor + dy;
					const uint8_t t = info.terrain[static_cast<size_t>(cy) * info.cols + cx];
					const MapLayerColor c = t < layers.size() ? layers[t] : MapLayerColor();
					int r = (c.rgb >> 16) & 255, g = (c.rgb >> 8) & 255, b = c.rgb & 255;
					if (relief && !c.water) {
						const int d = std::max(-24, std::min(24, heightAt(cx - 1, cy - 1) - heightAt(cx + 1, cy + 1)));
						const int scale = 96 + d;
						r = std::min(255, r * scale / 96);
						g = std::min(255, g * scale / 96);
						b = std::min(255, b * scale / 96);
					}
					sum[0] += r;
					sum[1] += g;
					sum[2] += b;
				}
			}
			const unsigned n = static_cast<unsigned>(factor * factor);
			image.bgra[static_cast<size_t>(oy) * image.w + ox] = 0xff000000u | (sum[0] / n) << 16 | (sum[1] / n) << 8 | (sum[2] / n);
		}
	}
	return image;
}

const char *MapSizeName(int units)
{
	if (units <= 0) return "";
	if (units <= 8192) return "Small";
	if (units <= 16384) return "Medium";
	return "Large";
}

int MapHumanSlots(const MapInfo &info)
{
	int n = 0;
	for (const MapPlayer &p : info.players) n += p.human ? 1 : 0;
	return n;
}

namespace {
std::vector<std::vector<std::string>> TextBlocks(const std::string &gameText)
{
	std::vector<std::vector<std::string>> blocks(1);
	size_t at = 0;
	while (at <= gameText.size()) {
		size_t end = gameText.find('\n', at);
		if (end == std::string::npos) end = gameText.size();
		std::string line = gameText.substr(at, end - at);
		while (!line.empty() && (line.back() == '\r' || IsSpace(line.back()))) line.pop_back();
		if (line.empty()) {
			if (!blocks.back().empty()) blocks.emplace_back();
		}
		else {
			blocks.back().push_back(line);
		}
		at = end + 1;
	}
	if (blocks.back().empty()) blocks.pop_back();
	return blocks;
}

bool PlayerLine(const std::string &line, int &slot, std::string &name)
{
	const size_t colon = line.find(':');
	if (colon == std::string::npos || colon < 3 || colon > 40) return false;
	size_t end = colon;
	while (end > 0 && IsSpace(line[end - 1])) end--;
	size_t digits = end;
	while (digits > 0 && line[digits - 1] >= '0' && line[digits - 1] <= '9') digits--;
	if (digits == end || digits == 0 || !IsSpace(line[digits - 1])) return false;
	for (size_t i = 0; i < digits; i++) {
		if (line[i] >= '0' && line[i] <= '9') return false;
	}
	slot = ToInt(line.substr(digits, end - digits));
	size_t from = colon + 1;
	while (from < line.size() && IsSpace(line[from])) from++;
	name = line.substr(from);
	return slot >= 1 && slot <= 16 && !name.empty();
}
}

std::vector<std::pair<int, std::string>> MapPlayerLines(const std::string &gameText)
{
	std::vector<std::pair<int, std::string>> out;
	const std::vector<std::vector<std::string>> blocks = TextBlocks(gameText);
	if (blocks.size() < 2) return out;
	for (const std::string &line : blocks.back()) {
		int slot = 0;
		std::string name;
		if (!PlayerLine(line, slot, name)) return std::vector<std::pair<int, std::string>>();
		out.push_back({slot, name});
	}
	return out;
}

std::string MapDescriptionBody(const std::string &gameText, bool dropPlayerLines)
{
	std::vector<std::vector<std::string>> blocks = TextBlocks(gameText);
	const auto allLabels = [](const std::vector<std::string> &b) {
		for (const std::string &l : b) {
			const size_t colon = l.find(':');
			if (colon == std::string::npos || colon > 40) return false;
		}
		return !b.empty();
	};
	if (blocks.size() >= 2 && allLabels(blocks.front())) blocks.erase(blocks.begin());
	if (dropPlayerLines && blocks.size() >= 2 && allLabels(blocks.back()) && blocks.back().size() <= 16) blocks.pop_back();
	if (!blocks.empty() && !blocks.front().empty()) {
		std::string &first = blocks.front().front();
		const size_t colon = first.find(':');
		if (colon != std::string::npos && colon <= 32 && colon + 1 < first.size() && first.find(' ') < colon + 1) {
			size_t from = colon + 1;
			while (from < first.size() && IsSpace(first[from])) from++;
			if (from < first.size()) first = first.substr(from);
		}
	}
	std::string out;
	for (const std::vector<std::string> &b : blocks) {
		if (!out.empty()) out += "\n\n";
		for (size_t i = 0; i < b.size(); i++) out += (i ? "\n" : "") + b[i];
	}
	return out;
}
