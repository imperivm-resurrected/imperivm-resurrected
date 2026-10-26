// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#include <windows.h>

#include "net/patrons.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/log.h"
#include "hooks/dmshim.h"
#include "i18n/tr.h"
#include "mods/downloads.h"
#include "net/steamnet.h"

#include <map>
#include <mutex>
#include <thread>

namespace {

const char kLookupUrl[] = "https://api.imperivmresurrected.com/v1/patrons/lookup?ids=";
const char kRollUrl[] = "https://api.imperivmresurrected.com/v1/patrons";
const DWORD kRollKeepMs = 5 * 60 * 1000;
const size_t kMaxPerLookup = 16;
const DWORD kKeepMs = 15 * 60 * 1000;
// After a failed lookup, so a server that is down is not asked every second.
const DWORD kRetryMs = 2 * 60 * 1000;

struct Entry {
	PatronInfo info;
	DWORD at = 0;
	bool known = false;
};

struct Pending {
	uint64_t room, id;
	std::string name;
};

std::mutex g_mx;
std::map<uint64_t, Entry> g_cache;
std::map<uint64_t, Pending> g_announce;
bool g_busy = false;
DWORD g_failedAt = 0;

std::vector<PatronRollEntry> g_roll;
int g_rollState = 0;
bool g_rollBusy = false;
DWORD g_rollAt = 0;

PatronTier TierOfPlace(int place)
{
	if (place == 1) return PatronTier::Gold;
	if (place == 2) return PatronTier::Silver;
	if (place == 3) return PatronTier::Bronze;
	return place <= 10 ? PatronTier::Top10 : PatronTier::Patron;
}

void ReadRoll()
{
	Json j;
	const std::string err = DlHttpGetJson(kRollUrl, j);
	const Json *list = err.empty() ? j.Get("patrons") : nullptr;
	std::vector<PatronRollEntry> roll;
	if (list) {
		for (const Json &p : list->arr) {
			PatronRollEntry e;
			e.place = static_cast<int>(p.GetNumber("place"));
			e.name = p.GetString("name");
			e.hideAmount = p.GetBool("hideAmount");
			e.amount = p.GetNumber("amount");
			e.tier = TierOfPlace(e.place);
			if (e.place > 0 && !e.name.empty()) roll.push_back(e);
		}
	}
	else {
		LogW("patrons", "could not read the honour roll (%s)", err.empty() ? "unexpected answer" : err.c_str());
	}
	std::lock_guard<std::mutex> lock(g_mx);
	g_rollBusy = false;
	g_rollAt = GetTickCount();
	if (list) {
		g_roll = roll;
		g_rollState = 1;
	}
	else if (g_rollState == 0) {
		g_rollState = -1;
	}
}

PatronTier TierFromName(const std::string &s)
{
	if (s == "gold") return PatronTier::Gold;
	if (s == "silver") return PatronTier::Silver;
	if (s == "bronze") return PatronTier::Bronze;
	if (s == "top10") return PatronTier::Top10;
	if (s == "patron") return PatronTier::Patron;
	if (s == "dev") return PatronTier::Dev;
	return PatronTier::None;
}

void Lookup(std::vector<uint64_t> ids)
{
	std::string url = kLookupUrl;
	for (size_t i = 0; i < ids.size(); i++) url += (i ? "," : "") + std::to_string(ids[i]);
	Json j;
	const std::string err = DlHttpGetJson(url, j);
	const Json *found = err.empty() ? j.Get("patrons") : nullptr;
	if (!err.empty()) LogW("patrons", "could not look up %u players (%s)", static_cast<unsigned>(ids.size()), err.c_str());

	std::vector<Pending> notices;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_busy = false;
		if (!found) {
			g_failedAt = GetTickCount();
			for (uint64_t id : ids) g_announce.erase(id);
			return;
		}
		const DWORD now = GetTickCount();
		for (uint64_t id : ids) {
			Entry &e = g_cache[id];
			e = Entry();
			e.at = now;
			e.known = true;
			if (const Json *p = found->Get(std::to_string(id))) {
				e.info.place = static_cast<int>(p->GetNumber("place"));
				e.info.tier = TierFromName(p->GetString("tier"));
			}
			auto a = g_announce.find(id);
			if (a == g_announce.end()) continue;
			if (e.info.tier != PatronTier::None) notices.push_back(a->second);
			g_announce.erase(a);
		}
	}
	// Outside g_mx: the room lock is taken after it in UpdateRoom, never before.
	for (const Pending &n : notices) SteamAddPatronJoined(n.room, n.id, n.name);
	if (!notices.empty()) UiSoundPlay(3);
}

}

PatronInfo PatronOf(uint64_t id)
{
	std::lock_guard<std::mutex> lock(g_mx);
	auto it = g_cache.find(id);
	return it == g_cache.end() ? PatronInfo() : it->second.info;
}

void PatronsWatch(const std::vector<uint64_t> &ids)
{
	std::vector<uint64_t> wanted;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const DWORD now = GetTickCount();
		if (g_busy || (g_failedAt && now - g_failedAt < kRetryMs)) return;
		for (uint64_t id : ids) {
			auto it = g_cache.find(id);
			if (id && (it == g_cache.end() || !it->second.known || now - it->second.at > kKeepMs)) wanted.push_back(id);
			if (wanted.size() == kMaxPerLookup) break;
		}
		if (wanted.empty()) return;
		g_busy = true;
		g_failedAt = 0;
	}
	SafeThread(Lookup, wanted).detach();
}

void PatronsRollRefresh()
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_rollBusy || (g_rollAt && GetTickCount() - g_rollAt < kRollKeepMs)) return;
		g_rollBusy = true;
	}
	SafeThread(ReadRoll).detach();
}

std::vector<PatronRollEntry> PatronsRoll(int &state)
{
	std::lock_guard<std::mutex> lock(g_mx);
	state = g_rollState;
	return g_roll;
}

void PatronsAnnounceLater(uint64_t room, uint64_t id, const std::string &name)
{
	std::lock_guard<std::mutex> lock(g_mx);
	g_announce[id] = {room, id, name};
}

std::string PatronJoinedNotice(const std::string &name, PatronTier tier)
{
	if (tier == PatronTier::Dev) return TrFmt("%s, developer of Imperivm Resurrected, has joined.", {name});
	return TrFmt("%s supports Imperivm Resurrected. Thank you!", {name});
}
