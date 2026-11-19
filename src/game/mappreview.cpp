// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include "game/mappreview.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>

#include "core/guard.h"
#include "core/log.h"
#include "hooks/gamefiles.h"

namespace {
const int kPreviewSide = 256;
const unsigned kReadMode = 1;
const unsigned kMaxCached = 400;

std::mutex g_lock;
std::map<std::string, std::shared_ptr<const MapPreviewData>> g_done;
// When a map could not be read: its file can be busy for a moment (the room just made, a pack being mounted), so it is read again later
// instead of staying without a preview.
std::map<std::string, unsigned long long> g_failedAt;
const unsigned long long kRetryMs = 2000;
std::deque<std::string> g_queue;
std::set<std::string> g_queued;
std::map<std::string, std::string> g_paths;
bool g_running = false;
unsigned g_generation = 0;
bool g_logged = false;
std::map<std::string, std::vector<MapLayerColor>> g_layers;

std::string Key(const std::string &path)
{
	std::string p = path;
	for (char &c : p) c = c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	if (p.size() > 5 && p.compare(p.size() - 5, 5, ".bfhp") == 0) p.resize(p.size() - 5);
	return p;
}

bool ReadFirst(const std::string &path, std::vector<char> &bytes)
{
	std::vector<std::string> candidates = {path};
	if (path.size() < 5 || _stricmp(path.c_str() + path.size() - 5, ".bfhp") != 0) candidates.push_back(path + ".bfhp");
	if (path.find_first_of("\\/") == std::string::npos) candidates.push_back("Scenarios\\" + path + ".bfhp");
	for (const std::string &c : candidates) {
		if (ReadGameFile(c.c_str(), kReadMode, bytes) && !bytes.empty()) return true;
	}
	return false;
}

const std::vector<MapLayerColor> &LayersFor(const std::string &season)
{
	const std::string key = season.empty() ? "spring" : season;
	const auto found = g_layers.find(key);
	if (found != g_layers.end()) return found->second;
	std::vector<MapLayerColor> layers;
	std::vector<char> xml;
	if (ReadGameFile("data/terrains.xml", kReadMode, xml) && !xml.empty()) {
		for (const MapLayerDef &d : MapLayerDefs(std::string(xml.begin(), xml.end()))) {
			if (layers.size() <= static_cast<size_t>(d.z)) layers.resize(static_cast<size_t>(d.z) + 1);
			MapLayerColor &c = layers[d.z];
			c.water = d.type == 4 || d.type == 7;
			std::vector<char> tile;
			uint32_t rgb = 0;
			if (ReadGameFile(("minimap/zoom8/terrain/" + MapLayerTilePath(d, key)).c_str(), kReadMode, tile) && !tile.empty() &&
				BmpAverageColor(reinterpret_cast<const unsigned char *>(tile.data()), tile.size(), rgb)) {
				c.rgb = rgb;
				c.known = true;
			}
			else if (c.water) {
				c.rgb = 0x2b6f91;
			}
		}
		LogD("game", "map preview: %u terrain layers read for %s", static_cast<unsigned>(layers.size()), key.c_str());
	}
	else {
		LogW("game", "map preview: the terrain table could not be read; the previews are grey");
	}
	return g_layers.emplace(key, std::move(layers)).first->second;
}

struct Timing {
	unsigned long long read = 0, parse = 0, render = 0, origin = 0, bytes = 0;
	int maps = 0;
};
Timing g_timing;

unsigned long long NowMs()
{
	return GetTickCount64();
}

std::shared_ptr<const MapPreviewData> Load(const std::string &path)
{
	auto data = std::make_shared<MapPreviewData>();
	std::vector<char> bytes;
	const unsigned long long t0 = NowMs();
	bool ok = ReadFirst(path, bytes);
	const unsigned long long t1 = NowMs();
	if (!ok && !g_logged) {
		g_logged = true;
		LogW("game", "map preview: could not open \"%s\"", path.c_str());
	}
	if (ok) ok = MapInfoParse(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), data->info);
	const unsigned long long t2 = NowMs();
	if (ok && !data->info.terrain.empty()) data->image = MapRender(data->info, LayersFor(data->info.season), kPreviewSide);
	const unsigned long long t3 = NowMs();
	MapInfoDropGrids(data->info);
	data->failed = !ok;
	data->humans = MapHumanSlots(data->info);
	data->origin = ModsMapOrigin(path);
	const unsigned long long t4 = NowMs();
	g_timing.read += t1 - t0;
	g_timing.parse += t2 - t1;
	g_timing.render += t3 - t2;
	g_timing.origin += t4 - t3;
	g_timing.bytes += bytes.size();
	g_timing.maps++;
	return data;
}

void Work()
{
	const bool background = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN) != 0;
	const unsigned long long started = NowMs();
	for (;;) {
		std::string key, path;
		unsigned generation = 0;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (g_queue.empty()) {
				g_running = false;
				if (g_timing.maps) {
					LogI("game", "map previews: %d maps, %.0f MB read, %.1f s in all (reading %.1f s, parsing %.1f s, drawing %.1f s, origin %.1f s; background mode %s)", g_timing.maps,
						static_cast<double>(g_timing.bytes) / 1048576.0, static_cast<double>(NowMs() - started) / 1000.0, static_cast<double>(g_timing.read) / 1000.0,
						static_cast<double>(g_timing.parse) / 1000.0, static_cast<double>(g_timing.render) / 1000.0, static_cast<double>(g_timing.origin) / 1000.0, background ? "on" : "off");
					g_timing = Timing();
				}
				if (background) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
				return;
			}
			key = g_queue.front();
			g_queue.pop_front();
			path = g_paths[key];
			generation = g_generation;
		}
		const std::shared_ptr<const MapPreviewData> data = Load(path);
		Sleep(4);
		std::lock_guard<std::mutex> lock(g_lock);
		g_queued.erase(key);
		if (generation != g_generation) continue;
		if (g_done.size() >= kMaxCached) {
			g_done.clear();
			g_failedAt.clear();
		}
		g_done[key] = data;
		if (data->failed) g_failedAt[key] = NowMs();
		else g_failedAt.erase(key);
	}
}

void Enqueue(const std::string &path, bool front)
{
	const std::string key = Key(path);
	if (g_done.count(key)) return;
	if (g_queued.count(key)) {
		// Asked for again where it is shown: it goes ahead of the ones only wanted.
		if (front) {
			const auto at = std::find(g_queue.begin(), g_queue.end(), key);
			if (at != g_queue.end() && at != g_queue.begin()) {
				g_queue.erase(at);
				g_queue.push_front(key);
			}
		}
		return;
	}
	g_queued.insert(key);
	g_paths[key] = path;
	if (front) g_queue.push_front(key);
	else g_queue.push_back(key);
	if (!g_running) {
		g_running = true;
		SafeThread([] { Work(); }).detach();
	}
}
}

std::shared_ptr<const MapPreviewData> MapPreviewGet(const std::string &scenarioPath)
{
	if (scenarioPath.empty()) return nullptr;
	std::lock_guard<std::mutex> lock(g_lock);
	const std::string key = Key(scenarioPath);
	const auto found = g_done.find(key);
	if (found != g_done.end()) {
		const auto failed = g_failedAt.find(key);
		if (failed != g_failedAt.end() && NowMs() - failed->second >= kRetryMs) {
			const std::shared_ptr<const MapPreviewData> last = found->second;
			g_done.erase(found);
			g_failedAt.erase(failed);
			Enqueue(scenarioPath, true);
			return last;
		}
		return found->second;
	}
	Enqueue(scenarioPath, true);
	return nullptr;
}

std::shared_ptr<const MapPreviewData> MapPreviewPeek(const std::string &scenarioPath)
{
	if (scenarioPath.empty()) return nullptr;
	std::lock_guard<std::mutex> lock(g_lock);
	const auto found = g_done.find(Key(scenarioPath));
	return found == g_done.end() ? nullptr : found->second;
}

void MapPreviewWanted(const std::vector<std::string> &scenarioPaths, int first)
{
	std::lock_guard<std::mutex> lock(g_lock);
	const int n = static_cast<int>(scenarioPaths.size());
	if (n == 0) return;
	for (int k = 0; k < n; k++) {
		const int i = ((first < 0 ? 0 : first) + k) % n;
		if (!scenarioPaths[i].empty()) Enqueue(scenarioPaths[i], false);
	}
}

void MapPreviewReset()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_generation++;
	g_done.clear();
	g_failedAt.clear();
	g_queue.clear();
	g_queued.clear();
	g_paths.clear();
	g_logged = false;
}
