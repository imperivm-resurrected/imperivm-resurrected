// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>
#include <atomic>
#pragma warning(push)
#pragma warning(disable : 4996 4828)
#pragma warning(pop)
#include "core/guard.h"
#include "net/rejoin.h"
#include "net/spectator.h"
#include "net/vnet.h"
#include "net/patrons.h"

#include "net/steamnet_internal.h"
#include "core/uiport.h"

using namespace steamnet_impl;

namespace steamnet_impl {
SteamSignals g_signal;
}

namespace steamnet_impl {
namespace {
const int kVnetChannel = 0;
}
}

namespace steamnet_impl {
SteamFns S;

SteamHandles g_steam;

std::mutex g_lock;
bool g_ready = false;
std::string g_status = "Starting Steam...";
std::string g_myName;
PendingJoinState g_pendingJoin;
std::atomic<uint32_t> g_membership{0};
LeaveState g_leave;
LobbySearchState g_search;
ResyncState g_resync;

OnlineRoom g_room;
std::string g_notice;
std::deque<std::function<void()>> g_queue;
std::set<uint64> g_rejoinAllowed;

PeerStatsState g_peerStats;

NetPeerStat NetStatOf(uint64 peer)
{
	NetPeerStat n;
	n.id = peer;
	SteamNetworkingIdentity who = Identity(peer);
	SteamNetConnectionInfo_t info = {};
	SteamNetConnectionRealTimeStatus_t status = {};
	const ESteamNetworkingConnectionState st =
		S.SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo(g_steam.msgs, who, &info, &status);
	n.connected = st == k_ESteamNetworkingConnectionState_Connected;
	if (!n.connected) return n;
	n.relay = (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0;
	n.ping = status.m_nPing;
	n.quality = status.m_flConnectionQualityLocal;
	n.inKBs = status.m_flInBytesPerSec / 1024.0f;
	n.outKBs = status.m_flOutBytesPerSec / 1024.0f;
	n.inPkts = status.m_flInPacketsPerSec;
	n.outPkts = status.m_flOutPacketsPerSec;
	return n;
}

std::string RouteInfo(uint64 peer)
{
	SteamNetworkingIdentity who = Identity(peer);
	SteamNetConnectionInfo_t info = {};
	SteamNetConnectionRealTimeStatus_t status = {};
	const ESteamNetworkingConnectionState st =
		S.SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo(g_steam.msgs, who, &info, &status);
	if (st != k_ESteamNetworkingConnectionState_Connected) return "connecting";
	return std::string((info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) ? "Steam relay" : "direct") + ", " +
		std::to_string(status.m_nPing) + " ms";
}
}

namespace steamnet_impl {
namespace {
void VnetSend(uint64_t peer, const void *data, int len)
{
	SteamNetworkingIdentity who = Identity(peer);
	S.SteamAPI_ISteamNetworkingMessages_SendMessageToUser(g_steam.msgs, who, data, static_cast<uint32>(len),
		k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession, kVnetChannel);
}

std::string SessionDetail(uint64 peer)
{
	SteamNetworkingIdentity who = Identity(peer);
	SteamNetConnectionInfo_t info = {};
	SteamNetConnectionRealTimeStatus_t status = {};
	const ESteamNetworkingConnectionState st =
		S.SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo(g_steam.msgs, who, &info, &status);
	const char *state = "unknown";
	switch (st) {
	case k_ESteamNetworkingConnectionState_None: state = "no session"; break;
	case k_ESteamNetworkingConnectionState_Connecting: state = "connecting"; break;
	case k_ESteamNetworkingConnectionState_FindingRoute: state = "finding route"; break;
	case k_ESteamNetworkingConnectionState_Connected: state = "connected"; break;
	case k_ESteamNetworkingConnectionState_ClosedByPeer: state = "closed by the peer"; break;
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: state = "problem detected locally"; break;
	default: break;
	}
	char text[512];
	if (info.m_eEndReason) {
		snprintf(text, sizeof text, "session %s, reason %d: %s", state, static_cast<int>(info.m_eEndReason), info.m_szEndDebug);
	}
	else if (st == k_ESteamNetworkingConnectionState_Connected) {
		snprintf(text, sizeof text, "session %s, ping %d ms, quality %.2f", state, status.m_nPing, status.m_flConnectionQualityLocal);
	}
	else {
		snprintf(text, sizeof text, "session %s", state);
	}
	return text;
}
}
}

namespace steamnet_impl {
void LogRouteChange(uint64 peer, const std::string &name, const std::string &route)
{
	static std::map<uint64, std::string> last;
	const std::string kind = route.substr(0, route.find(','));
	if (last[peer] != kind) {
		last[peer] = kind;
		LogD("steam", "connection with %s: %s%s", name.c_str(), route.c_str(), kind == "connecting" ? (" (" + SessionDetail(peer) + ")").c_str() : "");
	}
}

void LogSilence(uint64 peer, const std::string &name, bool playing)
{
	static std::map<uint64, DWORD> since;
	const int quiet = playing ? VnetSilenceMs(peer) : -1;
	auto it = since.find(peer);
	if (quiet >= 3000) {
		if (it == since.end()) {
			since[peer] = GetTickCount() - static_cast<DWORD>(quiet);
			LogW("net", "virtual network: no packets from %s for %.1f s (%s)", name.c_str(), quiet / 1000.0, SessionDetail(peer).c_str());
		}
	}
	else if (it != since.end()) {
		if (quiet >= 0) LogD("net", "virtual network: packets from %s are back after %.1f s without them", name.c_str(), (GetTickCount() - it->second) / 1000.0);
		since.erase(it);
	}
}

uint64 g_lobby = 0;
}

namespace steamnet_impl {
namespace {
DWORD g_lastRoomUpdate = 0;
}
}

namespace steamnet_impl {
SpectatorLobbyState g_spectate;
}

namespace steamnet_impl {
namespace {
int g_specType = k_ELobbyTypePublic;
}
}

namespace steamnet_impl {
long long g_startedAt = 0;

OnlineCreateSettings g_creating;
}

namespace steamnet_impl {
namespace {
void ReceiveVnet()
{
	SteamNetworkingMessage_t *in[64];
	const int n = S.SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(g_steam.msgs, kVnetChannel, in, 64);
	for (int i = 0; i < n; i++) {
		VnetDeliver(in[i]->m_identityPeer.GetSteamID64(), in[i]->m_pData, in[i]->m_cbSize);
		in[i]->Release();
	}
}

bool LoadSteam()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "steam_api.dll");
	HMODULE h = LoadLibraryA(path);
	if (!h) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_status = "steam_api.dll is missing from the game folder";
		return false;
	}
	bool ok = true;
#define RESOLVE(name)                                                                \
	S.name = reinterpret_cast<decltype(S.name)>(GetProcAddress(h, #name));           \
	if (!S.name) {                                                                   \
		LogW("steam", "%s is missing in steam_api.dll (different SDK version?)", #name);   \
		ok = false;                                                                  \
	}
	STEAM_FUNCS(RESOLVE)
#undef RESOLVE
	if (!ok) {
		std::lock_guard<std::mutex> lock(g_lock);
		g_status = "steam_api.dll is from another version";
	}
	return ok;
}
}
}

namespace steamnet_impl {
std::string IniPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	char *slash = strrchr(path, '\\');
	if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "imperivm-mod.ini");
	return path;
}
}

namespace steamnet_impl {
namespace {
uint64 TakeRejoin()
{
	char value[64] = {};
	GetPrivateProfileStringA("Online", "ReturnToRoom", "", value, sizeof(value), IniPath().c_str());
	WritePrivateProfileStringA("Online", "ReturnToRoom", nullptr, IniPath().c_str());
	unsigned long long lobby = 0, when = 0;
	if (sscanf_s(value, "%llu %llu", &lobby, &when) != 2) return 0;
	return static_cast<unsigned long long>(time(nullptr)) - when < 600 ? lobby : 0;
}

uint64 LaunchLobby()
{
	char cmd[1024] = {};
	S.SteamAPI_ISteamApps_GetLaunchCommandLine(S.SteamAPI_SteamApps_v009(), cmd, sizeof(cmd));
	std::string all = std::string(cmd) + " " + GetCommandLineA();
	const size_t p = all.find("+connect_lobby ");
	return p == std::string::npos ? 0 : _strtoui64(all.c_str() + p + 15, nullptr, 10);
}

void SteamTick()
{
	ReceiveVnet();
	DispatchCallbacks();
	for (;;) {
		std::function<void()> fn;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (g_queue.empty()) break;
			fn = std::move(g_queue.front());
			g_queue.pop_front();
		}
		fn();
	}
	ReceiveVnet();
	ReceiveShare();
	PumpRejoinOut();
	if (g_leave.at && static_cast<int>(GetTickCount() - g_leave.at) >= 0) Leave();
	if (g_pendingJoin.id && static_cast<int>(GetTickCount() - g_pendingJoin.deadline) >= 0) ResolvePendingJoin();
	if (!g_lobby) ResetRoomScenario();
	const DWORD now = GetTickCount();
	if (g_lobby && now - g_lastRoomUpdate > 1000) {
		g_lastRoomUpdate = now;
		TakeOverHostData();
		RefreshOwnKey();
		AssignNumbers();
		PublishPlayerList();
		UpdateRoom();
		MirrorSpectators();
		CheckRoomScenario();
		CheckRoomSave();
	}
	static DWORD lastBeat = 0;
	static uint64 beatLobby = 0;
	if (g_lobby && (now - lastBeat > 30000 || beatLobby != g_lobby) && S.SteamAPI_ISteamMatchmaking_GetLobbyOwner(g_steam.mm, g_lobby) == g_steam.me) {
		lastBeat = now;
		beatLobby = g_lobby;
		S.SteamAPI_ISteamMatchmaking_SetLobbyData(g_steam.mm, g_lobby, "alive", std::to_string(static_cast<long long>(time(nullptr))).c_str());
	}
	static DWORD lastStats = 0;
	if (now - lastStats > 60000) {
		lastStats = now;
		VnetLogStats();
	}
	static DWORD lastWake = 0;
	if (now - lastWake > 250 && (RejoinWantsWake() || SpectatorWantsWake())) {
		lastWake = now;
		UiWakeGameThread();
	}
	VnetPump(1);
}
}
}

namespace steamnet_impl {
void SteamThread()
{
	Sleep(3000);
	if (!LoadSteam()) {
		LogW("steam", "online disabled (no steam_api.dll)");
		return;
	}
	SteamErrMsg err = {};
	const ESteamAPIInitResult r = S.SteamAPI_InitFlat(&err);
	if (r != k_ESteamAPIInitResult_OK) {
		LogE("steam", "could not start (%d: %s)", r, err);
		std::lock_guard<std::mutex> lock(g_lock);
		g_status = "Steam is not available: open the game from Steam";
		return;
	}
	S.SteamAPI_ManualDispatch_Init();
	g_steam.pipe = S.SteamAPI_GetHSteamPipe();
	g_steam.friends = S.SteamAPI_SteamFriends_v018();
	g_steam.mm = S.SteamAPI_SteamMatchmaking_v009();
	g_steam.msgs = S.SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
	g_steam.me = S.SteamAPI_ISteamUser_GetSteamID(S.SteamAPI_SteamUser_v023());
	PatronsWatch({g_steam.me});
	S.SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(S.SteamAPI_SteamNetworkingUtils_SteamAPI_v004());
	{
		ISteamNetworkingUtils *u = S.SteamAPI_SteamNetworkingUtils_SteamAPI_v004();
		S.SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32(u, k_ESteamNetworkingConfig_SendRateMin, 512 * 1024);
		S.SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32(u, k_ESteamNetworkingConfig_SendRateMax, 16 * 1024 * 1024);
		S.SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32(u, k_ESteamNetworkingConfig_SendBufferSize, 4 * 1024 * 1024);
	}
	{
		std::lock_guard<std::mutex> lock(g_lock);
		g_myName = S.SteamAPI_ISteamFriends_GetPersonaName(g_steam.friends);
		g_ready = true;
		g_status.clear();
	}
	VnetSetSender(&VnetSend);
	LogI("steam", "online ready as %s (%llu), appid %u", g_myName.c_str(), g_steam.me,
		S.SteamAPI_ISteamUtils_GetAppID(S.SteamAPI_SteamUtils_v011()));
	if (const uint64 lobby = LaunchLobby()) {
		LogI("steam", "the game was opened from an invitation");
		g_pendingJoin.invite = true;
		Join(lobby);
	}
	else if (const uint64 lobby = TakeRejoin()) {
		LogI("steam", "back in the room after restarting with its mods");
		Join(lobby);
	}
	for (;;) {
		if (!Guard("Steam", SteamTick)) Sleep(50);
	}
}
}
