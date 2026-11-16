// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/leagueclient_seasons.h"
#include "core/guard.h"
#include "core/json.h"
#include "core/leagueranked.h"
#include "core/leagueseasons.h"
#include "core/leagueteam.h"
#include "core/log.h"
#include "net/leagueclient.h"
#include "net/leagueclient_internal.h"
#include "net/patrons.h"
#include "net/steamnet.h"

using namespace leagueclient_impl;

namespace {

const DWORD kSeasonsKeepMs = 10 * 60 * 1000, kSeasonsRetryMs = 60 * 1000, kHallKeepMs = 5 * 60 * 1000, kCardKeepMs = 60 * 1000;
const size_t kCardsKept = 64;

struct Seasons {
	bool loading = false, loaded = false;
	DWORD at = 0;
	// The language of the list read, and of the last answer, a failure too.
	std::string lang, asked;
	std::vector<LeagueSeasonInfo> list;
};

struct Hall {
	int season = -1;
	std::string queue = "1v1";
	bool loading = false, loaded = false, failed = false;
	DWORD at = 0;
	unsigned gen = 0;
	LeagueHall hall;
};

struct Card {
	LeagueCardState state = LeagueCardState::None;
	DWORD at = 0;
	LeaguePlayerProfile card;
};

struct Cosmetics {
	bool busy = false, ceremony = false;
	std::string error;
};

std::mutex g_mx;
Seasons g_seasons;
Hall g_hall;
std::map<std::string, Card> g_cards;
Cosmetics g_cosmetics;

std::string CardKey(const std::string &steamId, int season, const std::string &queue)
{
	return steamId + ":" + std::to_string(season) + ":" + LeagueQueueId(queue);
}

void FetchSeasons(std::string lang)
{
	const HttpReply r = Http("GET", "/v1/league/seasons?lang=" + lang, "", false);
	Json j;
	std::vector<LeagueSeasonInfo> list;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseSeasons(j, list);
	if (!ok) LogW("league", "could not read the list of seasons (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	g_seasons.loading = false;
	g_seasons.at = GetTickCount();
	g_seasons.asked = lang;
	if (!ok) return;
	g_seasons.loaded = true;
	g_seasons.list = list;
	g_seasons.lang = lang;
}

void FetchHall(unsigned gen, int season, std::string queue)
{
	const HttpReply r = Http("GET", LeagueHallPath(season, LangCode(), queue), "", false);
	Json j;
	LeagueHall hall;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParseHall(j, hall);
	if (!ok && r.status != 404) LogW("league", "could not read the hall of fame of season %d (%s)", season, r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	if (gen != g_hall.gen) return;
	g_hall.loading = false;
	g_hall.at = GetTickCount();
	g_hall.loaded = ok || r.status == 404;
	g_hall.failed = !g_hall.loaded;
	g_hall.hall = ok ? hall : LeagueHall();
	g_hall.hall.season = season;
}

void FetchCard(std::string steamId, int season, std::string queue)
{
	const HttpReply r = Http("GET", LeaguePlayerPath(UrlEncode(steamId), season, queue), "", false);
	Json j;
	LeaguePlayerProfile card;
	const bool ok = r.Ok() && Json::Parse(r.body, j) && LeagueParsePlayerProfile(j, card);
	if (!ok && r.status != 404) LogW("league", "could not read a player's card (%s)", r.Describe().c_str());
	std::lock_guard<std::mutex> lock(g_mx);
	Card &c = g_cards[CardKey(steamId, season, queue)];
	c.at = GetTickCount();
	c.state = ok ? LeagueCardState::Ready : r.status == 404 ? LeagueCardState::Missing : LeagueCardState::Failed;
	if (ok) c.card = card;
}

// The frame and the title the player wears changed: the next look-ups and the player's own standing say so.
void Changed()
{
	LeagueMeRefresh(true);
	PatronsForget(SteamOwnId());
	PatronsWatch({SteamOwnId()});
}

bool PutCosmetics(bool setFrame, int frame, bool setTitle, int title, std::string &error)
{
	Json body = Json::MakeObject();
	if (setFrame) body.Set("frame", frame >= 0 ? Json::MakeNumber(frame) : Json());
	if (setTitle) body.Set("title", title >= 0 ? Json::MakeNumber(title) : Json());
	const HttpReply r = Http("PUT", "/v1/league/me/cosmetics", body.Dump(), true);
	if (r.Ok()) {
		LogI("league", "cosmetics: frame %d, title %d", setFrame ? frame : -2, setTitle ? title : -2);
		return true;
	}
	LogW("league", "could not change the frame or the title (%s)", r.Describe().c_str());
	error = r.status == 400 ? "That season did not give you a frame and a title." : FailureText(r);
	return false;
}

void CosmeticsThread(bool setFrame, int frame, bool setTitle, int title)
{
	std::string error;
	const bool ok = PutCosmetics(setFrame, frame, setTitle, title, error);
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_cosmetics.busy = false;
		g_cosmetics.error = error;
	}
	if (ok) Changed();
}

void CeremonyThread(int season, bool equip, bool frameAndTitle, std::string queue)
{
	std::string error;
	bool changed = false;
	if (equip && frameAndTitle) changed = PutCosmetics(true, season, true, season, error);
	const HttpReply r = Http("POST", LeagueCeremonySeenPath(season, queue), "", true);
	if (r.Ok()) LogI("league", "the ceremony of season %d was seen%s", season, equip ? " and its rewards equipped" : "");
	else LogW("league", "could not mark the ceremony of season %d seen (%s)", season, r.Describe().c_str());
	{
		std::lock_guard<std::mutex> lock(g_mx);
		g_cosmetics.ceremony = false;
		g_cosmetics.error = error;
	}
	if (changed) Changed();
	else LeagueMeRefresh(true);
}

}

LeagueSeasonsView LeagueSeasonsState()
{
	LeagueSeasonsView v;
	std::lock_guard<std::mutex> lock(g_mx);
	v.loaded = g_seasons.loaded;
	v.loading = g_seasons.loading;
	v.seasons = g_seasons.list;
	v.hallSeason = g_hall.season;
	v.hallQueue = g_hall.queue;
	v.hallLoading = g_hall.loading;
	v.hallLoaded = g_hall.loaded;
	v.hallFailed = g_hall.failed;
	v.hall = g_hall.hall;
	v.cosmeticsBusy = g_cosmetics.busy;
	v.cosmeticsError = g_cosmetics.error;
	v.ceremonyBusy = g_cosmetics.ceremony;
	return v;
}

void LeagueSeasonsRefresh(bool force)
{
	const std::string lang = LangCode();
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_seasons.loading) return;
		const DWORD keep = g_seasons.loaded ? kSeasonsKeepMs : kSeasonsRetryMs;
		if (!force && g_seasons.at && GetTickCount() - g_seasons.at < keep && (!g_seasons.loaded || g_seasons.asked == lang)) return;
		g_seasons.loading = true;
	}
	SafeThread(FetchSeasons, lang).detach();
}

void LeagueHallRequest(int season, const std::string &queue)
{
	if (season < 0) return;
	const std::string q = LeagueQueueId(queue);
	unsigned gen = 0;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const bool same = g_hall.season == season && g_hall.queue == q;
		if (same && (g_hall.loading || (g_hall.loaded && GetTickCount() - g_hall.at < kHallKeepMs))) return;
		if (same && g_hall.failed && GetTickCount() - g_hall.at < kSeasonsRetryMs) return;
		if (!same) {
			g_hall.loaded = g_hall.failed = false;
			g_hall.hall = LeagueHall();
		}
		g_hall.season = season;
		g_hall.queue = q;
		g_hall.loading = true;
		gen = ++g_hall.gen;
	}
	SafeThread(FetchHall, gen, season, q).detach();
}

LeaguePlayerView LeaguePlayerState(const std::string &steamId, int season, const std::string &queue)
{
	LeaguePlayerView v;
	std::lock_guard<std::mutex> lock(g_mx);
	auto it = g_cards.find(CardKey(steamId, season, queue));
	if (it == g_cards.end()) return v;
	v.state = it->second.state;
	v.card = it->second.card;
	return v;
}

void LeaguePlayerRequest(const std::string &steamId, int season, bool force, const std::string &queue)
{
	if (steamId.empty()) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		const std::string key = CardKey(steamId, season, queue);
		auto it = g_cards.find(key);
		if (it != g_cards.end()) {
			const Card &c = it->second;
			if (c.state == LeagueCardState::Loading) return;
			const DWORD keep = c.state == LeagueCardState::Failed ? kSeasonsRetryMs / 4 : kCardKeepMs;
			if (!force && GetTickCount() - c.at < keep) return;
		}
		if (g_cards.size() >= kCardsKept && it == g_cards.end()) {
			auto oldest = g_cards.end();
			for (auto c = g_cards.begin(); c != g_cards.end(); ++c) {
				if (c->second.state != LeagueCardState::Loading && (oldest == g_cards.end() || c->second.at < oldest->second.at)) oldest = c;
			}
			if (oldest != g_cards.end()) g_cards.erase(oldest);
		}
		Card &c = g_cards[key];
		// What was read stays on screen while it is read again.
		if (c.state != LeagueCardState::Ready) c.state = LeagueCardState::Loading;
		c.at = GetTickCount();
	}
	SafeThread(FetchCard, steamId, season, LeagueQueueId(queue)).detach();
}

void LeagueCosmeticsSet(bool setFrame, int frame, bool setTitle, int title)
{
	if (!setFrame && !setTitle) return;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_cosmetics.busy) return;
		g_cosmetics.busy = true;
		g_cosmetics.error.clear();
	}
	SafeThread(CosmeticsThread, setFrame, frame, setTitle, title).detach();
}

void LeagueCeremonyEquip(int season, bool frameAndTitle, const std::string &badgeId, const std::string &queue)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_cosmetics.ceremony) return;
		g_cosmetics.ceremony = true;
		g_cosmetics.error.clear();
	}
	if (!badgeId.empty()) LeagueBadgeChoose("season", badgeId);
	SafeThread(CeremonyThread, season, true, frameAndTitle, LeagueQueueId(queue)).detach();
}

void LeagueCeremonySeen(int season, const std::string &queue)
{
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (g_cosmetics.ceremony) return;
		g_cosmetics.ceremony = true;
	}
	SafeThread(CeremonyThread, season, false, false, LeagueQueueId(queue)).detach();
}
