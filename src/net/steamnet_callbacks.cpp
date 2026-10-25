// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include "core/log.h"
#include "mods/mods.h"
#include "hooks/dmshim.h"
#include "net/modlist.h"
#include "net/vnet.h"
#include "i18n/tr.h"

#include "net/steamnet_internal.h"
#include "core/uiport.h"
#include "core/version.h"

using namespace steamnet_impl;

namespace steamnet_impl {
void PublishMods(const std::string &scenModId)
{
	if (!g_lobby) return;
	int optional = 0;
	const std::string list = EncodeMods(scenModId, optional);
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "mods", list.c_str());
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "modsv", "2");
	S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "nopt", std::to_string(optional).c_str());
	LogI("steam", "required content of the room: %u packages; unpublished optional: %d", static_cast<unsigned>(DecodeModList(list).size()), optional);
}
}

namespace steamnet_impl {
namespace {
void StoreLobbyEntry(uint64 lobby)
{
	OnlineLobby l = ReadLobby(lobby);
	l.friendHost = l.friendHost || std::find(g_search.friendLobbies.begin(), g_search.friendLobbies.end(), lobby) != g_search.friendLobbies.end();
	std::lock_guard<std::mutex> lock(g_lock);
	for (auto it = g_search.lobbies.begin(); it != g_search.lobbies.end();) {
		if (it->specOnly && _strtoui64(Data(it->id, "main").c_str(), nullptr, 10) == lobby) {
			FoldSpectator(l, *it);
			it = g_search.lobbies.erase(it);
		}
		else {
			++it;
		}
	}
	bool replaced = false;
	for (OnlineLobby &old : g_search.lobbies) {
		if (old.id == lobby) {
			if (!l.specLobby && old.specLobby) {
				l.specLobby = old.specLobby;
				l.spectators = old.spectators;
				l.specDelay = old.specDelay;
				if (!l.started) l.started = old.started;
				if (l.started || Data(old.specLobby, "playing") == "1") l.playing = true;
			}
			old = l;
			replaced = true;
		}
	}
	if (!replaced) g_search.lobbies.push_back(l);
}

bool ListedLobby(uint64 lobby)
{
	std::lock_guard<std::mutex> lock(g_lock);
	for (const OnlineLobby &l : g_search.lobbies) {
		if (l.id == lobby) return true;
	}
	return false;
}

void OnCallback(int id, const void *data)
{
	switch (id) {
	case LobbyCreated_t::k_iCallback: {
		auto *r = static_cast<const LobbyCreated_t *>(data);
		if (r->m_eResult != k_EResultOK) {
			if (g_spectate.creating) {
				g_spectate.creating = false;
				LogW("steam", "could not create the spectator room (code %d)", static_cast<int>(r->m_eResult));
				return;
			}
			Notice(Tr("Steam could not create the room (code ") + std::to_string(r->m_eResult) + ")");
			g_signal.createFailed = true;
			return;
		}
		if (g_lobby == r->m_ulSteamIDLobby || g_spectate.lobby == r->m_ulSteamIDLobby) return;
		if (g_spectate.creating) {
			g_spectate.creating = false;
			if (!g_spectate.wanted || !g_lobby) {
				S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, r->m_ulSteamIDLobby);
				return;
			}
			g_spectate.lobby = r->m_ulSteamIDLobby;
			g_spectate.sent.clear();
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "imperivm", "1");
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "v", kProtocol);
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "spec", "1");
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "main", std::to_string(g_lobby).c_str());
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_spectate.lobby, "alive", std::to_string(static_cast<long long>(time(nullptr))).c_str());
			LogI("steam", "spectator room created (%llu)", g_spectate.lobby);
			return;
		}
		g_lobby = r->m_ulSteamIDLobby;
		g_membership++;
		auto set = [&](const char *k, const std::string &v) { S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, k, v.c_str()); };
		set("imperivm", "1");
		set("v", kProtocol);
		set("name", g_creating.name);
		set("host", g_myName);
		set("hostid", std::to_string(g_steam.me));
		set("map", g_creating.map);
		set("mode", g_creating.mode);
		set("pw", g_creating.password.empty() ? "" : Fnv(g_creating.password));
		set("vis", std::to_string(g_creating.visibility));
		set("key", OwnKey());
		if (!g_creating.civSets.empty()) set("civsets", g_creating.civSets);
		PublishMods("");
		set("irver", ModVersionText());
		set("irlvl", ModsRulesLevel());
		set(("k:" + std::to_string(g_steam.me)).c_str(), "1");
		LogI("steam", "room \"%s\" created (%llu)", g_creating.name.c_str(), g_lobby);
		LogI("steam", "room created");
		SetPresence();
		UpdateRoom();
		g_spectate.visibility = g_creating.visibility;
		if (g_creating.spectators) OpenSpectators(g_creating.specDelay, g_creating.visibility);
		break;
	}
	case LobbyMatchList_t::k_iCallback: {
		auto *r = static_cast<const LobbyMatchList_t *>(data);
		std::vector<OnlineLobby> list;
		for (uint32 i = 0; i < r->m_nLobbiesMatching; i++) {
			const uint64 lobby = S.SteamAPI_ISteamMatchmaking_GetLobbyByIndex(g_steam.mm, static_cast<int>(i));
			if (Data(lobby, "v") != kProtocol) continue;
			if (Data(lobby, "name").empty()) {
				static std::set<uint64> reported;
				if (reported.insert(lobby).second) {
					LogW("steam", "room without data ignored %llu (%d members, %s)", lobby, S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, lobby),
						Data(lobby, "spec") == "1" ? "of spectators" : "main");
				}
				if (lobby != g_lobby && lobby != g_spectate.lobby) S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
				continue;
			}
			if (lobby == g_lobby || lobby == g_spectate.lobby) continue;
			if (_strtoui64(Data(lobby, "hostid").c_str(), nullptr, 10) == g_steam.me) continue;
			if (StaleLobby(lobby)) continue;
			list.push_back(ReadLobby(lobby));
		}
		MergeSpectateLobbies(list);
		g_search.friendLobbies.clear();
		const uint32 app = S.SteamAPI_ISteamUtils_GetAppID(S.SteamAPI_SteamUtils_v011());
		const int friends = S.SteamAPI_ISteamFriends_GetFriendCount(g_steam.friends, k_EFriendFlagImmediate);
		for (int i = 0; i < friends && i < 500; i++) {
			const uint64 f = S.SteamAPI_ISteamFriends_GetFriendByIndex(g_steam.friends, i, k_EFriendFlagImmediate);
			FriendGameInfo_t gi = {};
			if (!S.SteamAPI_ISteamFriends_GetFriendGamePlayed(g_steam.friends, f, &gi) || gi.m_gameID.AppID() != app) continue;
			const uint64 lobby = gi.m_steamIDLobby.ConvertToUint64();
			if (!gi.m_steamIDLobby.IsValid() || lobby == g_lobby) continue;
			bool known = false;
			for (OnlineLobby &l : list) {
				if (l.id == lobby) known = true;
			}
			if (known) continue;
			g_search.friendLobbies.push_back(lobby);
			S.SteamAPI_ISteamMatchmaking_RequestLobbyData(g_steam.mm, lobby);
		}
		static DWORD lastLog = 0;
		if (GetTickCount() - lastLog > 2000) {
			lastLog = GetTickCount();
			LogD("steam", "%u rooms found", static_cast<unsigned>(list.size()));
		}
		std::lock_guard<std::mutex> lock(g_lock);
		g_search.lobbies = list;
		g_search.loading = false;
		break;
	}
	case LobbyEnter_t::k_iCallback: {
		auto *r = static_cast<const LobbyEnter_t *>(data);
		if (r->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
			const int code = static_cast<int>(r->m_EChatRoomEnterResponse);
			if (r->m_ulSteamIDLobby == g_pendingJoin.joiningLobby) g_pendingJoin.joiningLobby = 0;
			if (g_signal.rejoinJoining) {
				if (g_signal.joinFailCode.exchange(code) == 0) LogW("steam", "could not enter the room (code %d); will retry", code);
				return;
			}
			LogW("steam", "could not enter the room (code %d)", code);
			Notice("Couldn't join the room (full, closed, or it no longer exists)");
			return;
		}
		g_signal.rejoinJoining = false;
		const uint64 lobby = r->m_ulSteamIDLobby;
		if (lobby == g_lobby) return;
		if (S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, lobby) == g_steam.me) {
			return;
		}
		if (lobby != g_pendingJoin.joiningLobby) {
			S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
			LogW("steam", "late entry to room %llu ignored (it was no longer the wanted one)", lobby);
			return;
		}
		g_pendingJoin.joiningLobby = 0;
		std::string password;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			password = g_pendingJoin.password;
		}
		if (IsBanned(lobby, g_steam.me)) {
			S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
			LogW("steam", "not entering room %llu: the host kicked you out of it", lobby);
			Notice("You have been kicked from the room.");
			g_signal.joinRefused = true;
			return;
		}
		const std::string pw = Data(lobby, "pw");
		if (!pw.empty() && !g_pendingJoin.invite && Fnv(password) != pw) {
			S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
			Notice("Wrong password");
			return;
		}
		const std::string mismatch = SteamVersionMismatch(Data(lobby, "irver"), Data(lobby, "irlvl"));
		if (!mismatch.empty()) {
			S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
			LogW("steam", "not entering the room: %s", mismatch.c_str());
			Notice(mismatch);
			return;
		}
		if (lobby != g_pendingJoin.syncLobby) {
			const OnlineLobby info = ReadLobby(lobby);
			const RoomContent content = SteamRoomContent(info, OwnKey());
			if (RoomContentDiffers(content)) {
				if (!info.spectate) StoreLobbyEntry(lobby);
				S.SteamAPI_ISteamMatchmaking_LeaveLobby(g_steam.mm, lobby);
				RefuseForContent(lobby, info, content);
				return;
			}
		}
		g_lobby = lobby;
		g_membership++;
		LogI("steam", "inside room \"%s\" of %s", Data(lobby, "name").c_str(), Data(lobby, "host").c_str());
		SetPresence();
		UpdateRoom();
		break;
	}
	case LobbyDataUpdate_t::k_iCallback: {
		auto *r = static_cast<const LobbyDataUpdate_t *>(data);
		const uint64 lobby = r->m_ulSteamIDLobby;
		if (g_pendingJoin.id && lobby == g_pendingJoin.id && r->m_ulSteamIDMember == lobby) ResolvePendingJoin();
		if (r->m_bSuccess && r->m_ulSteamIDMember == lobby && lobby != g_lobby && lobby != g_spectate.lobby &&
			(std::find(g_search.friendLobbies.begin(), g_search.friendLobbies.end(), lobby) != g_search.friendLobbies.end() || ListedLobby(lobby)) &&
			Data(lobby, "imperivm") == "1" && Data(lobby, "v") == kProtocol && Data(lobby, "spec") != "1" &&
							_strtoui64(Data(lobby, "hostid").c_str(), nullptr, 10) != g_steam.me && !StaleLobby(lobby)) {
			StoreLobbyEntry(lobby);
		}
		if (r->m_ulSteamIDMember == lobby) {
			std::lock_guard<std::mutex> lock(g_lock);
			g_search.dataSerial[lobby]++;
		}
		AssignNumbers();
		UpdateRoom();
		break;
	}
	case LobbyChatUpdate_t::k_iCallback: {
		auto *r = static_cast<const LobbyChatUpdate_t *>(data);
		const unsigned leftFlags = k_EChatMemberStateChangeLeft | k_EChatMemberStateChangeDisconnected | k_EChatMemberStateChangeKicked |
			k_EChatMemberStateChangeBanned;
		if (r->m_ulSteamIDLobby == g_lobby && (r->m_rgfChatMemberStateChange & leftFlags)) {
			const unsigned f = r->m_rgfChatMemberStateChange;
			LogI("steam", "%s leaves the room: %s", Name(r->m_ulSteamIDUserChanged).c_str(),
				(f & k_EChatMemberStateChangeDisconnected) ? "lost the connection to Steam" : (f & k_EChatMemberStateChangeKicked) ? "kicked" :
				(f & k_EChatMemberStateChangeBanned) ? "banned" : "left on their own (closed the game or left the room)");
		}
		if (r->m_ulSteamIDLobby == g_lobby && (r->m_rgfChatMemberStateChange & leftFlags) &&
			S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
			DropTransfers(r->m_ulSteamIDUserChanged);
		}
		AssignNumbers();
		UpdateRoom();
		break;
	}
	case LobbyChatMsg_t::k_iCallback: {
		auto *r = static_cast<const LobbyChatMsg_t *>(data);
		if (r->m_ulSteamIDLobby != g_lobby) return;
		CSteamID user;
		char text[1024] = {};
		EChatEntryType type;
		const int n = S.SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(g_steam.mm, g_lobby, static_cast<int>(r->m_iChatID), &user, text,
			sizeof(text) - 1, &type);
		if (n > 0 && type == k_EChatEntryTypeChatMsg) {
			AddChatLine(user.ConvertToUint64(), Name(user.ConvertToUint64()), std::string(text, strnlen(text, n)));
			if (user.ConvertToUint64() != g_steam.me) UiSoundPlay(0);
		}
		break;
	}
	case LobbyInvite_t::k_iCallback: {
		auto *r = static_cast<const LobbyInvite_t *>(data);
		LogI("steam", "invitation received from %s", Name(r->m_ulSteamIDUser).c_str());
		Notice(Name(r->m_ulSteamIDUser) + Tr(" invited you to a game"));
		UiSoundPlay(1);
		break;
	}
	case GameLobbyJoinRequested_t::k_iCallback: {
		auto *r = static_cast<const GameLobbyJoinRequested_t *>(data);
		LogI("steam", "invitation accepted from Steam");
		g_pendingJoin.invite = true;
		Join(r->m_steamIDLobby.ConvertToUint64());
		break;
	}
	case SteamNetworkingMessagesSessionRequest_t::k_iCallback: {
		auto *r = static_cast<const SteamNetworkingMessagesSessionRequest_t *>(data);
		const uint64 who = r->m_identityRemote.GetSteamID64();
		bool member = false;
		if (g_lobby) {
			const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby);
			for (int i = 0; i < n; i++) member = member || S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, i) == who;
		}
		if (!member && g_rejoinAllowed.count(who)) member = true;
		if (!member && VnetHasPeer(who)) member = true;
		if (!member && g_spectate.lobby && InLobby(g_spectate.lobby, who)) member = true;
		if (!member && g_lobby && Data(g_lobby, "playing") == "1" && !Data(g_lobby, ("k:" + std::to_string(who)).c_str()).empty()) member = true;
		if (member) S.SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(g_steam.msgs, r->m_identityRemote);
		LogI("steam", "connection of %s %s", Name(who).c_str(), member ? "accepted" : "rejected (not in the room)");
		break;
	}
	case SteamServersDisconnected_t::k_iCallback:
		LogW("steam", "this computer lost the connection to the Steam servers (result %d)",
			static_cast<int>(static_cast<const SteamServersDisconnected_t *>(data)->m_eResult));
		break;
	case SteamServersConnected_t::k_iCallback:
		LogI("steam", "this computer is connected to the Steam servers");
		break;
	case SteamServerConnectFailure_t::k_iCallback: {
		auto *r = static_cast<const SteamServerConnectFailure_t *>(data);
		LogW("steam", "this computer cannot connect to the Steam servers (result %d, %s)", static_cast<int>(r->m_eResult),
			r->m_bStillRetrying ? "still trying" : "no longer retrying");
		break;
	}
	case SteamRelayNetworkStatus_t::k_iCallback: {
		auto *r = static_cast<const SteamRelayNetworkStatus_t *>(data);
		static int lastAvail = -1000, lastRelay = -1000;
		if (lastAvail != static_cast<int>(r->m_eAvail) || lastRelay != static_cast<int>(r->m_eAvailAnyRelay)) {
			lastAvail = static_cast<int>(r->m_eAvail);
			lastRelay = static_cast<int>(r->m_eAvailAnyRelay);
			LogD("steam", "relay network of this computer: state %d, some relay %d (%s)", lastAvail, lastRelay, r->m_debugMsg);
		}
		break;
	}
	case SteamNetworkingMessagesSessionFailed_t::k_iCallback: {
		auto *r = static_cast<const SteamNetworkingMessagesSessionFailed_t *>(data);
		LogW("steam", "connection with %s failed (%d: %s)", Name(r->m_info.m_identityRemote.GetSteamID64()).c_str(),
			r->m_info.m_eEndReason, r->m_info.m_szEndDebug);
		break;
	}
	}
}
}
}

namespace steamnet_impl {
void DispatchCallbacks()
{
	S.SteamAPI_ManualDispatch_RunFrame(g_steam.pipe);
	CallbackMsg_t msg;
	while (S.SteamAPI_ManualDispatch_GetNextCallback(g_steam.pipe, &msg)) {
		if (msg.m_iCallback == SteamAPICallCompleted_t::k_iCallback) {
			auto *done = reinterpret_cast<SteamAPICallCompleted_t *>(msg.m_pubParam);
			std::vector<char> result(done->m_cubParam);
			bool failed = false;
			if (S.SteamAPI_ManualDispatch_GetAPICallResult(g_steam.pipe, done->m_hAsyncCall, result.data(), done->m_cubParam,
					done->m_iCallback, &failed) &&
				!failed) {
				OnCallback(done->m_iCallback, result.data());
			}
		}
		else {
			OnCallback(msg.m_iCallback, msg.m_pubParam);
		}
		S.SteamAPI_ManualDispatch_FreeLastCallback(g_steam.pipe);
	}
}
}

namespace steamnet_impl {
void RefuseForContent(uint64 lobby, const OnlineLobby &info, RoomContent content)
{
	if (content == RoomContent::kOtherMap) {
		LogW("steam", "room %llu: its map comes from \"%s\", which you do not have loaded; not staying in it", lobby, info.scenModName.c_str());
		if (!UiMultiplayerOpen()) Notice(TrFmt("This room uses the map from \"%s\": join from the game list to download it", {info.scenModName}));
	}
	else if (info.spectate) {
		LogW("steam", "spectator room %llu: its gameplay content does not match yours; not staying in it", lobby);
		Notice("This game uses different content. Join it from the game list to download it.");
	}
	else {
		LogW("steam", "room %llu: its gameplay content does not match yours; not staying in it", lobby);
		if (!UiMultiplayerOpen()) Notice("This room uses different content: join from the game list to synchronize it");
	}
	g_resync.at = GetTickCount();
	g_resync.lobby = lobby;
}

void RememberLobby(uint64 lobby)
{
	StoreLobbyEntry(lobby);
}
}
