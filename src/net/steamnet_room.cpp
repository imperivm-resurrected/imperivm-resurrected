// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include "core/log.h"
#include "hooks/dmshim.h"
#include "game/gameroom.h"
#include "net/vnet.h"
#include "net/patrons.h"
#include "i18n/tr.h"
#include "addr/state.h"

#include "net/steamnet_internal.h"
#include "core/uiport.h"

using namespace steamnet_impl;

namespace steamnet_impl {
bool InLobby(uint64 lobby, uint64 who)
{
	if (!lobby || !who) return false;
	const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, lobby);
	for (int i = 0; i < n; i++) {
		if (S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, lobby, i) == who) return true;
	}
	return false;
}

void SetPresence()
{
	if (g_lobby) {
		const std::string connect = "+connect_lobby " + std::to_string(g_lobby);
		S.SteamAPI_ISteamFriends_SetRichPresence(g_steam.friends, "connect", connect.c_str());
		S.SteamAPI_ISteamFriends_SetRichPresence(g_steam.friends, "status", TrFmt("In room %s", {Data(g_lobby, "name")}).c_str());
	}
	else {
		S.SteamAPI_ISteamFriends_ClearRichPresence(g_steam.friends);
	}
}

void AssignNumbers()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby);
	std::vector<uint64> members;
	const uint64 owner = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
	members.push_back(owner);
	for (int i = 0; i < n; i++) {
		const uint64 m = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, i);
		if (m != owner && !IsBanned(g_lobby, m)) members.push_back(m);
	}
	int maxK = atoi(Data(g_lobby, "kmax").c_str());
	std::map<int, uint64> holder;
	std::vector<uint64> need;
	for (uint64 m : members) {
		const int k = atoi(Data(g_lobby, ("k:" + std::to_string(m)).c_str()).c_str());
		if (k > maxK) maxK = k;
		if (!k) need.push_back(m);
		else if (holder.count(k)) {
			LogI("steam", "%s and %s had the same virtual IP 127.77.0.%d; %s gets another", Name(holder[k]).c_str(), Name(m).c_str(), k, Name(m).c_str());
			need.push_back(m);
		}
		else holder[k] = m;
	}
	for (uint64 m : need) {
		const int k = ++maxK;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, ("k:" + std::to_string(m)).c_str(), std::to_string(k).c_str());
		LogI("steam", "%s gets virtual IP 127.77.0.%d", Name(m).c_str(), k);
	}
	if (!need.empty()) S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "kmax", std::to_string(maxK).c_str());
}

void RefreshOwnKey()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	const std::string own = OwnKey();
	if (!own.empty() && Data(g_lobby, "key") != own) S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "key", own.c_str());
}

void TakeOverHostData()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	const std::string prev = Data(g_lobby, "hostid");
	if (_strtoui64(prev.c_str(), nullptr, 10) == g_steam.me) return;
	if (!prev.empty() && *reinterpret_cast<void **>(addr::kGame) == nullptr) {
		const std::string host = Data(g_lobby, "host");
		LogI("steam", "the host (%s) left before the game started; out of the room", host.c_str());
		Notice((host.empty() ? std::string("The host") : host) + Tr(" has closed the room."));
		Leave();
		return;
	}
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "hostid", std::to_string(g_steam.me).c_str());
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "host", g_myName.c_str());
	LogI("steam", "the host left; the room is in my charge");
}

void PublishPlayerList()
{
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby);
	std::string list = Name(g_steam.me);
	for (int i = 0; i < n; i++) {
		const uint64 m = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, i);
		if (m != g_steam.me) list += "\n" + Name(m);
	}
	static std::string last;
	static uint64 lastLobby = 0;
	if (list == last && lastLobby == g_lobby) return;
	last = list;
	lastLobby = g_lobby;
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "plist", list.c_str());
}

std::map<uint64, DWORD> g_sentDoneAt;
std::map<uint64, std::pair<std::string, DWORD>> g_restarting;
}

namespace steamnet_impl {
namespace {
std::map<uint64, std::string> g_prevMembers;
uint64 g_prevMembersLobby = 0;

void TrackRestarting(OnlineRoom &room)
{
	const DWORD now = GetTickCount();
	if (!room.id || !room.owner || room.id != g_prevMembersLobby) {
		g_restarting.clear();
		g_prevMembers.clear();
		g_prevMembersLobby = room.owner ? room.id : 0;
	}
	std::map<uint64, std::string> current;
	for (const OnlineMember &m : room.members) current[m.id] = m.name;
	for (const auto &prev : g_prevMembers) {
		if (current.count(prev.first)) continue;
		const auto done = g_sentDoneAt.find(prev.first);
		if (done != g_sentDoneAt.end() && now - done->second < 180000) {
			g_restarting[prev.first] = {prev.second, now};
			LogI("steam", "%s left the room after receiving the mods (restarting the game)", prev.second.c_str());
		}
	}
	for (auto it = g_restarting.begin(); it != g_restarting.end();) {
		const bool kicked = Data(room.id, "kick").find(";" + std::to_string(it->first) + ";") != std::string::npos;
		if (current.count(it->first) || kicked || now - it->second.second > 120000) {
			it = g_restarting.erase(it);
			continue;
		}
		OnlineMember m;
		m.id = it->first;
		m.name = it->second.first;
		room.restarting.push_back(m);
		++it;
	}
	if (room.owner) g_prevMembers = current;
}
}
}

namespace steamnet_impl {
void UpdateRoom()
{
	OnlineRoom room;
	std::vector<std::pair<uint64_t, int>> peers;
	int myK = 0;
	if (g_lobby && !g_signal.kickedHandled && Data(g_lobby, "kick").find(";" + std::to_string(g_steam.me) + ";") != std::string::npos) {
		g_signal.kickedHandled = true;
		Notice("You have been kicked from the room.");
		g_signal.kicked = true;
		if (DownloadActive()) FailDownload("you were kicked from the room");
		Leave();
		g_signal.kickedHandled = false;
		return;
	}
	if (g_lobby) {
		room.id = g_lobby;
		room.info = ReadLobby(g_lobby);
		const uint64 owner = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
		room.owner = owner == g_steam.me;
		room.contentSync = g_lobby == g_pendingJoin.syncLobby;
		const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby);
		for (int i = 0; i < n; i++) {
			OnlineMember m;
			m.id = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, i);
			if (m.id != g_steam.me && m.id != owner && IsBanned(g_lobby, m.id)) {
				room.bannedPresent++;
				continue;
			}
			m.name = Name(m.id);
			m.number = atoi(Data(g_lobby, ("k:" + std::to_string(m.id)).c_str()).c_str());
			m.owner = m.id == owner;
			m.me = m.id == g_steam.me;
			m.route = m.me ? "you" : RouteInfo(m.id);
			if (!m.me) {
				LogRouteChange(m.id, m.name, m.route);
				LogSilence(m.id, m.name, room.info.playing);
			}
			if (m.me) myK = m.number;
			else if (m.number) peers.push_back({m.id, m.number});
			room.members.push_back(m);
		}
	}
	{
		std::vector<NetPeerStat> stats;
		for (const OnlineMember &m : room.members) {
			if (m.me) continue;
			NetPeerStat n = NetStatOf(m.id);
			n.name = m.name;
			n.number = m.number;
			stats.push_back(n);
		}
		std::lock_guard<std::mutex> lock(g_peerStats.mx);
		g_peerStats.stats = std::move(stats);
	}
	static uint64 heldLobby = 0;
	static bool heldPlaying = false;
	static int heldMyK = 0;
	static std::map<uint64_t, int> heldNumbers;
	static std::map<uint64_t, DWORD> heldGoneAt;
	const DWORD kHoldGoneMs = 120000;
	if (heldLobby != g_lobby) {
		heldLobby = g_lobby;
		heldPlaying = false;
		heldMyK = 0;
		heldNumbers.clear();
		heldGoneAt.clear();
	}
	if (room.info.playing) heldPlaying = true;
	if (heldPlaying) {
		if (myK) heldMyK = myK;
		else if (heldMyK) myK = heldMyK;
		for (const auto &p : peers) {
			heldNumbers[p.first] = p.second;
			heldGoneAt.erase(p.first);
		}
	}
	TrackRestarting(room);
	{
		static std::map<uint64, std::string> before;
		std::map<uint64, std::string> now;
		for (const OnlineMember &m : room.members) {
			if (!m.me) now[m.id] = m.name;
		}
		for (const auto &gone : before) {
			if (now.count(gone.first)) continue;
			if (heldPlaying && heldNumbers.count(gone.first)) {
				LogI("steam", "%s no longer appears in the room mid-game; their connection is kept in case they return", Name(gone.first).c_str());
				continue;
			}
			SteamNetworkingIdentity who = Identity(gone.first);
			S.SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(g_steam.msgs, who);
			LogI("steam", "connection with %s closed (left the room)", Name(gone.first).c_str());
			bool restarting = false;
			for (const OnlineMember &r : room.restarting) {
				if (r.id == gone.first) restarting = true;
			}
			if (room.owner && !restarting) {
				GameRoomPlayerLeft(gone.second);
				UiWakeGameThread();
			}
		}
		before = now;
	}
	if (room.owner && room.info.playing) {
		static std::set<uint64> known;
		bool reopen = false;
		for (const OnlineMember &m : room.members) {
			if (m.me || known.count(m.id)) continue;
			known.insert(m.id);
			if (g_rejoinAllowed.count(m.id)) {
				LogI("rejoin", "%s is back in the room", m.name.c_str());
				S.SteamAPI_ISteamMatchmaking_SetLobbyJoinable(g_steam.mm, g_lobby, false);
			}
			else if (Data(g_lobby, ("k:" + std::to_string(m.id)).c_str()).empty()) {
				reopen = true;
				LogW("rejoin", "%s entered the in-game room without permission; out", m.name.c_str());
				std::string list = Data(g_lobby, "kick");
				if (list.empty()) list = ";";
				list += std::to_string(m.id) + ";";
				S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "kick", list.c_str());
			}
		}
		(void)reopen;
	}
	if (heldPlaying) {
		const DWORD tick = GetTickCount();
		for (auto it = heldNumbers.begin(); it != heldNumbers.end();) {
			bool present = false;
			for (const auto &p : peers) present = present || p.first == it->first;
			if (present) {
				++it;
				continue;
			}
			const int quiet = VnetSilenceMs(it->first);
			const bool alive = quiet >= 0 && quiet < 10000;
			if (alive) heldGoneAt.erase(it->first);
			const DWORD since = alive ? tick : heldGoneAt.emplace(it->first, tick).first->second;
			if (!alive && tick - since > kHoldGoneMs) {
				S.SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(g_steam.msgs, Identity(it->first));
				LogW("steam", "connection with %s closed (did not return to the room in %u s)", Name(it->first).c_str(), kHoldGoneMs / 1000);
				heldGoneAt.erase(it->first);
				it = heldNumbers.erase(it);
				continue;
			}
			peers.push_back({it->first, it->second});
			++it;
		}
	}
	VnetSetPeers(myK, peers);
	{
		std::vector<uint64_t> ids;
		for (const OnlineMember &m : room.members) ids.push_back(m.id);
		PatronsWatch(ids);
	}
	std::lock_guard<std::mutex> lock(g_lock);
	const bool same = g_room.id == room.id;
	room.chat = same ? g_room.chat : std::vector<OnlineChatLine>();
	room.chatSerial = same ? g_room.chatSerial : 0;
	if (same) {
		auto has = [](const std::vector<OnlineMember> &list, uint64_t id) {
			for (const OnlineMember &m : list) {
				if (m.id == id) return true;
			}
			return false;
		};
		SYSTEMTIME now;
		GetLocalTime(&now);
		auto notice = [&](const std::string &text) {
			OnlineChatLine l;
			l.system = true;
			l.text = text;
			l.minute = now.wHour * 60 + now.wMinute;
			room.chat.push_back(l);
			room.chatSerial++;
		};
		for (const OnlineMember &m : room.members) {
			if (!m.me && !has(g_room.members, m.id)) {
				notice(m.name + Tr(" has joined the room"));
				UiSoundPlay(2);
				if (PatronOf(m.id).tier != PatronTier::None) {
					notice(PatronJoinedNotice(m.name));
					room.chat.back().patron = true;
					room.chat.back().id = m.id;
					room.chat.back().who = m.name;
				}
				else PatronsAnnounceLater(room.id, m.id, m.name);
			}
		}
		for (const OnlineMember &m : g_room.members) {
			if (!m.me && !has(room.members, m.id) && !has(room.restarting, m.id)) notice(m.name + Tr(" has left the room"));
		}
		while (room.chat.size() > 300) room.chat.erase(room.chat.begin());
	}
	g_room = room;
}

void AddChatLine(uint64_t id, const std::string &who, const std::string &text)
{
	OnlineChatLine l;
	l.id = id;
	l.who = who;
	l.text = text;
	SYSTEMTIME now;
	GetLocalTime(&now);
	l.minute = now.wHour * 60 + now.wMinute;
	std::lock_guard<std::mutex> lock(g_lock);
	g_room.chat.push_back(l);
	g_room.chatSerial++;
	if (g_room.chat.size() > 300) g_room.chat.erase(g_room.chat.begin());
}


void RequestLobbySearch()
{
	S.SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(g_steam.mm, "imperivm", "1", k_ELobbyComparisonEqual);
	S.SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(g_steam.mm, k_ELobbyDistanceFilterWorldwide);
	S.SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(g_steam.mm, 100);
	S.SteamAPI_ISteamMatchmaking_RequestLobbyList(g_steam.mm);
}

void Leave()
{
	AbortDownload();
	if (g_spectate.lobby || g_spectate.wanted) CloseSpectators();
	g_startedAt = 0;
	if (!g_lobby) {
		g_leave.at = 0;
		g_leave.active = false;
		return;
	}
	const uint64 left = g_lobby;
	S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, g_lobby);
	LogI("steam", "out of room %llu", g_lobby);
	g_lobby = 0;
	SetPresence();
	UpdateRoom();
	bool search = false;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_room = OnlineRoom();
		for (auto it = g_search.lobbies.begin(); it != g_search.lobbies.end();) {
			if (it->id == left || _strtoui64(Data(it->id, "main").c_str(), nullptr, 10) == left) it = g_search.lobbies.erase(it);
			else ++it;
		}
		if (!g_leave.skipRefresh && !g_search.loading) {
			g_search.loading = true;
			g_search.loadingSince = GetTickCount();
			search = true;
		}
	}
	g_leave.at = 0;
	g_leave.active = false;
	if (search) RequestLobbySearch();
}

void ResolvePendingJoin()
{
	const uint64 id = g_pendingJoin.id;
	g_pendingJoin.id = 0;
	if (!id) return;
	if (IsBanned(id, g_steam.me)) {
		LogW("steam", "not entering room %llu: the host kicked you out of it", id);
		Notice("You have been kicked from the room.");
		g_signal.joinRefused = true;
		return;
	}
	Join(id);
}

void Join(uint64 lobby)
{
	if (g_lobby == lobby && !g_leave.active) return;
	g_leave.skipRefresh = true;
	Leave();
	g_leave.skipRefresh = false;
	LogI("steam", "entering room %llu", lobby);
	g_pendingJoin.joiningLobby = lobby;
	S.SteamAPI_ISteamMatchmaking_JoinLobby(g_steam.mm, lobby);
}

}

namespace steamnet_impl {
namespace {
std::map<std::string, std::string> g_mainSent;

void SpecSet(const char *k, const std::string &v)
{
	std::string &old = g_spectate.sent[k];
	if (old == v && g_spectate.sent.count(k)) return;
	old = v;
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, k, v.c_str());
}

void MainSet(const char *k, const std::string &v)
{
	auto it = g_mainSent.find(k);
	if (it != g_mainSent.end() && it->second == v) return;
	g_mainSent[k] = v;
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, k, v.c_str());
}
}
}

namespace steamnet_impl {
void OpenSpectators(int delay, int visibility)
{
	g_spectate.wanted = true;
	g_spectate.delay = (std::max)(0, (std::min)(600, delay));
	g_spectate.visibility = visibility;
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	if (g_spectate.lobby || g_spectate.creating) return;
	g_spectate.creating = true;
	g_spectate.creatingSince = GetTickCount();
	const ELobbyType type = visibility == 1 ? k_ELobbyTypeFriendsOnly : visibility == 2 ? k_ELobbyTypePrivate : k_ELobbyTypePublic;
	S.SteamAPI_ISteamMatchmaking_CreateLobby(g_steam.mm, type, 250);
}

void CloseSpectators()
{
	g_spectate.wanted = false;
	if (g_spectate.lobby) {
		S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, g_spectate.lobby);
		LogI("steam", "spectator room closed");
		g_spectate.lobby = 0;
		if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "sl", "");
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "sd", "");
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "t0", "");
		}
	}
	g_spectate.sent.clear();
	g_mainSent.clear();
	std::lock_guard<std::mutex> lock(g_spectate.mx);
	g_spectate.ids.clear();
}

void MirrorSpectators()
{
	if (g_spectate.creating && GetTickCount() - g_spectate.creatingSince > kSearchPatienceMs) {
		g_spectate.creating = false;
		LogI("steam", "the spectator room was never created; trying again");
	}
	if (g_spectate.wanted && !g_spectate.lobby && !g_spectate.creating && g_lobby) OpenSpectators(g_spectate.delay, g_spectate.visibility);
	if (!g_spectate.lobby) return;
	if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
	static const char *const kCopy[] = {"name", "host", "hostid", "map", "mode", "key", "scen", "pw", "vis", "irver", "mods", "slots", "plist", "opts", "rules", "save", "playing", "modsv", "nopt"};
	for (const char *k : kCopy) SpecSet(k, Data(g_lobby, k));
	SpecSet("main", std::to_string(g_lobby));
	SpecSet("delay", std::to_string(g_spectate.delay));
	SpecSet("t0", g_startedAt ? std::to_string(g_startedAt) : "");
	MainSet("sl", std::to_string(g_spectate.lobby));
	MainSet("sd", std::to_string(g_spectate.delay));
	MainSet("t0", g_startedAt ? std::to_string(g_startedAt) : "");
	static DWORD lastBeat = 0;
	if (GetTickCount() - lastBeat > 30000) {
		lastBeat = GetTickCount();
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "alive", std::to_string(static_cast<long long>(time(nullptr))).c_str());
	}
	std::vector<uint64_t> ids;
	const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_spectate.lobby);
	for (int i = 0; i < n; i++) {
		const uint64 m = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_spectate.lobby, i);
		if (m != g_steam.me) ids.push_back(m);
	}
	std::lock_guard<std::mutex> lock(g_spectate.mx);
	g_spectate.ids = std::move(ids);
}

void FoldSpectator(OnlineLobby &row, const OnlineLobby &s)
{
	row.specLobby = s.id;
	row.spectators = s.spectators;
	row.specDelay = s.specDelay;
	if (s.started) row.started = s.started;
	if (s.started || Data(s.id, "playing") == "1") row.playing = true;
}

void MergeSpectateLobbies(std::vector<OnlineLobby> &list)
{
	std::vector<OnlineLobby> specs, rest;
	for (const OnlineLobby &l : list) (l.spectate ? specs : rest).push_back(l);
	for (OnlineLobby &s : specs) {
		const uint64 mainId = _strtoui64(Data(s.id, "main").c_str(), nullptr, 10);
		bool merged = false;
		for (OnlineLobby &l : rest) {
			if (l.id != mainId) continue;
			FoldSpectator(l, s);
			merged = true;
		}
		if (merged) continue;
		if (mainId && mainId != g_lobby && Data(mainId, "imperivm") == "1" && Data(mainId, "v") == kProtocol &&
			_strtoui64(Data(mainId, "hostid").c_str(), nullptr, 10) != g_steam.me && !StaleLobby(mainId)) {
			OnlineLobby mainLobby = ReadLobby(mainId);
			FoldSpectator(mainLobby, s);
			rest.push_back(mainLobby);
			continue;
		}
		s.spectate = false;
		s.specOnly = true;
		s.specLobby = s.id;
		s.playing = Data(s.id, "playing") == "1" || s.started != 0;
		rest.push_back(s);
	}
	list = std::move(rest);
}
}
