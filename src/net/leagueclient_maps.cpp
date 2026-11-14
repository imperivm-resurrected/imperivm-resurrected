// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/guard.h"
#include "core/log.h"
#include "core/sha256.h"
#include "core/tournament.h"
#include "game/cupmaps.h"
#include "mods/mods.h"
#include "net/leagueclient_internal.h"
#include "net/leagueclient_tourney.h"

using namespace leagueclient_impl;

namespace {

// A scenario is looked at again after this long: one in place in case the player moved or changed it, a wrong or missing one sooner, as
// the player may be putting it right.
const DWORD kRecheckReadyMs = 300000, kRecheckWrongMs = 60000;
const unsigned kDownloadWaitMs = 120000;

struct Entry {
	CupMapState state;
	bool running = false;
	DWORD at = 0;
};

std::mutex g_mx;
// By the file's name in lower case and its SHA-256.
std::map<std::string, Entry> g_maps;

std::string KeyOf(const TourneyMap &m)
{
	std::string file = m.file;
	for (char &c : file) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return file + "|" + m.hash;
}

// Returns what the map was before.
CupMapStatus Set(const std::string &key, const CupMapState &state, bool running)
{
	std::lock_guard<std::mutex> lock(g_mx);
	Entry &e = g_maps[key];
	const CupMapStatus before = e.state.status;
	e.state = state;
	e.running = running;
	e.at = GetTickCount();
	return before;
}

// The content pack whose copy of the file the game loads instead of the one in Scenarios, if any.
std::string PackOf(const std::string &file)
{
	const MapOrigin origin = ModsMapOrigin(CupScenarioPath(file));
	return origin.kind == kMapMod ? (origin.modName.empty() ? origin.modFolder : origin.modName) : std::string();
}

void Check(std::string key, TourneyMap m)
{
	CupMapState st;
	std::string sha;
	uint64_t bytes = 0;
	const bool have = CupScenarioLocal(m.file, sha, bytes);
	if (have && sha == m.hash) {
		st.status = CupMapStatus::Ready;
		Set(key, st, false);
		return;
	}
	if (m.kind == TourneyMapKind::Game) {
		st.status = have ? CupMapStatus::Different : CupMapStatus::Missing;
		st.pack = have ? PackOf(m.file) : std::string();
		if (Set(key, st, false) != st.status) LogW("league", "the cup's scenario %s is the game's, and this computer's copy is %s", m.file.c_str(), have ? "another one" : "missing");
		return;
	}
	if (m.bytes > kTourneyScenarioMaxBytes) {
		st.status = CupMapStatus::Failed;
		st.reason = "too_large";
		Set(key, st, false);
		return;
	}
	st.status = CupMapStatus::Downloading;
	st.total = static_cast<uint64_t>(m.bytes);
	Set(key, st, true);
	LogI("league", "downloading the cup's scenario %s (%lld bytes)", m.file.c_str(), static_cast<long long>(m.bytes));
	HttpOptions o;
	o.accept = "application/octet-stream";
	o.maxBody = static_cast<size_t>(kTourneyScenarioMaxBytes) + 1;
	o.receiveMs = kDownloadWaitMs;
	o.progress = [key](uint64_t done, uint64_t total) {
		std::lock_guard<std::mutex> lock(g_mx);
		Entry &e = g_maps[key];
		e.state.got = done;
		if (total) e.state.total = total;
	};
	const HttpReply r = HttpWith("GET", "/v1/maps/" + m.hash, std::string(), false, o);
	st = CupMapState();
	st.status = CupMapStatus::Failed;
	if (!r.Ok()) {
		st.reason = r.status == 404 ? "not_found" : "network";
		LogW("league", "could not download the cup's scenario %s (%s)", m.file.c_str(), r.Describe().c_str());
		Set(key, st, false);
		return;
	}
	Sha256 h;
	h.Add(r.body);
	if (h.Hex() != m.hash) {
		st.reason = "hash";
		LogW("league", "the cup's scenario %s that came is not the file its SHA-256 names (%u bytes)", m.file.c_str(), static_cast<unsigned>(r.body.size()));
		Set(key, st, false);
		return;
	}
	std::string error;
	if (!CupScenarioStore(m.file, r.body, error)) {
		st.reason = "save";
		LogE("league", "could not keep the cup's scenario %s: %s", m.file.c_str(), error.c_str());
		Set(key, st, false);
		return;
	}
	// A content pack with a scenario of the same name wins over the file in Scenarios.
	if (!CupScenarioLocal(m.file, sha, bytes) || sha != m.hash) {
		st.reason = "pack";
		st.pack = PackOf(m.file);
		LogW("league", "the cup's scenario %s is saved, but the game loads another copy (%s)", m.file.c_str(), st.pack.c_str());
		Set(key, st, false);
		return;
	}
	st = CupMapState();
	st.status = CupMapStatus::Ready;
	Set(key, st, false);
}

}

CupMapState LeagueCupMap(const TourneyMap &m)
{
	CupMapState out;
	if (!m.Scenario()) {
		out.status = CupMapStatus::Ready;
		return out;
	}
	if (!m.Known() || !TourneySha256Valid(m.hash)) {
		out.status = CupMapStatus::Failed;
		out.reason = "not_found";
		return out;
	}
	const std::string key = KeyOf(m);
	std::lock_guard<std::mutex> lock(g_mx);
	auto it = g_maps.find(key);
	const DWORD now = GetTickCount();
	const bool due = it == g_maps.end() ||
		(!it->second.running && it->second.state.status != CupMapStatus::Failed &&
			now - it->second.at >= (it->second.state.status == CupMapStatus::Ready ? kRecheckReadyMs : kRecheckWrongMs));
	if (due) {
		Entry &e = g_maps[key];
		e.running = true;
		e.at = now;
		if (it == g_maps.end()) e.state = CupMapState();
		SafeThread(Check, key, m).detach();
		it = g_maps.find(key);
	}
	return it->second.state;
}

void LeagueCupMapsRetry(const std::vector<TourneyMap> &maps)
{
	std::lock_guard<std::mutex> lock(g_mx);
	for (const TourneyMap &m : maps) {
		if (!m.Scenario()) continue;
		const auto it = g_maps.find(KeyOf(m));
		if (it == g_maps.end() || it->second.running) continue;
		if (it->second.state.status == CupMapStatus::Failed || it->second.state.status == CupMapStatus::Missing || it->second.state.status == CupMapStatus::Different) {
			LogI("league", "trying the cup's scenario %s again", m.file.c_str());
			g_maps.erase(it);
		}
	}
}
