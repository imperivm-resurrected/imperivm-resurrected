// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "net/leagueclient_profile.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/leagueprofile.h"
#include "core/log.h"
#include "net/leagueclient_internal.h"

using namespace leagueclient_impl;

namespace {

const DWORD kKeepMs = 60 * 1000, kRetryMs = 15 * 1000;
const size_t kKept = 16;

struct Entry {
	LeagueProfileState state = LeagueProfileState::None;
	DWORD at = 0;
	std::string lang;
	std::shared_ptr<const LeagueProfile> profile;
};

std::mutex g_mx;
std::map<std::string, Entry> g_profiles;

void Fetch(std::string steamId, std::string lang)
{
	const HttpReply r = Http("GET", LeagueProfilePath(UrlEncode(steamId), lang), "", false);
	Json j;
	auto profile = std::make_shared<LeagueProfile>();
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseProfile(j, *profile);
	if (!ok && r.status != 404) LogW("league", "could not read a player's profile (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	Entry &e = g_profiles[steamId];
	e.at = GetTickCount();
	if (ok) {
		e.state = LeagueProfileState::Ready;
		e.profile = profile;
	}
	else if (r.status == 404) {
		e.state = LeagueProfileState::Missing;
		e.profile.reset();
	}
	// A failure keeps what was read before on screen.
	else if (!e.profile) e.state = LeagueProfileState::Failed;
	else e.state = LeagueProfileState::Ready;
}

}

LeagueProfileView LeagueProfileOf(const std::string &steamId)
{
	LeagueProfileView v;
	std::lock_guard<std::mutex> lock(g_mx);
	auto it = g_profiles.find(steamId);
	if (it == g_profiles.end()) return v;
	v.state = it->second.state;
	v.profile = it->second.profile;
	return v;
}

void LeagueProfileRequest(const std::string &steamId, bool force)
{
	if (steamId.empty()) return;
	const std::string lang = LangCode();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		auto it = g_profiles.find(steamId);
		if (it != g_profiles.end()) {
			const Entry &e = it->second;
			if (e.state == LeagueProfileState::Loading) return;
			const DWORD keep = e.state == LeagueProfileState::Failed ? kRetryMs : kKeepMs;
			if (!force && e.lang == lang && GetTickCount() - e.at < keep) return;
		}
		if (g_profiles.size() >= kKept && it == g_profiles.end()) {
			auto oldest = g_profiles.end();
			for (auto e = g_profiles.begin(); e != g_profiles.end(); ++e) {
				if (e->second.state != LeagueProfileState::Loading && (oldest == g_profiles.end() || e->second.at < oldest->second.at)) oldest = e;
			}
			if (oldest != g_profiles.end()) g_profiles.erase(oldest);
		}
		Entry &e = g_profiles[steamId];
		// What was read stays on screen while it is read again.
		if (e.state != LeagueProfileState::Ready) e.state = LeagueProfileState::Loading;
		e.at = GetTickCount();
		e.lang = lang;
	}
	SafeThread(Fetch, steamId, lang).detach();
}
