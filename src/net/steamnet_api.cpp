// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include "core/guard.h"
#include "mods/mods.h"
#include "i18n/tr.h"

#include "net/steamnet_internal.h"
#include "core/version.h"

using namespace steamnet_impl;

void StartSteamOnline()
{
	SafeThread(SteamThread).detach();
}

bool SteamOnlineReady()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_ready;
}

std::string SteamOnlineStatus()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_status;
}

std::string SteamOnlinePlayerName()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_myName;
}

uint64_t SteamOwnId()
{
	return SteamOnlineReady() ? g_steam.me : 0;
}

std::map<uint64_t, std::shared_ptr<const OnlineAvatar>> g_avatars;
std::map<uint64_t, DWORD> g_avatarAsked;

void LoadAvatar(uint64 id)
{
	const int image = S.SteamAPI_ISteamFriends_GetMediumFriendAvatar(g_steam.friends, id);
	if (image == 0) {
		S.SteamAPI_ISteamFriends_RequestUserInformation(g_steam.friends, id, false);
		return;
	}
	if (image < 0) return;
	ISteamUtils *utils = S.SteamAPI_SteamUtils_v011();
	uint32 w = 0, h = 0;
	if (!S.SteamAPI_ISteamUtils_GetImageSize(utils, image, &w, &h) || !w || !h || w > 256 || h > 256) return;
	std::vector<uint8_t> rgba(w * h * 4);
	if (!S.SteamAPI_ISteamUtils_GetImageRGBA(utils, image, rgba.data(), static_cast<int>(rgba.size()))) return;
	auto a = std::make_shared<OnlineAvatar>();
	a->width = static_cast<int>(w);
	a->height = static_cast<int>(h);
	a->bgra.resize(w * h);
	for (size_t i = 0; i < a->bgra.size(); i++) {
		const uint8_t *p = &rgba[i * 4];
		a->bgra[i] = (static_cast<uint32_t>(p[3]) << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
	}
	std::lock_guard<std::mutex> lock(g_lock);
	g_avatars[id] = a;
}

std::shared_ptr<const OnlineAvatar> SteamAvatar(uint64_t id)
{
	if (!id || !SteamOnlineReady()) return nullptr;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto it = g_avatars.find(id);
		if (it != g_avatars.end()) return it->second;
		DWORD &asked = g_avatarAsked[id];
		if (asked && GetTickCount() - asked < 2000) return nullptr;
		asked = GetTickCount();
	}
	Post([id] { LoadAvatar(id); });
	return nullptr;
}

void SteamRefreshLobbies()
{
	if (!SteamOnlineReady()) return;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_search.loading) {
			if (GetTickCount() - g_search.loadingSince < kSearchPatienceMs) return;
			LogW("steam", "the room search did not respond; repeating");
		}
		g_search.loading = true;
		g_search.loadingSince = GetTickCount();
	}
	Post([] { RequestLobbySearch(); });
}

void SteamRequestLobbyData(uint64_t id)
{
	if (!id || !SteamOnlineReady()) return;
	Post([id] { S.SteamAPI_ISteamMatchmaking_RequestLobbyData(g_steam.mm, id); });
}

unsigned SteamLobbyDataSerial(uint64_t id)
{
	std::lock_guard<std::mutex> lock(g_lock);
	const auto it = g_search.dataSerial.find(id);
	return it == g_search.dataSerial.end() ? 0 : it->second;
}

uint64_t SteamTakeResync()
{
	const unsigned long long id = g_resync.lobby.exchange(0);
	return id && GetTickCount() - g_resync.at < 8000 ? id : 0;
}

bool SteamLobbiesLoading()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_search.loading && GetTickCount() - g_search.loadingSince < kSearchPatienceMs;
}

std::vector<OnlineLobby> SteamLobbies()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_search.lobbies;
}

void SteamCreateLobby(const OnlineCreateSettings &s)
{
	if (!SteamOnlineReady()) return;
	Post([s] {
		g_leave.skipRefresh = true;
		Leave();
		g_leave.skipRefresh = false;
		g_creating = s;
		const ELobbyType type = s.visibility == 1 ? k_ELobbyTypeFriendsOnly : s.visibility == 2 ? k_ELobbyTypePrivate : k_ELobbyTypePublic;
		S.SteamAPI_ISteamMatchmaking_CreateLobby(g_steam.mm, type, std::max(2, std::min(8, s.maxPlayers)));
	});
}

void SteamJoinLobby(uint64_t id, const std::string &password, bool forContentSync)
{
	if (!SteamOnlineReady()) return;
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_pendingJoin.password = password;
	}
	Post([id, forContentSync] {
		g_pendingJoin.invite = false;
		g_pendingJoin.syncLobby = forContentSync ? id : 0;
		if (id == g_lobby && !g_leave.active) return;
		g_pendingJoin.id = id;
		g_pendingJoin.deadline = GetTickCount() + 1500;
		S.SteamAPI_ISteamMatchmaking_RequestLobbyData(g_steam.mm, id);
	});
}

bool SteamTakeJoinRefused()
{
	return g_signal.joinRefused.exchange(false);
}

bool SteamTakeCreateFailed()
{
	return g_signal.createFailed.exchange(false);
}

uint32_t SteamMembership()
{
	return g_membership;
}

void SteamLeaveLobby(uint32_t onlyIfMembership)
{
	if (!SteamOnlineReady()) return;
	Post([onlyIfMembership] {
		if (!g_lobby) {
			Leave();
			return;
		}
		if (onlyIfMembership && g_membership != onlyIfMembership) return;
		g_leave.active = true;
		g_leave.at = GetTickCount() + 500;
	});
}

OnlineRoom SteamCurrentRoom()
{
	if (g_leave.active) return OnlineRoom();
	std::lock_guard<std::mutex> lock(g_lock);
	OnlineRoom room = g_room;
	for (OnlineMember &m : room.members) {
		const auto it = g_share.sendProgress.find(m.id);
		if (it != g_share.sendProgress.end() && it->second.second) {
			m.receiving = static_cast<int>(it->second.first * 100 / it->second.second);
			m.receivingBytes = it->second.second;
		}
		else {
			const auto ext = g_share.extProgress.find(m.id);
			if (ext != g_share.extProgress.end() && GetTickCount() - ext->second.stamp < 6000) {
				m.receiving = static_cast<int>(ext->second.got * 100 / ext->second.total);
				m.receivingBytes = ext->second.total;
			}
		}
	}
	return room;
}

void SteamSendChat(const std::string &text)
{
	if (!SteamOnlineReady() || text.empty()) return;
	Post([text] {
		if (g_lobby) S.SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(g_steam.mm, g_lobby, text.c_str(), static_cast<int>(text.size()) + 1);
	});
}

void SteamInviteFriends()
{
	if (!SteamOnlineReady()) return;
	Post([] {
		if (g_lobby) S.SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(g_steam.friends, g_lobby);
	});
}

std::string SteamTakeNotice()
{
	std::lock_guard<std::mutex> lock(g_lock);
	std::string n = g_notice;
	g_notice.clear();
	return n;
}

std::string SteamOwnGameplayKey()
{
	return OwnKey();
}

RoomContent SteamRoomContent(const OnlineLobby &lobby, const std::string &ownKey, bool strictMap, bool watching)
{
	RoomContentFacts facts;
	facts.roomKey = lobby.gameplayKey;
	facts.ownKey = ownKey;
	facts.mapModId = lobby.scenModId;
	facts.haveMapMod = !lobby.scenModId.empty() && ModsMountedHasExact(lobby.scenModId, lobby.scenModVersion);
	const bool viewer = watching || lobby.spectate || lobby.specOnly;
	facts.mapSimple = !strictMap && lobby.scenSimple && (viewer || (!lobby.playing && !lobby.started));
	return CompareRoomContent(facts);
}

void SteamLeaveForContent(bool strictMap)
{
	if (!SteamOnlineReady()) return;
	Post([strictMap] {
		if (!g_lobby) return;
		const uint64 lobby = g_lobby;
		const OnlineLobby info = ReadLobby(lobby);
		const RoomContent content = SteamRoomContent(info, OwnKey(), strictMap);
		if (!RoomContentDiffers(content)) return;
		if (!info.spectate) RememberLobby(lobby);
		Leave();
		RefuseForContent(lobby, info, content);
	});
}

void SteamSetHosting(bool hosting)
{
	if (!SteamOnlineReady()) return;
	Post([hosting] {
		if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "hosting", hosting ? "1" : "");
			LogI("steam", "game %s", hosting ? "created in the game: the others can join now" : "closed");
		}
	});
}

void SteamPublishOptions(const std::string &text)
{
	if (!SteamOnlineReady()) return;
	static std::string pending;
	static DWORD pendingTick = 0;
	if (text == pending && GetTickCount() - pendingTick < 2000) return;
	pending = text;
	pendingTick = GetTickCount();
	Post([text] {
		static std::string last;
		static uint64 lastLobby = 0;
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		if (text == last && g_lobby == lastLobby) return;
		last = text;
		lastLobby = g_lobby;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "opts", text.c_str());
	});
}

void SteamPublishRules(unsigned rules)
{
	if (!SteamOnlineReady()) return;
	char text[16];
	sprintf_s(text, "%x", rules);
	const std::string value = text;
	Post([value] {
		static std::string last;
		static uint64 lastLobby = 0;
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		if (value == last && g_lobby == lastLobby) return;
		last = value;
		lastLobby = g_lobby;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "rules", value.c_str());
	});
}

void SteamPublishSlots(const std::string &text)
{
	if (!SteamOnlineReady()) return;
	static std::string pending;
	static DWORD pendingTick = 0;
	if (text == pending && GetTickCount() - pendingTick < 2000) return;
	pending = text;
	pendingTick = GetTickCount();
	Post([text] {
		static std::string last;
		static uint64 lastLobby = 0;
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		if (text == last && g_lobby == lastLobby) return;
		last = text;
		lastLobby = g_lobby;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "slots", text.c_str());
	});
}

void SteamPublishSaveColors(const std::string &text)
{
	if (!SteamOnlineReady()) return;
	static std::string pending;
	static uint32_t pendingMembership = 0;
	if (text == pending && SteamMembership() == pendingMembership) return;
	pending = text;
	pendingMembership = SteamMembership();
	Post([text] {
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "savecolors", text.c_str());
	});
}

void SteamRequestMods(const std::vector<OnlineMod> &mods)
{
	if (!SteamOnlineReady()) return;
	Post([mods] {
		if (!g_lobby) return;
		AbortDownload();
		g_share.in = InTransfer();
		g_share.in.host = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
		g_share.in.tempRoot = ModsFolderPath() + "\\_downloads\\room";
		DeleteTree(g_share.in.tempRoot);
		MakeDirs(g_share.in.tempRoot);
		std::string text;
		uint64_t total = 0;
		for (const OnlineMod &m : mods) {
			text += m.id + "\t" + m.version + "\n";
			total += m.bytes;
		}
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_share.download = OnlineModDownload();
			g_share.download.active = true;
			g_share.download.total = total;
		}
		if (ShareSend(g_share.in.host, kShReq, text.data(), text.size()) != k_EResultOK) {
			FailDownload("could not contact the host");
			return;
		}
		LogI("content-share", "mods over steam: %u mods requested from the host (%llu bytes)", static_cast<unsigned>(mods.size()), total);
	});
}

OnlineModDownload SteamModDownload()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_share.download;
}

void SteamResetModDownload()
{
	std::lock_guard<std::mutex> lock(g_lock);
	g_share.download = OnlineModDownload();
}

void SteamSetRejoin(uint64_t lobby)
{
	char value[64];
	sprintf_s(value, "%llu %llu", static_cast<unsigned long long>(lobby), static_cast<unsigned long long>(time(nullptr)));
	WritePrivateProfileStringA("Online", "ReturnToRoom", value, IniPath().c_str());
}

void SteamPublishScenario(const std::string &path)
{
	if (!SteamOnlineReady()) return;
	Post([path] { PublishScenario(path); });
}

OnlineMapStatus SteamMapStatus()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_share.mapStatus;
}

void SteamPublishSave(const std::string &file, const std::string &name)
{
	if (!SteamOnlineReady()) return;
	Post([file, name] { PublishSave(file, name); });
}

OnlineMapStatus SteamSaveStatus()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_share.saveStatus;
}

void SteamPublishWatchers(int count)
{
	if (!SteamOnlineReady()) return;
	Post([count] {
		static int last = -1;
		static uint64 lastLobby = 0;
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		if (count == last && g_lobby == lastLobby) return;
		last = count;
		lastLobby = g_lobby;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "watching", count > 0 ? std::to_string(count).c_str() : "");
	});
}

int SteamRoomWatchers()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_room.info.watching;
}

void SteamSetPlaying(bool playing)
{
	if (!SteamOnlineReady()) return;
	Post([playing] {
		if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
			S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "playing", playing ? "1" : "");
			S.SteamAPI_ISteamMatchmaking_SetLobbyJoinable(g_steam.mm, g_lobby, !playing);
			g_startedAt = playing ? static_cast<long long>(time(nullptr)) : 0;
			MirrorSpectators();
			LogI("steam", "room %s", playing ? "in game (closed to new players)" : "open again");
		}
	});
}

void SteamSetSpectators(bool allow, int delaySeconds)
{
	if (!SteamOnlineReady()) return;
	Post([allow, delaySeconds] {
		if (allow) OpenSpectators(delaySeconds, g_spectate.visibility);
		else CloseSpectators();
	});
}

bool SteamSpectatorsOpen(int &delaySeconds)
{
	std::lock_guard<std::mutex> lock(g_spectate.mx);
	delaySeconds = g_spectate.delay;
	return g_spectate.wanted && g_spectate.lobby != 0;
}

long long SteamGameStartedAt()
{
	return *reinterpret_cast<volatile long long *>(&g_startedAt);
}

std::vector<uint64_t> SteamSpectatorIds()
{
	std::lock_guard<std::mutex> lock(g_spectate.mx);
	return g_spectate.ids;
}

void SteamSpectatorSend(uint64_t peer, uint8_t type, const std::string &data)
{
	if (!SteamOnlineReady() || !peer) return;
	Post([peer, type, data] {
		g_share.rejoinOut.push_back({peer, type, data});
		PumpRejoinOut();
	});
}

void SteamResetStaleSession(uint64_t peer)
{
	if (!SteamOnlineReady() || !peer) return;
	Post([peer] {
		SteamNetworkingIdentity who = Identity(peer);
		SteamNetConnectionInfo_t info = {};
		SteamNetConnectionRealTimeStatus_t status = {};
		const ESteamNetworkingConnectionState st =
			S.SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo(g_steam.msgs, who, &info, &status);
		if (st != k_ESteamNetworkingConnectionState_ProblemDetectedLocally && st != k_ESteamNetworkingConnectionState_ClosedByPeer) return;
		S.SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(g_steam.msgs, who);
		LogI("steam", "restarting the session with %s, which was broken (reason %d: %s)", Name(peer).c_str(), static_cast<int>(info.m_eEndReason), info.m_szEndDebug);
	});
}

void SteamKick(uint64_t member)
{
	if (!SteamOnlineReady() || !member) return;
	Post([member] {
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me || member == g_steam.me) return;
		std::string list = Data(g_lobby, "kick");
		if (list.empty()) list = ";";
		const std::string id = std::to_string(member);
		if (list.find(";" + id + ";") == std::string::npos) list += id + ";";
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "kick", list.c_str());
		DropTransfers(member);
		LogI("steam", "%s kicked from the room", Name(member).c_str());
		UpdateRoom();
	});
}

void SteamRequestSeat(int slot)
{
	if (!SteamOnlineReady()) return;
	Post([slot] {
		if (!g_lobby) return;
		const uint64 owner = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
		if (!owner || owner == g_steam.me) return;
		const std::string text = std::to_string(slot);
		ShareSend(owner, kShSeat, text.data(), text.size());
		LogD("room", "asking the host for slot %d", slot + 1);
	});
}

void SteamSetMemberLimit(int n)
{
	if (!SteamOnlineReady() || n <= 0) return;
	static int pending = -1;
	static uint32_t pendingMembership = 0;
	if (n == pending && SteamMembership() == pendingMembership) return;
	pending = n;
	pendingMembership = SteamMembership();
	Post([n] {
		if (!g_lobby || S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) != g_steam.me) return;
		if (S.SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(g_steam.mm, g_lobby) == n) return;
		S.SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(g_steam.mm, g_lobby, n);
		LogI("steam", "the room now admits %d players", n);
	});
}

void SteamSendSwap(uint64_t to, const std::string &text)
{
	if (!SteamOnlineReady() || !to) return;
	Post([to, text] {
		if (to == g_steam.me) {
			std::lock_guard<std::mutex> lock(g_seats.mx);
			g_seats.swaps.push_back({g_steam.me, text});
			return;
		}
		ShareSend(to, kShSwap, text.data(), text.size());
	});
}

bool SteamTakeSwap(uint64_t &from, std::string &text)
{
	std::lock_guard<std::mutex> lock(g_seats.mx);
	if (g_seats.swaps.empty()) return false;
	from = g_seats.swaps.front().first;
	text = g_seats.swaps.front().second;
	g_seats.swaps.pop_front();
	return true;
}

bool SteamTakeSeatRequest(uint64_t &member, int &slot)
{
	std::lock_guard<std::mutex> lock(g_seats.mx);
	if (g_seats.requests.empty()) return false;
	member = g_seats.requests.front().first;
	slot = g_seats.requests.front().second;
	g_seats.requests.pop_front();
	return true;
}

std::vector<std::string> SteamMissingScenario()
{
	std::vector<std::string> out;
	std::lock_guard<std::mutex> lock(g_lock);
	const std::vector<std::string> f = SplitTabs(g_share.scenarioValue);
	const bool fromMod = f.size() >= 6 && !f[3].empty();
	if (f.size() < 3 || (_strtoui64(f[1].c_str(), nullptr, 10) == 0 && !fromMod)) return out;
	for (const OnlineMember &m : g_room.members) {
		if (m.me) continue;
		const auto it = g_share.mapHave.find(m.id);
		if (it == g_share.mapHave.end() || it->second != g_share.scenarioValue) out.push_back(m.name);
	}
	return out;
}

std::string SteamScenarioModName()
{
	std::lock_guard<std::mutex> lock(g_lock);
	const std::vector<std::string> f = SplitTabs(g_share.scenarioValue);
	return f.size() >= 6 && !f[3].empty() ? f[5] : std::string();
}

std::string SteamVersionMismatch(const std::string &roomVersion, const std::string &roomRulesLevel)
{
	const std::string mine = ModVersionText();
	if (roomVersion == mine) {
		if (roomRulesLevel.empty() || roomRulesLevel == ModsRulesLevel()) return "";
		LogW("steam", "the room uses %s %s and this build %s", mine.c_str(), roomRulesLevel.c_str(), ModsRulesLevel().c_str());
		return TrFmt("That room uses another build of Imperivm Resurrected %s. Both must install the same build.", {mine});
	}
	if (roomVersion.empty()) {
		return "That room uses an old version of Imperivm Resurrected (before 0.29). The host has to update to play "
			   "with you.";
	}
	if (ModsCompareVersions(roomVersion, mine) > 0) {
		return TrFmt("That room uses Imperivm Resurrected %s and you have %s. Update from the main menu to join.", {roomVersion, mine});
	}
	return TrFmt("That room uses Imperivm Resurrected %s, older than your %s. The host has to update to play with you.", {roomVersion, mine});
}

void SteamReportDownloadProgress(uint64_t got, uint64_t total)
{
	static DWORD last = 0;
	if (!SteamOnlineReady() || !total || GetTickCount() - last < 1000) return;
	last = GetTickCount();
	Post([got, total] {
		if (!g_lobby) return;
		const uint64 owner = S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby);
		if (!owner || owner == g_steam.me) return;
		const std::string text = std::to_string(got) + " " + std::to_string(total);
		ShareSend(owner, kShProgress, text.data(), text.size());
	});
}

void SteamBroadcastCamera(int player, int x, int y)
{
	if (!SteamOnlineReady()) return;
	Post([player, x, y] {
		if (!g_lobby) return;
		const std::string text = std::to_string(player) + " " + std::to_string(x) + " " + std::to_string(y);
		const int n = S.SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(g_steam.mm, g_lobby);
		for (int i = 0; i < n; i++) {
			const uint64 m = S.SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(g_steam.mm, g_lobby, i);
			if (m != g_steam.me) ShareSend(m, kShCam, text.data(), text.size());
		}
	});
}

std::vector<NetPeerStat> SteamNetStats()
{
	std::lock_guard<std::mutex> lock(g_peerStats.mx);
	return g_peerStats.stats;
}

bool SteamTakeKicked()
{
	return g_signal.kicked.exchange(false);
}

void SteamRejoinSend(uint64_t peer, uint8_t type, const std::string &data)
{
	if (!SteamOnlineReady() || !peer) return;
	Post([peer, type, data] {
		g_rejoinAllowed.insert(peer);
		g_share.rejoinOut.push_back({peer, type, data});
		PumpRejoinOut();
	});
}

void SteamRejoinAllow(uint64_t peer)
{
	if (!SteamOnlineReady() || !peer) return;
	Post([peer] {
		g_rejoinAllowed.insert(peer);
		if (g_lobby && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
			S.SteamAPI_ISteamMatchmaking_SetLobbyJoinable(g_steam.mm, g_lobby, true);
			LogI("rejoin", "room open for %s", Name(peer).c_str());
		}
	});
}

void SteamRejoinJoin(uint64_t lobby)
{
	if (!SteamOnlineReady() || !lobby) return;
	g_signal.rejoinJoining = true;
	g_signal.joinFailCode = 0;
	Post([lobby] { Join(lobby); });
}

int SteamRejoinJoinFailed()
{
	return g_signal.joinFailCode.exchange(0);
}

void SteamRejoinJoinEnd()
{
	g_signal.rejoinJoining = false;
	g_signal.joinFailCode = 0;
}

uint64_t SteamRejoinLobby()
{
	return g_lobby;
}

int SteamLobbyNumberOf(uint64_t id)
{
	if (!g_lobby || !id) return 0;
	return atoi(Data(g_lobby, ("k:" + std::to_string(id)).c_str()).c_str());
}
