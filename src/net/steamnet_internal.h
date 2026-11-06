// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#pragma once
#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#include <steam/steam_api_flat.h>
#pragma warning(pop)
#include <deque>
#include <functional>
#include <set>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cstdint>
#include "net/sharemsg.h"
#include "net/steamnet.h"

#define STEAM_FUNCS(X)                                                                                     \
	X(SteamAPI_InitFlat) X(SteamAPI_ManualDispatch_Init) X(SteamAPI_ManualDispatch_RunFrame)               \
	X(SteamAPI_ManualDispatch_GetNextCallback) X(SteamAPI_ManualDispatch_FreeLastCallback)                 \
	X(SteamAPI_ManualDispatch_GetAPICallResult) X(SteamAPI_GetHSteamPipe) X(SteamAPI_SteamUser_v023)       \
	X(SteamAPI_ISteamUser_GetSteamID) X(SteamAPI_SteamFriends_v018) X(SteamAPI_ISteamFriends_GetPersonaName) \
	X(SteamAPI_ISteamFriends_GetFriendPersonaName) X(SteamAPI_ISteamFriends_SetRichPresence)               \
	X(SteamAPI_ISteamFriends_ClearRichPresence) X(SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog)  \
	X(SteamAPI_SteamUtils_v011) X(SteamAPI_ISteamUtils_GetAppID) X(SteamAPI_SteamApps_v009)                \
	X(SteamAPI_ISteamApps_GetLaunchCommandLine) X(SteamAPI_SteamMatchmaking_v009)                          \
	X(SteamAPI_ISteamMatchmaking_CreateLobby) X(SteamAPI_ISteamMatchmaking_SetLobbyData)                   \
	X(SteamAPI_ISteamMatchmaking_GetLobbyData) X(SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter) \
	X(SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter)                                        \
	X(SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter)                                     \
	X(SteamAPI_ISteamMatchmaking_RequestLobbyList) X(SteamAPI_ISteamMatchmaking_GetLobbyByIndex)           \
	X(SteamAPI_ISteamMatchmaking_JoinLobby) X(SteamAPI_ISteamMatchmaking_LeaveLobby)                       \
	X(SteamAPI_ISteamMatchmaking_GetLobbyOwner) X(SteamAPI_ISteamMatchmaking_GetNumLobbyMembers)           \
	X(SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex) X(SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit)  \
	X(SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit)                                                      \
	X(SteamAPI_ISteamMatchmaking_SendLobbyChatMsg) X(SteamAPI_ISteamMatchmaking_GetLobbyChatEntry)         \
	X(SteamAPI_ISteamMatchmaking_SetLobbyJoinable) X(SteamAPI_ISteamMatchmaking_RequestLobbyData)           \
	X(SteamAPI_ISteamFriends_GetFriendCount) X(SteamAPI_ISteamFriends_GetFriendByIndex)                    \
	X(SteamAPI_ISteamFriends_GetFriendGamePlayed) X(SteamAPI_ISteamFriends_HasFriend)                      \
	X(SteamAPI_SteamNetworkingUtils_SteamAPI_v004) X(SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess) \
	X(SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32)                                            \
	X(SteamAPI_SteamNetworkingMessages_SteamAPI_v002) X(SteamAPI_ISteamNetworkingMessages_SendMessageToUser) \
	X(SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel)                                          \
	X(SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser)                                             \
	X(SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser)                                              \
	X(SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo)                                          \
	X(SteamAPI_ISteamFriends_GetMediumFriendAvatar) X(SteamAPI_ISteamFriends_RequestUserInformation)       \
	X(SteamAPI_ISteamUtils_GetImageSize) X(SteamAPI_ISteamUtils_GetImageRGBA)

namespace steamnet_impl {

const char kProtocol[] = "1";

struct SteamFns {
#define DECLARE(name) decltype(&::name) name = nullptr;
	STEAM_FUNCS(DECLARE)
#undef DECLARE
};

extern SteamFns S;
struct SteamHandles {
	ISteamFriends *friends = nullptr;
	ISteamMatchmaking *mm = nullptr;
	ISteamNetworkingMessages *msgs = nullptr;
	HSteamPipe pipe = 0;
	uint64 me = 0;
};
extern SteamHandles g_steam;
extern std::mutex g_lock;
extern bool g_ready;
extern std::string g_status;
extern std::string g_myName;
struct SteamSignals {
	std::atomic<bool> rejoinJoining{false};
	std::atomic<int> joinFailCode{0};
	bool kickedHandled = false;
	std::atomic<bool> kicked{false};
	std::atomic<bool> joinRefused{false};
	std::atomic<bool> wrongPassword{false};
	std::atomic<bool> createFailed{false};
};
extern SteamSignals g_signal;
extern std::atomic<uint32_t> g_membership;
struct ResyncState {
	std::atomic<unsigned long long> lobby{0};
	std::atomic<DWORD> at{0};
};
extern ResyncState g_resync;
struct LobbySearchState {
	std::vector<OnlineLobby> lobbies;
	std::vector<uint64> friendLobbies;
	std::map<uint64, unsigned> dataSerial;
	bool loading = false;
	DWORD loadingSince = 0;
};
extern LobbySearchState g_search;
const DWORD kSearchPatienceMs = 20000;

extern OnlineRoom g_room;
extern std::string g_notice;
struct PendingJoinState {
	uint64 id = 0;
	uint64 joiningLobby = 0;
	uint64 syncLobby = 0;
	DWORD deadline = 0;
	bool invite = false;
	std::string password;
};
extern PendingJoinState g_pendingJoin;
extern std::deque<std::function<void()>> g_queue;
extern std::set<uint64> g_rejoinAllowed;
void Notice(const std::string &text);
void Post(std::function<void()> fn);
std::string Name(uint64 id);
SteamNetworkingIdentity Identity(uint64 id);
std::string Fnv(const std::string &s);
std::string Data(uint64 lobby, const char *key);
std::string EncodeMods(const std::string &scenModId, int &optional);
std::string OwnKey();
bool IsBanned(uint64 lobby, uint64 id);
bool StaleLobby(uint64 id);
OnlineLobby ReadLobby(uint64 id);
struct PeerStatsState {
	std::mutex mx;
	std::vector<NetPeerStat> stats;
};
extern PeerStatsState g_peerStats;
NetPeerStat NetStatOf(uint64 peer);
std::string RouteInfo(uint64 peer);
void LogRouteChange(uint64 peer, const std::string &name, const std::string &route);
void LogSilence(uint64 peer, const std::string &name, bool playing);
extern uint64 g_lobby;
extern long long g_startedAt;
bool InLobby(uint64 lobby, uint64 who);
void SetPresence();
void AssignNumbers();
void RefreshOwnKey();
void TakeOverHostData();
void PublishPlayerList();
extern std::map<uint64, DWORD> g_sentDoneAt;
extern std::map<uint64, std::pair<std::string, DWORD>> g_restarting;
void UpdateRoom();
void AddChatLine(uint64_t id, const std::string &who, const std::string &text);
struct LeaveState {
	DWORD at = 0;
	std::atomic<bool> active{false};
	bool skipRefresh = false;
};
extern LeaveState g_leave;
void RequestLobbySearch();
void Leave();
void RefuseForContent(uint64 lobby, const OnlineLobby &info, RoomContent content);
void RememberLobby(uint64 lobby);
void ResolvePendingJoin();
void Join(uint64 lobby);
struct SpectatorLobbyState {
	uint64 lobby = 0;
	bool creating = false;
	DWORD creatingSince = 0;
	bool wanted = false;
	int delay = 30;
	std::map<std::string, std::string> sent;
	std::mutex mx;
	std::vector<uint64_t> ids;
	int visibility = 0;
};
extern SpectatorLobbyState g_spectate;
void OpenSpectators(int delay, int visibility);
void CloseSpectators();
void MirrorSpectators();
void FoldSpectator(OnlineLobby &row, const OnlineLobby &s);
void MergeSpectateLobbies(std::vector<OnlineLobby> &list);
extern OnlineCreateSettings g_creating;
void PublishMods(const std::string &scenModId);
void DispatchCallbacks();
const int kShareChannel = 2;

const int kShareChunk = 64 * 1024;

enum : uint8_t { kShReq = 1, kShFile = 2, kShData = 3, kShModEnd = 4, kShDeny = 5, kShDone = 6, kShMapReq = 7, kShMapFile = 8, kShMapEnd = 9, kShCam = 10, kShSeat = 11, kShSwap = 12, kShProgress = 13, kShMapHave = 14 };
static_assert(kContentMessages.Has(kShReq) && kContentMessages.Has(kShMapHave), "content messages out of their range");

struct RejoinOut {
	uint64 peer;
	uint8_t type;
	std::string data;
};

struct SeatState {
	std::mutex mx;
	std::deque<std::pair<uint64_t, int>> requests;
	std::deque<std::pair<uint64_t, std::string>> swaps;
};
extern SeatState g_seats;
void PumpRejoinOut();
EResult ShareSend(uint64 peer, uint8_t type, const void *data, size_t len);
std::vector<std::string> SplitTabs(const std::string &s);
struct ShareStep {
	uint8_t type;
	std::string text;
	std::string path;
};

struct OutTransfer {
	uint64 peer = 0;
	std::vector<ShareStep> steps;
	size_t step = 0;
	bool headerSent = false;
	HANDLE file = INVALID_HANDLE_VALUE;
	std::vector<uint8_t> pending;
	uint64_t total = 0, sent = 0;
	bool mods = false;
};

struct ExtProgress {
	uint64_t got = 0, total = 0;
	DWORD stamp = 0;
};

void HostOnRequest(uint64 peer, const std::string &text);
void PumpOut();
void DropTransfers(uint64 peer);
struct InTransfer {
	uint64 host = 0;
	HANDLE file = INVALID_HANDLE_VALUE;
	std::string tempRoot;
	uint64_t fileLeft = 0, budget = 0;
};

void FailDownload(const std::string &why);
void AbortDownload();
bool DownloadActive();
void MakeDirs(const std::string &path);
void DeleteTree(const std::string &dir);
void ClientOnMessage(uint64 peer, const uint8_t *data, size_t len);
void PublishScenario(const std::string &path);
struct ShareState {
	std::deque<RejoinOut> rejoinOut;
	std::vector<OutTransfer> out;
	std::map<uint64_t, std::pair<uint64_t, uint64_t>> sendProgress;
	std::string scenarioValue;
	std::map<uint64_t, std::string> mapHave;
	std::map<uint64_t, ExtProgress> extProgress;
	InTransfer in;
	OnlineModDownload download;
	OnlineMapStatus mapStatus;
	OnlineMapStatus saveStatus;
};
extern ShareState g_share;
void CheckRoomScenario();
void CheckRoomSave();
void PublishSave(const std::string &file, const std::string &name);
void ResetRoomScenario();
void ReceiveShare();
std::string IniPath();
void SteamThread();

}
